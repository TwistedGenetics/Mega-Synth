# Mega Synth

A VST3 / Audio Unit port of the **Mega Browser Synth** by Twisted Genetics: six oscillators, an FM and ring-mod matrix, 17 filter flavours, three LFOs and three mod envelopes with a 6-slot assignment matrix, tape delay, 90s reverb + shimmer, Juno chorus, a reverse pitch reverb and a 32-step acid sequencer.

The DSP follows the browser version closely: same oscillator shapes, the same Web Audio filter stages and formulas, the same envelopes, modulation scaling and effect routing. Patches move both ways between the two.

## Download (Mac)

Every push builds the plugin on GitHub's Mac machines.

- **Releases** (tagged versions): the **Releases** section on the right of the repo page. Download `MegaSynth-macOS.zip`.
- **Latest build**: **Actions** tab → newest "Build plugin" run → `MegaSynth-macOS` under *Artifacts* (you need to be signed in to GitHub).

The Mac build is a universal binary (Apple Silicon and Intel) with VST3, AU and a standalone app. Installation steps are in [INSTALL-mac.txt](INSTALL-mac.txt) and inside the zip. The short version:

```
# after copying the plugins into ~/Library/Audio/Plug-Ins/VST3 and .../Components
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/"Mega Synth.vst3"
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/"Mega Synth.component"
```

That Terminal step is needed because the build isn't signed with a paid Apple Developer ID. A Windows VST3 is built too (`MegaSynth-Windows.zip`).

## Using it

- **Tabs**: Oscillators, Osc 4 Sample, Mixer & Routing (levels, FX returns, FM and ring matrices), Filter & Env, Effects, Modulation (LFOs, mod envelopes, assignments) and Sequencer.
- **Osc 4** plays a WAV (or AIFF/FLAC) you load with *Load Sample*. The sample is saved inside your DAW project.
- **Keyboard**: click the on-screen keys, or use A W S E D F T G Y H U J K with Z / X for octave, like the browser version (some DAWs keep computer-key presses for themselves).
- **MIDI**: notes, pitch bend (range on the header), mod wheel → LFO 1 depth, CC7 → master volume.
- **Sequencer**: tick *Run Sequencer*. *Clock* chooses its own tempo knob or the DAW transport (steps lock to the DAW's 16ths and run while the DAW plays). *Random Phrase* uses the generator mode, scale and length.
- **Copy Patch / Paste Patch** use the same JSON as the browser's Save Patch / Load Patch, including the wavetable sample.
- Every control is automatable. Double-click a knob to reset it.

## Differences from the browser version

Most behaviour is unchanged. These are the places where the browser version had a bug or couldn't do what a control promised:

- **Ladder** and **Minimoog - Fat Cat** filters are now true 4-pole resonant ladders. In the browser they're built from a Web Audio feedback loop with no delay in it, which Web Audio can't run as intended.
- **TB-303** and **Acid Filter Approx** keep their two resonant stages and drive. The positive feedback loop is gone because it latches up above low resonance, and a soft limiter catches the +30 dB resonance peak at the top of the range.
- **SuperSaw Level** and **Stereo Width** now work. In the browser the SuperSaw always played at full level, panned centre. Because of this the default patch has less SuperSaw than the browser's default.
- **Complex Level** follows the knob live and can be modulated. The browser only read it at note-on.
- Modulation targets that did nothing in the browser now work: Ring Gain, Tape Flutter, Shimmer Brightness, Chorus Rate/Depth, Reverse Time/Pitch. *Reverb Size* still isn't modulatable.
- **Portamento** glides from the previous note. In the browser every new note slid up from 440 Hz.
- **Delay Sync** stays in effect while notes play. The browser switched back to the manual delay time whenever a note was held.
- Tempo-synced effects follow the DAW tempo.
- The reverb, shimmer and reverse reverb share one impulse, so they run through one convolution with their returns applied before it. The sound only differs if you modulate their return levels quickly.
- **New**: velocity sensitivity (off by default, as before), pitch-bend range, the DAW-synced sequencer clock, and samples saved with the project.

## Building from source

Requires CMake 3.22+ and a C++17 compiler (Xcode on Mac). JUCE 8 is downloaded automatically.

```
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build --config Release
```

Plugins land in `build/MegaSynth_artefacts/Release/`. `-DMEGASYNTH_TESTS=ON` adds an offline render test (`MegaSynthTest`) that plays every filter mode, the sequencer, the sample oscillator and the patch import, and checks the output.

## Licence

The plugin is built with [JUCE](https://juce.com), used under its AGPLv3 / JUCE licence terms. If you plan to sell or distribute it closed-source, check JUCE's licensing first.
