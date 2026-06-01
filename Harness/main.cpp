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

    std::vector<float> sine (kBlock * 8), noise (kBlock * 8),
                       clicks (kBlock * 8), saber (kBlock * 16);
    genSine (sine, seamlessFreq (440.0, (int) sine.size()), 0.5f);
    genNoise (noise, 0.5f, 1234);
    genClickTrain (clicks, 8.0, 0.9f);
    genLightsaber (saber, 777);

    auto rejectionDb = [&] (const std::vector<float>& sig, float tonalDb, float noiseDb)
    {
        ResolvedParams full   = resolveParams (0.0f, 0.0f, 0.0f, 0.0f);
        ResolvedParams corner = resolveParams (tonalDb, noiseDb, 0.0f, 0.0f);
        const double eFull   = measureOutputEnergy (sig, sep01, 0.0f, full);
        const double eCorner = measureOutputEnergy (sig, sep01, 0.0f, corner);
        return toDb (eCorner / std::max (eFull, 1e-30));
    };

    struct Check { const char* label; double db; double targetMaxDb; };
    const std::array<Check,4> checks = {{
        { "sine  @ noise corner (tonal rejection)",  rejectionDb (sine,  -60.0f, 0.0f), -50.0 },
        { "saber @ noise corner (hum rejection)",    rejectionDb (saber, -60.0f, 0.0f), -50.0 },
        { "noise @ tonal corner (noise rejection)",  rejectionDb (noise, 0.0f, -60.0f), -40.0 },
        { "clicks @ tonal corner (click rejection)",  rejectionDb (clicks,0.0f, -60.0f), -40.0 },
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

    bool targetsOk = true;
    targetsOk &= checkMaskReconciler();
    targetsOk &= checkHarmonicDetector();
    targetsOk &= checkComputeMasksWithTonal();
    targetsOk &= checkAnalysisOnlyMagnitude();
    targetsOk &= checkIsolationTargets (85.0f);
    targetsOk &= checkIsolationTargets (100.0f);

    std::printf ("\n%s\n", targetsOk ? "ALL ISOLATION TARGETS MET."
                                     : "ISOLATION TARGETS NOT MET (expected pre-implementation).");
    return targetsOk ? 0 : 1;
}
