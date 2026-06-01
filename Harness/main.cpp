// =============================================================================
// Unravel offline DSP measurement harness
// =============================================================================
// Drives the SHIPPING HPSSProcessor through synthetic signals and measures
// per-stream energy at each XY-pad corner, reporting isolation in dB.
//
// It reproduces the plugin's PluginProcessor::updateParameters() corner-aware
// compensation math (transient corner scaling + spectralFloor corner lift)
// exactly, because that math — not just MaskEstimator — determines real corner
// isolation. The DSP itself (HPSSProcessor/STFTProcessor/MagPhaseFrame/
// MaskEstimator) is the real, unmodified plugin code.
// =============================================================================

#include <JuceHeader.h>
#include "HPSSProcessor.h"
#include "MaskEstimator.h"
#include "HarmonicMaskDetector.h"
#include "MaskReconciler.h"
#include "STFTProcessor.h"
#include "LowFreqPartialTracker.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include <algorithm>

namespace
{
constexpr double kSR        = 48000.0;
constexpr int    kBlock     = 512;
constexpr int    kNumBlocks = 400;          // ~4.3 s — long enough for HPSS history + smoothers to settle
constexpr int    kWarmupBlocks = 120;       // discard transient startup region from energy measurement

// ---- dB <-> linear, matching PluginProcessor::updateParameters() ----
float dbToLinear (float db) noexcept { return db <= -60.0f ? 0.0f : std::pow (10.0f, db / 20.0f); }

// -------------------------------------------------------------------------
// Reproduces PluginProcessor::updateParameters() gain + corner compensation.
// Inputs are the *raw* user gains (dB) the XY pad / sliders would set.
// Outputs the linear stream gains AND the effective spectralFloor actually
// sent to the MaskEstimator. Mirrors the shipping code exactly.
// -------------------------------------------------------------------------
struct ResolvedParams
{
    float tonalGain;
    float noiseGain;
    float transientGain;
    float spectralFloor;   // 0..1, after corner lift
};

ResolvedParams resolveParams (float tonalDb, float noiseDb, float transientDb,
                              float userFloor01 /* 0..1 */)
{
    float tonalGain     = dbToLinear (tonalDb);
    float noiseGain     = dbToLinear (noiseDb);
    float transientGain = dbToLinear (transientDb);

    const bool anySolo = false; // harness never solos

    // Pad-corner transient scaling (PluginProcessor.cpp ~L394-395)
    if (! anySolo)
        transientGain *= std::min ({ tonalGain, noiseGain, 1.0f });

    // Pad-asymmetry spectralFloor lift (PluginProcessor.cpp ~L426-433)
    const float maxPadGain = std::max (tonalGain, noiseGain);
    float cornerFactor = 0.0f;
    if (! anySolo && maxPadGain > 1e-9f)
    {
        const float asymmetry = 1.0f - std::min (tonalGain, noiseGain) / maxPadGain;
        cornerFactor = asymmetry * asymmetry * asymmetry * asymmetry; // ^4
    }
    const float spectralFloor = std::max (userFloor01, cornerFactor);

    return { tonalGain, noiseGain, transientGain, spectralFloor };
}

// Nearest frequency to `freq` that completes an integer number of cycles in
// `bufLen` samples at kSR — guarantees seamless looping with no wrap discontinuity.
double seamlessFreq (double freq, int bufLen)
{
    const double cycles = std::round (freq * (double) bufLen / kSR);
    return (cycles < 1.0 ? 1.0 : cycles) * kSR / (double) bufLen;
}

// -------------------------------------------------------------------------
// Synthetic signal generators
// -------------------------------------------------------------------------
void genSine (std::vector<float>& x, double freq, float amp)
{
    for (size_t n = 0; n < x.size(); ++n)
        x[n] = amp * std::sin (2.0 * M_PI * freq * (double) n / kSR);
}

void genNoise (std::vector<float>& x, float amp, uint32_t seed)
{
    juce::Random rng ((juce::int64) seed);
    for (auto& s : x) s = amp * (rng.nextFloat() * 2.0f - 1.0f);
}

void genClickTrain (std::vector<float>& x, double hz, float amp)
{
    std::fill (x.begin(), x.end(), 0.0f);
    const int period = (int) (kSR / hz);
    for (size_t n = 0; n < x.size(); n += (size_t) period)
    {
        // Short 2-sample bipolar click (broadband)
        x[n] = amp;
        if (n + 1 < x.size()) x[n + 1] = -amp;
    }
}

// Just the lightsaber hum (the two sustained low partials, no crackle bed) —
// seamless. Used to measure hum rejection without the crackle, which the noise
// corner legitimately keeps and which would otherwise dominate whole-signal energy.
void genHum (std::vector<float>& x)
{
    const double hum100 = seamlessFreq (100.0, (int) x.size());
    const double hum160 = seamlessFreq (160.0, (int) x.size());
    for (size_t n = 0; n < x.size(); ++n)
    {
        const double t = (double) n / kSR;
        x[n] = 0.35f * std::sin (2.0 * M_PI * hum100 * t)
             + 0.20f * std::sin (2.0 * M_PI * hum160 * t);
    }
}

// "Lightsaber-like": sustained hum (two low tones) + steady crackle (filtered noise).
void genLightsaber (std::vector<float>& x, uint32_t seed)
{
    // Snap hum partials to integer-cycle frequencies for seamless looping.
    const double hum100 = seamlessFreq (100.0, (int) x.size());
    const double hum160 = seamlessFreq (160.0, (int) x.size());
    juce::Random rng ((juce::int64) seed);
    float lp = 0.0f;
    for (size_t n = 0; n < x.size(); ++n)
    {
        const double t = (double) n / kSR;
        float hum = 0.35f * std::sin (2.0 * M_PI * hum100 * t)
                  + 0.20f * std::sin (2.0 * M_PI * hum160 * t);
        float white = rng.nextFloat() * 2.0f - 1.0f;
        lp += 0.25f * (white - lp);            // mild low-pass -> "crackle" bed
        float crackle = 0.18f * lp;
        x[n] = hum + crackle;
    }
}

// -------------------------------------------------------------------------
// Run a signal through HPSSProcessor with given resolved params; return the
// post-warmup output energy (sum of squares over the measured region).
// A fresh processor is used each call so HPSS history doesn't leak between runs.
// -------------------------------------------------------------------------
double measureOutputEnergy (const std::vector<float>& signal,
                            float separation01, float focus01,
                            const ResolvedParams& p)
{
    HPSSProcessor proc (false); // high-quality 2048/512, same as plugin
    proc.prepare (kSR, kBlock);
    proc.setSeparation (separation01);
    proc.setFocus (focus01);
    proc.setSpectralFloor (p.spectralFloor);

    std::vector<float> in (kBlock, 0.0f), out (kBlock, 0.0f);
    double energy = 0.0;
    size_t readPos = 0;

    for (int b = 0; b < kNumBlocks; ++b)
    {
        for (int i = 0; i < kBlock; ++i)
        {
            in[(size_t) i] = signal[readPos % signal.size()];
            ++readPos;
        }
        proc.processBlock (in.data(), out.data(), kBlock,
                           p.tonalGain, p.noiseGain, p.transientGain);

        if (b >= kWarmupBlocks)
            for (int i = 0; i < kBlock; ++i)
                energy += (double) out[(size_t) i] * out[(size_t) i];
    }
    return energy;
}

double toDb (double ratio) { return ratio <= 1e-30 ? -300.0 : 10.0 * std::log10 (ratio); }

// -------------------------------------------------------------------------
// Goertzel single-frequency power estimator.
// Returns the power (sum of squared real+imag) of frequency freqHz over the
// n-sample buffer x, normalised by n so amplitudes are comparable across
// different buffer lengths.
// -------------------------------------------------------------------------
double goertzelPower (const float* x, int n, double freqHz, double sr)
{
    const double omega = 2.0 * M_PI * freqHz / sr;
    const double coeff = 2.0 * std::cos (omega);
    double s0 = 0.0, s1 = 0.0, s2 = 0.0;
    for (int i = 0; i < n; ++i)
    {
        s0 = (double) x[i] + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    // Final power: real^2 + imag^2
    const double real = s1 - s2 * std::cos (omega);
    const double imag =       s2 * std::sin (omega);
    return (real * real + imag * imag) / (double) n;
}

// -------------------------------------------------------------------------
// Like measureOutputEnergy but accumulates the full post-warmup output into
// a buffer, then returns Goertzel power summed over the supplied target freqs.
// Also returns whole-signal energy via the out-param for comparison.
// -------------------------------------------------------------------------
double measureBandEnergy (const std::vector<float>& signal,
                          float separation01, float focus01,
                          const ResolvedParams& p,
                          const std::vector<double>& targetFreqsHz,
                          double* outWholeEnergy = nullptr)
{
    HPSSProcessor proc (false);
    proc.prepare (kSR, kBlock);
    proc.setSeparation (separation01);
    proc.setFocus (focus01);
    proc.setSpectralFloor (p.spectralFloor);

    std::vector<float> in (kBlock, 0.0f), out (kBlock, 0.0f);
    std::vector<float> outputBuf;
    outputBuf.reserve ((size_t) ((kNumBlocks - kWarmupBlocks) * kBlock));
    double wholeEnergy = 0.0;
    size_t readPos = 0;

    for (int b = 0; b < kNumBlocks; ++b)
    {
        for (int i = 0; i < kBlock; ++i)
        {
            in[(size_t) i] = signal[readPos % signal.size()];
            ++readPos;
        }
        proc.processBlock (in.data(), out.data(), kBlock,
                           p.tonalGain, p.noiseGain, p.transientGain);

        if (b >= kWarmupBlocks)
        {
            for (int i = 0; i < kBlock; ++i)
            {
                outputBuf.push_back (out[(size_t) i]);
                wholeEnergy += (double) out[(size_t) i] * out[(size_t) i];
            }
        }
    }

    if (outWholeEnergy) *outWholeEnergy = wholeEnergy;

    // Sum Goertzel power at each target frequency
    double bandPower = 0.0;
    const int N = (int) outputBuf.size();
    for (double f : targetFreqsHz)
        bandPower += goertzelPower (outputBuf.data(), N, f, kSR);
    return bandPower;
}

// -------------------------------------------------------------------------
// HUM RESIDUAL DIAGNOSTIC
// Separates true hum bleed from legitimate crackle energy at the noise corner.
// -------------------------------------------------------------------------
void runHumResidualDiagnostic()
{
    std::printf ("\n");
    std::printf ("================================================================\n");
    std::printf ("  HUM RESIDUAL DIAGNOSTIC (Goertzel frequency-selective)\n");
    std::printf ("================================================================\n");

    // -- Build signals ---------------------------------------------------
    const int bufLen = kBlock * 16; // same length as saber in checkIsolationTargets

    // Hum-ONLY signal: exact same partials + amps as genLightsaber, NO crackle.
    const double hum100 = seamlessFreq (100.0, bufLen);
    const double hum160 = seamlessFreq (160.0, bufLen);
    std::vector<float> humOnly (bufLen);
    for (int n = 0; n < bufLen; ++n)
    {
        const double t = (double) n / kSR;
        humOnly[(size_t) n] = 0.35f * (float) std::sin (2.0 * M_PI * hum100 * t)
                            + 0.20f * (float) std::sin (2.0 * M_PI * hum160 * t);
    }

    // Full lightsaber (hum + crackle)
    std::vector<float> saber (bufLen);
    genLightsaber (saber, 777);

    // -- Params ----------------------------------------------------------
    const float sep01 = 0.85f; // match the primary test separation
    ResolvedParams full      = resolveParams (0.0f,   0.0f, 0.0f, 0.0f);
    ResolvedParams noiseOnly = resolveParams (-60.0f, 0.0f, 0.0f, 0.0f);

    // Hum frequencies (actual seamless-snapped values)
    const std::vector<double> humFreqs = { hum100, hum160 };
    // A crackle-band probe frequency (3 kHz is well above the hum, in the crackle bed)
    const std::vector<double> crackleFreq = { 3000.0 };

    // ---- A. HUM-ONLY whole-signal rejection at noise corner ------------
    double humOnlyFull, humOnlyCorner;
    measureBandEnergy (humOnly, sep01, 0.0f, full,      {}, &humOnlyFull);
    measureBandEnergy (humOnly, sep01, 0.0f, noiseOnly, {}, &humOnlyCorner);
    const double humOnlyRejDb = toDb (humOnlyCorner / std::max (humOnlyFull, 1e-30));

    std::printf ("\n  A. HUM-ONLY signal — whole-signal rejection at noise corner\n");
    std::printf ("     full-mix energy  : %.4e\n", humOnlyFull);
    std::printf ("     corner energy    : %.4e\n", humOnlyCorner);
    std::printf ("     rejection        : %+.2f dB\n", humOnlyRejDb);

    // ---- B. FULL lightsaber: Goertzel at hum freqs — noise corner vs full ----
    double saberFullWhole, saberCornerWhole;
    const double saberHumFull   = measureBandEnergy (saber, sep01, 0.0f, full,
                                                     humFreqs, &saberFullWhole);
    const double saberHumCorner = measureBandEnergy (saber, sep01, 0.0f, noiseOnly,
                                                     humFreqs, &saberCornerWhole);
    const double humGoertzelRejDb = toDb (saberHumCorner / std::max (saberHumFull, 1e-30));

    std::printf ("\n  B. FULL lightsaber — Goertzel HUM-FREQUENCY rejection at noise corner\n");
    std::printf ("     hum freqs (Hz)        : %.2f  %.2f\n", hum100, hum160);
    std::printf ("     Goertzel power (full) : %.4e\n", saberHumFull);
    std::printf ("     Goertzel power (noise corner) : %.4e\n", saberHumCorner);
    std::printf ("     TRUE hum rejection    : %+.2f dB\n", humGoertzelRejDb);

    // ---- C. FULL lightsaber: crackle-band Goertzel — noise corner vs full ----
    const double saberCrackFull   = measureBandEnergy (saber, sep01, 0.0f, full,      crackleFreq);
    const double saberCrackCorner = measureBandEnergy (saber, sep01, 0.0f, noiseOnly, crackleFreq);
    const double crackleRejDb = toDb (saberCrackCorner / std::max (saberCrackFull, 1e-30));

    std::printf ("\n  C. FULL lightsaber — Goertzel CRACKLE-BAND (3 kHz) at noise corner vs full\n");
    std::printf ("     Goertzel power (full) : %.4e\n", saberCrackFull);
    std::printf ("     Goertzel power (noise corner) : %.4e\n", saberCrackCorner);
    std::printf ("     Crackle retention     : %+.2f dB  (expect ~0 dB = crackle kept)\n", crackleRejDb);

    // ---- D. FULL lightsaber whole-signal: the original −21 dB number -------
    const double wholeRejDb = toDb (saberCornerWhole / std::max (saberFullWhole, 1e-30));

    std::printf ("\n  D. FULL lightsaber — WHOLE-SIGNAL rejection at noise corner (−21 dB baseline)\n");
    std::printf ("     whole-signal full-mix energy  : %.4e\n", saberFullWhole);
    std::printf ("     whole-signal corner energy    : %.4e\n", saberCornerWhole);
    std::printf ("     whole-signal rejection        : %+.2f dB\n", wholeRejDb);

    std::printf ("\n  SUMMARY\n");
    std::printf ("  -------\n");
    std::printf ("  Hum-only whole-signal rejection : %+.2f dB\n", humOnlyRejDb);
    std::printf ("  Full saber HUM Goertzel rejection: %+.2f dB  <- true hum bleed\n", humGoertzelRejDb);
    std::printf ("  Full saber CRACKLE retention     : %+.2f dB  <- should be ~0 dB\n", crackleRejDb);
    std::printf ("  Full saber WHOLE-signal          : %+.2f dB  <- the original number\n", wholeRejDb);
    std::printf ("================================================================\n");
}

struct Corner { const char* name; float tonalDb; float noiseDb; };

void runSignal (const char* sigName, const std::vector<float>& sig,
                float separationPct)
{
    const float sep01 = separationPct / 100.0f;
    const float focus01 = 0.0f;
    const float transientUserDb = 0.0f; // default unity, as the pad leaves it

    // Reference: full mix, both pad axes at 0 dB (no corner compensation active).
    ResolvedParams full = resolveParams (0.0f, 0.0f, transientUserDb, 0.0f);
    const double eFull = measureOutputEnergy (sig, sep01, focus01, full);

    // The four pad corners (X=tonal dB, Y=noise dB). -inf == -60 dB == 0 gain.
    const std::array<Corner,4> corners = {{
        { "TONAL-ONLY  (T=+0dB, N=-inf)", 0.0f,  -60.0f },
        { "NOISE-ONLY  (T=-inf, N=+0dB)", -60.0f, 0.0f  },
        { "FULL MIX    (T=+0dB, N=+0dB)", 0.0f,   0.0f  },
        { "SILENT      (T=-inf, N=-inf)", -60.0f, -60.0f },
    }};

    std::printf ("\n=== Signal: %s   (separation=%.0f%%) ===\n", sigName, separationPct);
    std::printf ("  Reference full-mix energy: %.4e\n", eFull);
    std::printf ("  %-34s  %12s  %10s  %10s\n", "Corner", "outEnergy", "vsFull(dB)", "floor");

    for (const auto& c : corners)
    {
        ResolvedParams p = resolveParams (c.tonalDb, c.noiseDb, transientUserDb, 0.0f);
        const double e = measureOutputEnergy (sig, sep01, focus01, p);
        std::printf ("  %-34s  %12.4e  %+10.2f  %10.3f\n",
                     c.name, e, toDb (e / std::max (eFull, 1e-30)), p.spectralFloor);
    }
}

// Measure how much of a PURE TONAL signal survives at the NOISE-ONLY corner,
// and how much of a PURE NOISE signal survives at the TONAL-ONLY corner.
// This is the bleed metric the fix targets. Expressed relative to that same
// signal passed at FULL MIX (which is ~unity passthrough).
void runBleedMatrix (float separationPct)
{
    const float sep01 = separationPct / 100.0f;

    std::vector<float> sine (kBlock * 8), noise (kBlock * 8), clicks (kBlock * 8);
    genSine (sine, seamlessFreq (440.0, (int) sine.size()), 0.5f);
    genNoise (noise, 0.5f, 1234);
    genClickTrain (clicks, 8.0, 0.9f);

    struct S { const char* name; std::vector<float>* sig; };
    std::array<S,3> sigs = {{ { "sine440", &sine }, { "noise", &noise }, { "clicks", &clicks } }};

    ResolvedParams full       = resolveParams (0.0f,   0.0f,   0.0f, 0.0f);
    ResolvedParams tonalOnly  = resolveParams (0.0f,  -60.0f,  0.0f, 0.0f);
    ResolvedParams noiseOnly  = resolveParams (-60.0f, 0.0f,   0.0f, 0.0f);

    std::printf ("\n=== BLEED MATRIX (separation=%.0f%%) ===\n", separationPct);
    std::printf ("  %-10s  %12s  %18s  %18s\n",
                 "signal", "fullMix", "tonalCorner(dB)", "noiseCorner(dB)");

    for (auto& s : sigs)
    {
        const double eFull  = measureOutputEnergy (*s.sig, sep01, 0.0f, full);
        const double eTonal = measureOutputEnergy (*s.sig, sep01, 0.0f, tonalOnly);
        const double eNoise = measureOutputEnergy (*s.sig, sep01, 0.0f, noiseOnly);
        std::printf ("  %-10s  %12.4e  %+18.2f  %+18.2f\n",
                     s.name, eFull,
                     toDb (eTonal / std::max (eFull, 1e-30)),
                     toDb (eNoise / std::max (eFull, 1e-30)));
    }
    std::printf ("  Interpretation: tonalCorner row 'noise'  = how much NOISE bleeds when isolating TONAL.\n");
    std::printf ("                  noiseCorner row 'sine440' = how much TONE bleeds when isolating NOISE.\n");
}
// -------------------------------------------------------------------------
// Direct MaskEstimator attribution: feed a STEADY magnitude spectrum for many
// frames so the HPSS horizontal median / IIR fully settle, then print the
// average tonal / transient / noise mask at the signal-bearing bins. This
// isolates WHERE a steady sine's energy goes in the 3-stream split, separate
// from the gain stage. Shows whether residual at the noise corner is a
// tonal->noise misclassification (mask) or pure soft-mask leakage.
// -------------------------------------------------------------------------
void runMaskAttribution (float separationPct, float spectralFloor01)
{
    const int fftSize = 2048;
    const int numBins = fftSize / 2 + 1;

    MaskEstimator est;
    est.prepare (numBins, kSR);
    est.setSeparation (separationPct / 100.0f);
    est.setFocus (0.0f);
    est.setSpectralFloor (spectralFloor01);

    // Build a steady "tonal" magnitude spectrum: one sharp peak at bin ~ (440Hz).
    const int sineBin = (int) std::round (440.0 / (kSR / fftSize));
    std::vector<float> sineMag (numBins, 0.0f), noiseMag (numBins, 0.0f);
    sineMag[(size_t) sineBin]     = 1.0f;
    if (sineBin > 0)     sineMag[(size_t) sineBin - 1] = 0.1f;  // slight window skirt
    if (sineBin + 1 < numBins) sineMag[(size_t) sineBin + 1] = 0.1f;

    juce::Random rng (99);
    for (int i = 1; i < numBins; ++i) noiseMag[(size_t) i] = 0.2f + 0.05f * rng.nextFloat();

    std::vector<float> tMask (numBins), trMask (numBins), nMask (numBins);

    auto settle = [&] (std::vector<float>& mag)
    {
        // Re-randomise noise per frame for the noise case so flux is non-zero;
        // keep the sine static (a true steady tone has ~zero flux).
        for (int f = 0; f < 60; ++f)
        {
            est.updateGuides (juce::Span<const float> (mag.data(), (size_t) numBins));
            est.updateStats  (juce::Span<const float> (mag.data(), (size_t) numBins));
            est.computeMasks (juce::Span<float> (tMask), juce::Span<float> (trMask), juce::Span<float> (nMask));
        }
    };

    std::printf ("\n=== MASK ATTRIBUTION (sep=%.0f%%, floor=%.2f) ===\n", separationPct, spectralFloor01);

    est.reset();
    settle (sineMag);
    std::printf ("  Steady SINE  @ bin %d:  tonal=%.4f  transient=%.4f  noise=%.4f  (sum=%.3f)\n",
                 sineBin, tMask[(size_t) sineBin], trMask[(size_t) sineBin], nMask[(size_t) sineBin],
                 tMask[(size_t) sineBin] + trMask[(size_t) sineBin] + nMask[(size_t) sineBin]);

    est.reset();
    // mid-band noise bin
    const int nb = numBins / 4;
    settle (noiseMag);
    std::printf ("  Steady NOISE @ bin %d:  tonal=%.4f  transient=%.4f  noise=%.4f  (sum=%.3f)\n",
                 nb, tMask[(size_t) nb], trMask[(size_t) nb], nMask[(size_t) nb],
                 tMask[(size_t) nb] + trMask[(size_t) nb] + nMask[(size_t) nb]);
    // Print the mask profile around the sine peak to expose skirt-bin leakage.
    est.reset();
    settle (sineMag);
    std::printf ("  SINE skirt profile (tonal mask) around peak bin %d:\n   ", sineBin);
    for (int b = sineBin - 4; b <= sineBin + 4; ++b)
        std::printf (" b%d:%.3f", b, tMask[(size_t) b]);
    std::printf ("\n");
    std::printf ("  NOTE: at the NOISE-ONLY corner, the SINE's 'tonal' share is silenced but its\n");
    std::printf ("        'noise'+'transient' share survives -> that residual IS the tone bleed.\n");
}
// Returns false if any isolation target is missed. Prints a PASS/FAIL line per check.
bool checkIsolationTargets (float separationPct)
{
    const float sep01 = separationPct / 100.0f;
    bool ok = true;

    // Noise spans the whole measurement window (no loop) so it is genuinely
    // broadband — a short looped noise buffer is periodic, with a stable
    // low-harmonic comb that the partial tracker would (correctly) treat as tonal.
    std::vector<float> sine (kBlock * 8), noise (kBlock * kNumBlocks),
                       clicks (kBlock * 8), hum (kBlock * 16);
    genSine (sine, seamlessFreq (440.0, (int) sine.size()), 0.5f);
    genNoise (noise, 0.5f, 1234);
    genClickTrain (clicks, 8.0, 0.9f);
    genHum (hum);

    auto rejectionDb = [&] (const std::vector<float>& sig, float tonalDb, float noiseDb)
    {
        ResolvedParams full   = resolveParams (0.0f, 0.0f, 0.0f, 0.0f);
        ResolvedParams corner = resolveParams (tonalDb, noiseDb, 0.0f, 0.0f);
        const double eFull   = measureOutputEnergy (sig, sep01, 0.0f, full);
        const double eCorner = measureOutputEnergy (sig, sep01, 0.0f, corner);
        return toDb (eCorner / std::max (eFull, 1e-30));
    };

    // hum @ noise corner uses the crackle-free hum so the measurement reflects
    // tonal rejection, not the crackle the noise corner is supposed to keep.
    struct Check { const char* label; double db; double targetMaxDb; };
    const std::array<Check,4> checks = {{
        { "sine @ noise corner (tonal rejection)",   rejectionDb (sine,  -60.0f, 0.0f), -50.0 },
        { "hum  @ noise corner (hum rejection)",     rejectionDb (hum,   -60.0f, 0.0f), -50.0 },
        { "noise @ tonal corner (noise rejection)",  rejectionDb (noise, 0.0f, -60.0f), -40.0 },
        { "clicks @ tonal corner (click rejection)", rejectionDb (clicks,0.0f, -60.0f), -40.0 },
    }};

    std::printf ("\n=== ISOLATION TARGETS (separation=%.0f%%) ===\n", separationPct);
    for (const auto& c : checks)
    {
        const bool pass = c.db <= c.targetMaxDb;
        ok = ok && pass;
        std::printf ("  [%s] %-42s  %+7.2f dB  (target <= %.0f dB)\n",
                     pass ? "PASS" : "FAIL", c.label, c.db, c.targetMaxDb);
    }
    return ok;
}
bool checkAnalysisOnlyMagnitude()
{
    STFTProcessor::Config cfg; cfg.fftSize = 2048; cfg.hopSize = 512; cfg.analysisOnly = true;
    STFTProcessor stft (cfg);
    stft.prepare (kSR, kBlock);
    std::vector<float> sine (cfg.fftSize * 4);
    genSine (sine, 1000.0, 1.0f);  // 1 kHz -> bin ~ 1000 / (48000/2048) = 42.7
    int produced = 0; const int peakBin = (int) std::round (1000.0 / (kSR / cfg.fftSize));
    bool peakIsMax = false;
    for (size_t off = 0; off + (size_t) kBlock <= sine.size(); off += (size_t) kBlock)
    {
        stft.pushAndProcess (sine.data() + off, kBlock);
        if (stft.isFrameReady())
        {
            auto mag = stft.getCurrentMagnitudes();
            int argmax = 0; for (int b = 1; b < (int) mag.size(); ++b) if (mag[b] > mag[argmax]) argmax = b;
            peakIsMax = std::abs (argmax - peakBin) <= 1;
            ++produced;
        }
    }
    const bool ok = produced > 0 && peakIsMax;
    std::printf ("  [%s] analysisOnly magnitude peak at expected bin\n", ok ? "PASS" : "FAIL");
    return ok;
}
bool checkMaskReconciler()
{
    const int longFft = 8192, shortFft = 2048;
    const int nL = longFft/2 + 1, nS = shortFft/2 + 1;
    MaskReconciler rec; rec.prepare (nL, nS);
    std::vector<float> longMask (nL, 0.0f), shortMask (nS, 0.0f);
    // Put a 1.0 band on the long grid around 4 kHz; expect ~1 at the matching short bin, ~0 far away.
    const int loBin = (int) std::round (4000.0 / (kSR / longFft));
    for (int b = loBin - 20; b <= loBin + 20; ++b) if (b >= 0 && b < nL) longMask[(size_t) b] = 1.0f;
    rec.map (longMask, shortMask);
    const int sBin = (int) std::round (4000.0 / (kSR / shortFft));
    const float atBand = shortMask[(size_t) sBin];
    const float farAway = shortMask[(size_t) (nS / 10)]; // ~2.4 kHz — well away from the 4 kHz band
    // NOTE: nS/6 (~3984 Hz at 48 kHz) falls inside the mapped band; nS/10 is the correct far-away probe.
    // bounds: nothing should exceed [0,1]
    bool inBounds = true; for (float v : shortMask) if (v < -1e-6f || v > 1.0f + 1e-6f) inBounds = false;
    const bool ok = atBand > 0.9f && farAway < 0.1f && inBounds;
    std::printf ("  [%s] mask reconciler: band=%.3f far=%.3f inBounds=%d\n",
                 ok ? "PASS" : "FAIL", atBand, farAway, (int) inBounds);
    return ok;
}
bool checkComputeMasksWithTonal()
{
    const int fftSize = 2048, numBins = fftSize/2 + 1;
    MaskEstimator est; est.prepare (numBins, kSR);
    est.setSeparation (0.85f); est.setSpectralFloor (0.0f);
    std::vector<float> mag (numBins, 0.2f);              // some broadband content for flux/transient
    std::vector<float> ext (numBins, 0.0f);             // external tonal mask: 1.0 in a band
    for (int b = 100; b < 140; ++b) ext[(size_t) b] = 1.0f;
    std::vector<float> t (numBins), tr (numBins), n (numBins);
    bool sumsOk = true; float maxErr = 0.0f;
    for (int f = 0; f < 30; ++f)
    {
        est.updateGuides (juce::Span<const float>(mag.data(), (size_t) numBins));
        est.updateStats  (juce::Span<const float>(mag.data(), (size_t) numBins));
        est.computeMasksWithTonal (juce::Span<const float>(ext.data(), (size_t) numBins),
                                   juce::Span<float>(t), juce::Span<float>(tr), juce::Span<float>(n));
    }
    for (int b = 0; b < numBins; ++b)
    {
        const float s = t[(size_t) b] + tr[(size_t) b] + n[(size_t) b];
        maxErr = std::max (maxErr, std::abs (s - 1.0f));
        if (std::abs (s - 1.0f) > 1e-4f) sumsOk = false;
    }
    // The external tonal band should dominate the tonal mask there.
    const float tonalInBand = t[120];
    const bool ok = sumsOk && tonalInBand > 0.5f;
    std::printf ("  [%s] computeMasksWithTonal: maxSumErr=%.2e  tonal@band=%.3f\n",
                 ok ? "PASS" : "FAIL", maxErr, tonalInBand);
    return ok;
}
bool checkHarmonicDetector()
{
    const int longFft = 8192, numBins = longFft / 2 + 1;
    HarmonicMaskDetector det; det.prepare (numBins); det.setSeparation (0.85f);
    std::vector<float> mag (numBins, 0.0f), mask (numBins, 0.0f);
    const int sineBin = (int) std::round (440.0 / (kSR / longFft));
    mag[(size_t) sineBin] = 1.0f;                       // a clean tone, almost no skirt at 8192
    for (int f = 0; f < 40; ++f) det.process (mag, mask); // settle the median
    const float tonalAtPeak = mask[(size_t) sineBin];
    const float tonalOffPeak = mask[(size_t) (numBins / 3)]; // empty bin
    const bool ok = tonalAtPeak > 0.95f && tonalOffPeak < 0.10f;
    std::printf ("  [%s] harmonic detector: peak tonal=%.3f off-peak=%.3f\n",
                 ok ? "PASS" : "FAIL", tonalAtPeak, tonalOffPeak);
    return ok;
}

// LowFreqPartialTracker discriminates a sustained low tone (gets overridden
// toward tonal) from a frequency-jittering low peak / noise (never confirmed,
// no override). Two cases:
//   HAPPY: a steady 100 Hz tone -> override at the tone bin rises to ~1.
//   EDGE:  a low peak whose frequency jumps every frame -> never confirmed,
//          override stays ~0 (the temporal-stability discriminator at work).
bool checkLowFreqTracker()
{
    const int fftSize = 2048, numBins = fftSize / 2 + 1;

    auto runFrames = [&] (double sr, bool jitter)
    {
        const int toneBin = (int) std::round (100.0 / (sr / fftSize));  // 100 Hz partial
        LowFreqPartialTracker tr;
        tr.prepare (numBins, sr);
        std::vector<float> mag (numBins, 0.02f);  // low broadband floor
        std::vector<float> mask (numBins, 0.0f);
        juce::Random rng (7);
        for (int f = 0; f < 40; ++f)
        {
            std::fill (mag.begin(), mag.end(), 0.02f);
            // place a prominent low peak (+ skirt); steady bin, or jumping bin
            const int b = jitter ? (2 + rng.nextInt (10)) : toneBin;
            if (b - 1 >= 0)      mag[(size_t) (b - 1)] = 0.5f;
            mag[(size_t) b]      = 1.0f;
            if (b + 1 < numBins) mag[(size_t) (b + 1)] = 0.5f;
            tr.process (juce::Span<const float> (mag.data(), (size_t) numBins));
        }
        std::fill (mask.begin(), mask.end(), 0.0f);
        tr.applyOverride (juce::Span<float> (mask.data(), (size_t) numBins));
        float peak = 0.0f;
        bool finite = true;
        for (int b = 0; b < numBins; ++b)
        {
            finite = finite && std::isfinite (mask[(size_t) b]);
            peak = std::max (peak, mask[(size_t) b]);
        }
        return std::pair<float,bool> { finite ? peak : -1.0f, finite };
    };

    // Steady tone must be confirmed/overridden and finite across sample rates (D.2).
    bool ok = true;
    float steadyMin = 1.0f;
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        const auto r = runFrames (sr, false);
        ok = ok && r.second && r.first > 0.8f;
        steadyMin = std::min (steadyMin, r.first);
    }
    const float jitterOverride = runFrames (48000.0, true).first;
    ok = ok && jitterOverride < 0.1f;
    std::printf ("  [%s] low-freq tracker: steady(min over 44.1/48/96k)=%.3f (want>0.8)  jitter=%.3f (want<0.1)\n",
                 ok ? "PASS" : "FAIL", steadyMin, jitterOverride);
    return ok;
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit; // for message-thread-free JUCE bits

    std::printf ("Unravel offline DSP measurement harness\n");
    std::printf ("SR=%.0f  block=%d  FFT=2048/512 (high quality)\n", kSR, kBlock);
    std::printf ("Measuring blocks %d..%d (post-warmup)\n", kWarmupBlocks, kNumBlocks);

    std::vector<float> sine (kBlock * 8), noise (kBlock * 8), clicks (kBlock * 8), saber (kBlock * 16);
    genSine (sine, seamlessFreq (440.0, (int) sine.size()), 0.5f);
    genNoise (noise, 0.5f, 1234);
    genClickTrain (clicks, 8.0, 0.9f);
    genLightsaber (saber, 777);

    for (float sep : { 85.0f, 100.0f, 0.0f })
    {
        runSignal ("pure sine 440Hz", sine, sep);
        runSignal ("broadband noise", noise, sep);
        runSignal ("click train 8Hz", clicks, sep);
        runSignal ("lightsaber hum+crackle", saber, sep);
        runBleedMatrix (sep);
    }

    // Mask attribution at the corner-effective floor (corner lift drives floor->1.0).
    runMaskAttribution (85.0f, 1.0f);
    runMaskAttribution (100.0f, 1.0f);
    runMaskAttribution (85.0f, 0.0f);

    runHumResidualDiagnostic();

    bool targetsOk = true;
    targetsOk &= checkMaskReconciler();
    targetsOk &= checkHarmonicDetector();
    targetsOk &= checkComputeMasksWithTonal();
    targetsOk &= checkAnalysisOnlyMagnitude();
    targetsOk &= checkLowFreqTracker();
    targetsOk &= checkIsolationTargets (85.0f);
    targetsOk &= checkIsolationTargets (100.0f);

    std::printf ("\n%s\n", targetsOk ? "ALL ISOLATION TARGETS MET."
                                     : "ISOLATION TARGETS NOT MET (expected pre-implementation).");
    return targetsOk ? 0 : 1;
}
