# AI Assistant Guidelines - JUCE Audio Plugin (VST3/AU)

> **BINDING RULES**: This document is an operational law book. All rules are mandatory requirements that override default behavior. Violations constitute task failure.

---

## A. Document Authority and Scope

This document governs all AI-assisted development on JUCE audio plugin projects within this repository.

**Precedence**: These rules override:
- Default AI behavior
- External conventions
- User requests that contradict these rules

**Scope**: All code modifications, debugging, testing, documentation, and CI/CD work.

**Enforcement**: Every rule in this document is testable and enforceable. Ambiguous interpretation is prohibited.

---

## B. Absolute Rules (Stop Conditions)

### B.1 NEVER Rules (Immediate Stop if Violated)

| Rule | Consequence of Violation |
|------|--------------------------|
| NEVER create mock/simplified versions | Task failure - must use actual JUCE implementations |
| NEVER replace complex systems with simple alternatives | Task failure - must fix root cause |
| NEVER allocate memory in audio callbacks | Real-time safety violation |
| NEVER use locks in processBlock() | Real-time safety violation |
| NEVER modify DSP code without running `stft-validator` agent | Reconstruction integrity at risk |
| NEVER bypass the Completion Checklist | Quality gate violation |

### B.2 ALWAYS Rules (Mandatory for Every Task)

| Rule | Validation Method |
|------|-------------------|
| ALWAYS verify against JUCE documentation before changes | Document reference required |
| ALWAYS validate work using CMake build commands | `cmake --build build` must succeed |
| ALWAYS ensure all plugin formats build (VST3, AU, Standalone) | All targets must compile |
| ALWAYS ensure UI is readable and accessible | No overlapping elements |
| ALWAYS run `code-reviewer` agent after completing code | Agent output required |
| ALWAYS test with pluginval before marking complete | `pluginval --validate` must pass |

### B.3 Stop and Escalate Conditions

STOP and request clarification if:
- Requirements conflict with rules in this document
- Proposed change affects real-time thread safety
- Build fails after changes
- Reconstruction test fails
- Multiple approaches exist with significant trade-offs

---

## C. Project Structure and Architecture

### C.1 Directory Layout

```
Unravel/
├── Source/
│   ├── PluginProcessor.cpp/h    # Main AudioProcessor
│   ├── PluginEditor.cpp/h       # Main AudioProcessorEditor
│   ├── DSP/                     # ACTIVE audio processing
│   │   ├── HPSSProcessor.*      # Main HPSS coordinator (USE THIS)
│   │   ├── STFTProcessor.*      # Time-frequency conversion
│   │   ├── MagPhaseFrame.*      # Magnitude/phase handling
│   │   ├── MaskEstimator.*      # HPSS mask computation
│   │   └── _legacy/             # ARCHIVED - DO NOT USE
│   ├── GUI/                     # UI components
│   └── Parameters/              # ParameterDefinitions.h
├── Tests/                       # Unit tests (-DBUILD_TESTS=ON)
├── Scripts/                     # Build scripts ONLY
├── Documentation/               # Technical docs ONLY
├── JUCE/                        # Framework submodule
└── CMakeLists.txt               # Build configuration
```

### C.2 Processing Architecture

```
UnravelAudioProcessor
├── APVTS (all parameters)
├── HPSSProcessor[] (one per channel)
│   ├── STFTProcessor  → FFT/IFFT with COLA
│   ├── MagPhaseFrame  → Complex ↔ Mag/Phase
│   └── MaskEstimator  → HPSS algorithm
└── Parameter smoothers (20ms ramp)
```

### C.3 File Classification

| Classification | Files | Action |
|----------------|-------|--------|
| ACTIVE | HPSSProcessor, STFTProcessor, MagPhaseFrame, MaskEstimator | USE for all DSP work |
| LEGACY | Source/DSP/_legacy/* | DO NOT modify, reference, or include |
| UI | Source/GUI/* | Follow UI modification workflow |
| PARAMETERS | ParameterDefinitions.h | Follow parameter workflow |

**To remove legacy code**: MUST use `legacy-cleaner` agent.

---

## D. Mandatory Execution Workflows

### D.1 Pre-Task Checklist (Before ANY Code Change)

| Step | Action | Validation |
|------|--------|------------|
| 1 | Review JUCE documentation for relevant APIs | Cite documentation |
| 2 | Build project cleanly | `cmake -B build && cmake --build build` succeeds |
| 3 | Verify plugin loads in DAW | Test VST3/AU load |
| 4 | Understand parameter flow | Parameters → APVTS → UI |
| 5 | Identify thread safety requirements | Document audio vs UI thread |

### D.2 DSP Modification Workflow

| Step | Action | Required Agent |
|------|--------|----------------|
| 1 | Implement in processBlock() | - |
| 2 | Use parameter smoothing | - |
| 3 | Verify NO allocations in audio callbacks | - |
| 4 | Test buffer sizes: 64, 128, 256, 512, 1024 | - |
| 5 | Test sample rates: 44.1k, 48k, 96k | - |
| 6 | Monitor CPU (<30% target) | - |
| 7 | **Run STFT reconstruction test** | `stft-validator` MANDATORY |
| 8 | **Debug any audio issues** | `dsp-debugger` if issues found |

### D.3 UI Modification Workflow

| Step | Action | Required Agent |
|------|--------|----------------|
| 1 | Connect ALL controls to APVTS parameters | - |
| 2 | Use existing attachment patterns | - |
| 3 | Ensure proper spacing (no overlaps) | - |
| 4 | Test responsiveness | - |
| 5 | Verify layout scaling in resized() | - |
| 6 | **Verify accessibility** | `accessibility-pro` for accessibility work |

### D.4 Parameter Addition Workflow

| Step | Action | Required Agent |
|------|--------|----------------|
| 1 | Define ID in ParameterDefinitions.h | - |
| 2 | Add to createParameterLayout() | - |
| 3 | Add UI control in PluginEditor | - |
| 4 | Create APVTS attachment | - |
| 5 | Implement in processBlock() | - |
| 6 | Add smoothing for audio-rate params | - |
| 7 | Test DAW automation | - |
| 8 | **Verify parameter flow** | `parameter-auditor` if issues |

---

## E. Quality Gates and Validation

### E.1 Build Validation Commands

```bash
# Standard build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release

# Build all formats
cmake --build build --target Unravel_VST3
cmake --build build --target Unravel_AU
cmake --build build --target Unravel_Standalone

# Build with tests
cmake -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON
cmake --build build
ctest --test-dir build

# Plugin validation
pluginval --validate build/Unravel_artefacts/Release/VST3/Unravel.vst3

# macOS Code Signing & Notarization (REQUIRED for releases)
./Scripts/sign_and_notarize.sh
```

### E.3 macOS Code Signing & Notarization

**MANDATORY** before any release: Run the signing script to avoid Gatekeeper warnings.

```bash
./Scripts/sign_and_notarize.sh
```

This script:
1. Builds the Release version
2. Signs with Developer ID + hardened runtime
3. Submits to Apple notary service (~2-5 min)
4. Staples the notarization ticket
5. Installs to system plugins folder

**First-time setup**: The script will prompt for Apple ID and app-specific password (create at https://appleid.apple.com/account/manage). Credentials are stored securely in Keychain.

**Verification**: After signing, the plugin should show:
```
spctl --assess: source=Notarized Developer ID
stapler validate: The validate action worked!
```

**Build failures**: MUST use `juce-build-doctor` agent.

### E.4 Completion Checklist (ALL Items Required)

| # | Gate | Validation Command/Method |
|---|------|---------------------------|
| 1 | Build succeeds without warnings | Compiler output clean |
| 2 | All formats build (VST3, AU, Standalone) | All targets compile |
| 3 | Plugin validates with pluginval | `pluginval --validate` passes |
| 4 | No memory leaks | JUCE_LEAK_DETECTOR clean |
| 5 | UI elements properly spaced | Visual inspection |
| 6 | All parameters connected and automatable | DAW automation test |
| 7 | CPU usage < 30% typical | Profiler measurement |
| 8 | DAW automation tested | Logic, Ableton, Pro Tools |
| 9 | Code follows JUCE standards | `code-reviewer` agent run |
| 10 | Plugin loads in target DAWs | Manual load test |
| 11 | **macOS signed & notarized (for releases)** | `./Scripts/sign_and_notarize.sh` |

---

## F. Thread Safety and Real-Time Constraints

### F.1 Thread Classification

| Thread | Allowed Operations | Prohibited Operations |
|--------|-------------------|----------------------|
| Audio Thread | Atomic reads, smoothed values, pre-allocated buffers | Allocations, locks, blocking, file I/O |
| UI Thread | All UI operations, AsyncUpdater | Direct audio buffer access |
| Message Thread | Timer callbacks, repaint triggers | Audio processing |

### F.2 Parameter System Rules

| Rule | Implementation |
|------|----------------|
| Central management | AudioProcessorValueTreeState (APVTS) only |
| Thread-safe access | Atomic parameter access via APVTS |
| Smoothing | Required for all continuous/audio-rate parameters |
| UI connection | Via APVTS attachments only |
| Automation | Full DAW automation support required |

### F.3 Communication Between Threads

| Pattern | Use Case |
|---------|----------|
| Atomic flags | Audio → UI state notification |
| AsyncUpdater | UI updates from audio state |
| Timer | Periodic UI refresh |
| APVTS | Parameter sync |

---

## G. Special Agents System

### G.1 Purpose and Authority

Special agents are **operational tools** with specific expertise. They are invoked to:
- Diagnose complex issues systematically
- Validate work against quality standards
- Perform specialized tasks correctly the first time

Agent output constitutes **authoritative guidance** for their domain.

### G.2 Agent Selection Rules

| Situation | Required Agent | Invocation |
|-----------|----------------|------------|
| DSP audio issues (gain, clipping, artifacts) | `dsp-debugger` | MANDATORY |
| After ANY STFTProcessor changes | `stft-validator` | MANDATORY |
| Build failures or plugin loading issues | `juce-build-doctor` | MANDATORY |
| Parameter not affecting audio | `parameter-auditor` | MANDATORY |
| DAW timing/latency issues | `latency-analyzer` | MANDATORY |
| Removing legacy code | `legacy-cleaner` | MANDATORY |
| After completing code changes | `code-reviewer` | MANDATORY |
| HPSS separation quality issues | `hpss-tuner` | Recommended |
| Accessibility implementation | `accessibility-pro` | Recommended |
| CI/CD improvements | `ci-enhancer` | Recommended |
| Test suite creation | `test-generator` | Recommended |
| Audio functionality QA | `audio-qa-tester` | Recommended |
| Visual polish and animations | `ui-polisher` | Optional |
| UX flow simplification | `ux-optimizer` | Optional |
| Architecture redesign | `system-architect` | Optional |
| Code cleanup/refactoring | `code-refactorer` | Optional |
| iZotope RX guidance | `izotope-rx-expert` | Optional |

### G.3 Mandatory Agent Usage

The following agents MUST be invoked when conditions are met:

| Condition | Agent | Failure to Invoke = |
|-----------|-------|---------------------|
| Any STFTProcessor.cpp modification | `stft-validator` | Task incomplete |
| Audio output sounds wrong | `dsp-debugger` | Insufficient diagnosis |
| Build fails | `juce-build-doctor` | Unresolved blocker |
| Parameter doesn't work | `parameter-auditor` | Unresolved bug |
| Deleting legacy files | `legacy-cleaner` | Risk of breaking build |
| Code changes complete | `code-reviewer` | Quality gate skipped |

### G.4 Agent Misuse Prohibitions

| Prohibited Action | Correct Action |
|-------------------|----------------|
| Using `ui-polisher` for accessibility | Use `accessibility-pro` |
| Using `code-refactorer` for architecture | Use `system-architect` |
| Skipping `stft-validator` after STFT changes | ALWAYS run validator |
| Manual legacy deletion without agent | Use `legacy-cleaner` |
| Diagnosing DSP issues without agent | Use `dsp-debugger` |

### G.5 Agent Escalation Rules

If an agent cannot resolve an issue:
1. Document the agent's findings
2. Identify if a different agent is appropriate
3. If no agent applies, escalate to user with specific questions
4. NEVER silently abandon unresolved issues

---

## H. Agent Reference Index

### H.1 Project-Specific Agents (Unravel)

| Agent | Model | Primary Use |
|-------|-------|-------------|
| `dsp-debugger` | opus | Diagnose gain, clipping, STFT issues |
| `stft-validator` | sonnet | Verify perfect reconstruction |
| `juce-build-doctor` | sonnet | Fix build and plugin loading |
| `parameter-auditor` | sonnet | Trace parameter flow |
| `latency-analyzer` | sonnet | Verify latency compensation |
| `legacy-cleaner` | haiku | Safely remove unused code |
| `hpss-tuner` | sonnet | Optimize separation parameters |
| `ci-enhancer` | sonnet | Improve CI/CD pipeline |

### H.2 General-Purpose Agents

| Agent | Model | Primary Use |
|-------|-------|-------------|
| `code-reviewer` | opus | Post-implementation code review |
| `test-generator` | opus | Create comprehensive test suites |
| `audio-qa-tester` | opus | QA testing of audio functionality |
| `accessibility-pro` | opus | Accessibility implementation |
| `ui-polisher` | opus | Visual polish and animations |
| `ux-optimizer` | opus | Simplify user flows |
| `system-architect` | opus | Architecture redesign |
| `code-refactorer` | opus | Code cleanup and optimization |
| `izotope-rx-expert` | opus | iZotope RX software guidance |

### H.3 Quick Selection Guide

```
Audio sounds wrong?          → dsp-debugger
Changed STFT code?           → stft-validator
Build failing?               → juce-build-doctor
Parameter not working?       → parameter-auditor
Timing/sync issues?          → latency-analyzer
Removing old code?           → legacy-cleaner
Separation quality?          → hpss-tuner
Finished writing code?       → code-reviewer
Need tests?                  → test-generator
Accessibility work?          → accessibility-pro
```

---

## I. Common Issues and Escalation

### I.1 Issue-Solution Matrix

| Issue | First Action | Agent if Unresolved |
|-------|--------------|---------------------|
| Build Errors | Check JUCE submodule, update | `juce-build-doctor` |
| Plugin Not Loading | Verify code signing, restart DAW | `juce-build-doctor` |
| Dead UI Control | Check APVTS attachment and ID | `parameter-auditor` |
| Audio Glitches | Remove allocations from processBlock | `dsp-debugger` |
| UI Not Updating | Verify AsyncUpdater/Timer | - |
| Parameter Not Working | Verify ID consistency | `parameter-auditor` |
| DAW Crash | Run debugger, check null pointers | `juce-build-doctor` |
| High CPU Usage | Profile, optimize DSP | `dsp-debugger` |
| Gain/Level Issues | Check STFT scaling, FFT format | `dsp-debugger` |
| Reconstruction Error | Check COLA, window scaling | `stft-validator` |

### I.2 Escalation Triggers

Escalate to user immediately if:
- Multiple agents fail to resolve issue
- Fix requires architectural decision
- Security or data integrity at risk
- Requirements are ambiguous or conflicting
- Change would violate rules in this document

---

## J. Definitions

| Term | Definition |
|------|------------|
| APVTS | AudioProcessorValueTreeState - JUCE parameter management |
| COLA | Constant Overlap-Add - window scaling for perfect reconstruction |
| FFT | Fast Fourier Transform |
| HPSS | Harmonic-Percussive Source Separation |
| STFT | Short-Time Fourier Transform |
| processBlock | Audio callback - real-time thread |
| prepareToPlay | Initialization callback - non-real-time |
| pluginval | Industry-standard plugin validation tool |
| Agent | Specialized AI tool for specific task domain |
| MANDATORY | Must be done - no exceptions |
| Recommended | Should be done unless clear reason not to |
| Optional | Use when beneficial |

---

**END OF BINDING GUIDELINES**

These rules are now in effect. Agent usage is enforced. Quality gates are mandatory.
