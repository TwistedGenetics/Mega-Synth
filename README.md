# Mega Synth

A VST3 / Audio Unit port of the **Mega Browser Synth** by Twisted Genetics: six oscillators, an FM and ring-mod matrix, 17 filter flavours, three LFOs and three mod envelopes with a 6-slot assignment matrix, tape delay, 90s reverb + shimmer, Juno chorus, a reverse pitch reverb and a 32-step acid sequencer.

The DSP follows the browser version closely: same oscillator shapes, the same Web Audio filter stages and formulas, the same envelopes, modulation scaling and effect routing. Patches move both ways between the two.

## Download (Mac)

Every push builds the plugin on GitHub's Mac machines.

- **Releases**: every successful build on `main` is published to the **Releases** section on the right of the repo page. Download `MegaSynth-macOS.zip` from the newest one.
- **Latest build**: **Actions** tab → newest "Build plugin" run → `MegaSynth-macOS` under *Artifacts* (you need to be signed in to GitHub).

The Mac build is a universal binary (Apple Silicon and Intel) with VST3, AU and a standalone app. Installation steps are in [INSTALL-mac.txt](INSTALL-mac.txt) and inside the zip. The short version:

```
# after copying the plugins into ~/Library/Audio/Plug-Ins/VST3 and .../Components
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/"Mega Synth.vst3"
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/"Mega Synth.component"
```

That Terminal step is needed because the build isn't signed with a paid Apple Developer ID. A Windows VST3 is built too (`MegaSynth-Windows.zip`).

## Using it

- **Tabs**: Oscillators (Osc 1-3, Sub, Wavetable 1 and 2, Complex, SuperSaw), Mixer & Routing (levels, FX returns, FM and ring matrices), Filter & Env, Effects, Modulation (LFOs, mod envelopes, assignments) and Sequencer.
- **Wavetable 1 and 2** each play a WAV (or AIFF/FLAC) loaded with their own *Load Sample* button. Wavetable 1 is the browser's Osc 4 and can be used in the FM and ring matrices; Wavetable 2 is new, plays alongside it, and can be modulated (level, detune, scan, loop points). Samples are saved inside your DAW project and in patch files.
- **Keyboard**: click the on-screen keys. Inside a DAW the plugin never takes the computer keyboard, so clicking its controls doesn't interrupt your DAW's QWERTY keyboard (in Ableton press **M** for Computer MIDI Keyboard). The standalone app plays from A W S E D F T G Y H U J K with Z / X for octave, like the browser version.
- **MIDI**: notes, pitch bend (range on the header), mod wheel → LFO 1 depth, CC7 → master volume. Aftertouch, poly aftertouch, MPE and any two CCs are available as modulation sources.
- **Sequencer**: tick *Run Sequencer*. *Clock* chooses its own tempo knob or the DAW transport (steps lock to the DAW's 16ths and run while the DAW plays). *Random Phrase* uses the generator mode, scale and length.
- **Patches**: *Save Patch* writes a `.megasynth` file to `Music/Mega Synth/Patches` (subfolders work too). The patch menu lists everything in that folder, `<` / `>` step through them, and the menu can also open a patch file from anywhere. A patch includes the sequence and the Osc 4 sample.
- **Copy Patch / Paste Patch** use the same JSON as the browser's Save Patch / Load Patch, including the wavetable sample.
- Every control is automatable. Double-click a knob to reset it.
- **Undo / Redo / Original** (header): every knob move, step edit, route change, patch load and sample load can be undone. *Original* goes back to the patch as it was loaded.

## Mutation tab

Three per-note modules sit between the oscillator mix and the filter, in this order. Each has its own mix, and at 0 it's switched out completely.

- **DNA Splice**: builds a new wave from two of the note's own sources (A and B: Osc 1-3, Sub, WT 1/2, Complex, SuperSaw) in one of seven ways: Waveform (A for part of each cycle, B for the rest), Crossover (A below a frequency, B above), Harmonic (every Nth harmonic from B), Spectral (B shaped by A's spectrum), Transient / Body (A's attack, B's sustain), Amplitude DNA (B following A's loudness) and Morph / Gene Shuffle. Changing mode or source mid-note crossfades.
- **Wave Mutation**: Drive, Bend, Asymmetry, Fold (wavefolding), Shape (saturation) and Rectify, run at twice the sample rate to keep aliasing down, then Bit Depth and Rate Reduce for deliberate digital grit.
- **Audio-Rate Transform**: a modulator (an internal sine at a ratio of the note plus an offset in Hz, one of the note's own oscillators, or noise) drives through-zero **FM** of the chosen oscillators, **AM**, **Ring** modulation and a **Frequency Shifter** (moves every partial by the same number of Hz, up or down).

## Modulation matrix (Mod Matrix tab)

Any source can drive any sound parameter: 32 routes, each with its own depth.

- **Sources**: LFO 1-4, amp / filter / mod envelopes 1-3, velocity, note number, key tracking, aftertouch, poly aftertouch, mod wheel, pitch bend, two assignable MIDI CCs (CC A / CC B), MPE pressure / slide / glide, random per note, smooth random, stepped random, sample & hold (of LFO 1), chaos, slow drift, note gate, note-on and note-off pulses, envelope / audio / transient followers, and the raw outputs of Osc 1, 2, 3 and Sub at audio rate.
- **Destinations**: every continuous parameter in the synth (151 of them), picked from a menu grouped by module or found with the search box. Depth is a share of the destination's full travel, so +25% means the same distance on a cutoff knob and on a level knob.
- **Per route**: on/off, curve (linear, exponential, logarithmic, S-curve, inverted, rectified, quantized, smooth, stepped), polarity (+/- swings around the knob, + only pushes up), a *via* source that scales the depth (e.g. mod wheel opens LFO vibrato) and smoothing.
- **Routes can modulate other routes' depths** (pick *Mod Matrix: Mod N Amount* as the destination).
- **Right-click any knob** to modulate it or to remove/find its routes. A modulated knob shows a cyan ring for the range the routes can reach and a white dot for the value the newest note is hearing.
- **Audio rate**: Osc 1/2/3/Sub outputs can drive oscillator pitch (FM), levels (AM), filter cutoff and resonance, FM depths, ring mix and the complex oscillator's FM and fold, sample by sample. They can't drive other destinations; the matrix marks those routes inactive.
- Envelope times are read when a note starts, so routes onto them use the note-on values (velocity → attack works; an LFO onto attack takes its value at note-on). Effects are shared by all notes, so routes onto effect parameters follow the newest note.
- **Macros** (Macros & Scenes tab): 8 macro knobs you can rename. Right-click any knob > *Assign to macro* to add it (each assignment is a matrix route, so its depth, curve and direction can be changed there). Macros are also sources and destinations in the matrix, so the mod wheel or an LFO can drive a macro that drives ten knobs.
- **Scenes A-D**: *Store A-D* saves the whole sound (oscillators, filter, envelopes, LFOs, effects; not the routes, macros, volume or sequencer). Turn on *Morph* and drag the XY pad to blend them: knobs blend smoothly, switches such as waveform and filter type take the nearest scene's setting. With Morph on, the A-D buttons choose which scene the panel shows and edits; with it off they recall a scene. Scene X and Y are normal parameters, so they can be automated, put on a macro, or modulated per note from the matrix.
- The original LFO / envelope slots on the Modulation tab still work exactly as before, alongside the matrix. Patches made before the matrix load with it empty and sound the same.

## Differences from the browser version

Most behaviour is unchanged. These are the places where the browser version had a bug or couldn't do what a control promised:

- **Ladder** and **Minimoog - Fat Cat** filters are now true 4-pole resonant ladders. In the browser they're built from a Web Audio feedback loop with no delay in it, which Web Audio can't run as intended.
- **TB-303** is a 4-pole ladder with its resonance feedback high-passed at about 150 Hz, like the real 303, so resonance fades as the cutoff drops and the filter closes right down. **Acid Filter Approx** keeps the browser's two resonant stages and drive, with resonance easing off below ~500 Hz so it doesn't boom when closed. Both have a soft limiter at extreme resonance.
- **SuperSaw Level** and **Stereo Width** now work. In the browser the SuperSaw always played at full level, panned centre. Because of this the default patch has less SuperSaw than the browser's default.
- **Complex Level** follows the knob live and can be modulated. The browser only read it at note-on.
- Modulation targets that did nothing in the browser now work: Ring Gain, Tape Flutter, Shimmer Brightness, Chorus Rate/Depth, Reverse Time/Pitch. *Reverb Size* still isn't modulatable.
- **Portamento** glides from the previous note. In the browser every new note slid up from 440 Hz.
- **Delay Sync** stays in effect while notes play. The browser switched back to the manual delay time whenever a note was held.
- Tempo-synced effects follow the DAW tempo.
- The reverb, shimmer and reverse reverb share one impulse, so they run through one convolution with their returns applied before it. The sound only differs if you modulate their return levels quickly.
- **Analog Warmth** (Filter & Env tab, new): *Warmth* swaps the hard clip at the filter input for a softer saturation with a little 2nd harmonic, and adds a gentle low lift and softer top on the master. *Bass Keep* holds onto low end in filter types with high-pass or band-pass stages (Steiner-Parker, State Variable, MS-20, Oberheim, SEM, CS-15...). *Drift* lets each oscillator wander slightly and start free-running. They default to 0.5 / 0.5 / 0.3; set all three to 0 for the browser's exact sound.
- **New**: semitone dials on every oscillator, velocity sensitivity (off by default, as before), pitch-bend range, the DAW-synced sequencer clock, and samples saved with the project.

## Building from source

Requires CMake 3.22+ and a C++17 compiler (Xcode on Mac). JUCE 8 is downloaded automatically.

```
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build --config Release
```

Plugins land in `build/MegaSynth_artefacts/Release/`. `-DMEGASYNTH_TESTS=ON` adds an offline render test (`MegaSynthTest`) that plays every filter mode, the sequencer, the sample oscillator and the patch import, and checks the output.

## Licence

The plugin is built with [JUCE](https://juce.com), used under its AGPLv3 / JUCE licence terms. If you plan to sell or distribute it closed-source, check JUCE's licensing first.
