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

- **Tabs**, in the order the sound flows (see *Where things are* below): **Perform**, **Overview**, **Oscillators**, **Mixer**, **Filters**, **Envelopes**, **FX & Mutation**, **Modulation**, **Sequencers** and **Lab**. Most tabs have sub-tabs along the top.
- **Wavetable 1 and 2** each play a WAV (or AIFF/FLAC) loaded with their own *Load Sample* button. Wavetable 1 is the browser's Osc 4 and can be used in the FM and ring matrices; Wavetable 2 is new, plays alongside it, and can be modulated (level, detune, scan, loop points). Samples are saved inside your DAW project and in patch files.
- **Keyboard**: click the on-screen keys. Inside a DAW the plugin never takes the computer keyboard, so clicking its controls doesn't interrupt your DAW's QWERTY keyboard (in Ableton press **M** for Computer MIDI Keyboard). The standalone app plays from A W S E D F T G Y H U J K with Z / X for octave, like the browser version.
- **MIDI**: notes, pitch bend (range on the header; changing it also bends notes that are already bent), mod wheel, sustain pedal (CC64; switch off with *Sustain Pedal* if your pedal is meant for something else), CC7 → master volume. Aftertouch, poly aftertouch, MPE and any two CCs are available as modulation sources.
  - **Mod wheel**: always a Mod Matrix source. As in the browser synth it also sets LFO 1 Depth (*Mod Wheel to LFO 1 Depth*, on for older patches). That only makes a sound if LFO 1 goes somewhere, so Init switches it off and routes the wheel to vibrato instead.
  - **MIDI IN panel** (right of the keyboard): live pitch bend (in semitones), mod wheel and what it drives, sustain pedal, the last controller and its channel, and how many bend / CC messages have arrived. If the bars don't move when you move the wheels, your DAW isn't sending them to the plug-in.
- **Init** (header): a clean starting point. One saw on Osc 1, filter open, plain amp envelope, no effects, no mutation, sequencers or arp; the mod wheel adds vibrato. New instances start from it (a project you open always comes back exactly as saved). *Save current patch as Init* makes the current sound your own Init, used by the Init button, new instances and the browser's *Init* entry; *Reset Init to factory* goes back to the clean one. *Reset every control to its default* is the old Init.
- **Sequencer**: tick *Run Sequencer*. *Clock* chooses its own tempo knob or the DAW transport (steps lock to the DAW's 16ths and run while the DAW plays). *Random Phrase* uses the generator mode, scale and length.
- **Perform tab** (first tab): the 8 macros, the scene XY pad, Master Mutate (amount and seed), the DNA Sequencer's on / pattern / swing / density / depth, the main filter and effect knobs and the Beat Repeat pad, all on one page for playing live.
- **MIDI learn**: right-click any knob, switch or menu, choose *MIDI Learn*, then move a knob or fader on your controller (the MIDI IN panel shows what is waiting to be learned). The knob shows its CC number; right-click again to *Forget MIDI*. Moving a CC that's already used moves it to the new knob. The MIDI map is saved with your DAW project (not in patches), so changing patches keeps your controller layout. CC 120-127 (channel mode messages) can't be learned.
- **Window size**: drag the bottom-right corner, or use *Size* in the header (75 / 90 / 100 / 125 / 150%). The size is saved with the project.
- **A / B compare** (header): both slots start as the loaded patch. Edit one, switch to the other to hear the difference; *Copy* copies the slot you're playing to the other one. Switching can be undone; loading a patch resets A/B.
- **Browse** (header): search (every word must match the name, category or description), categories (factory categories, *Your patches* and each subfolder of your patch folder), favourites (click the star; stored in `favourites.txt` in the patch folder) and *Load as you browse* so the arrow keys audition patches. Enter or double-click closes, Esc closes.
- **Host presets**: in AU hosts (Logic's preset menu) and the standalone app, the factory presets are also listed as programs. VST3 shows a single program, because VST3 makes programs an automatable parameter that could load a preset over your settings; use *Browse* instead. A MIDI Program Change (0-27) loads that factory preset in every format.
- **Patches**: *Save* writes a `.megasynth` file to `Music/Mega Synth/Patches` (subfolders work too). The patch menu lists everything in that folder, `<` / `>` step through them, and the menu can also open a patch file from anywhere. A patch includes the sequence and the Osc 4 sample.
- **Copy Patch / Paste Patch** use the same JSON as the browser's Save Patch / Load Patch, including the wavetable sample.
- Every control is automatable. Double-click a knob to reset it.
- **Undo / Redo / Original** (header): every knob move, step edit, route change, patch load and sample load can be undone. *Original* goes back to the patch as it was loaded.

## Where things are

The tabs follow the signal: the oscillators, the mix, the filters, the envelopes, the mutation stages and effects, then what moves it all (modulation and sequencers) and the sound-design tools (Lab). Clicking a block in the Overview's signal flow opens its page.

| Tab | Sub-tabs |
|---|---|
| Perform | Perform, Performance FX |
| Overview | (one page) |
| Oscillators | Osc 1-3 & Sub, Wavetables, Complex & SuperSaw, Unison, FM & Ring |
| Mixer | (one page) |
| Filters | (one page) |
| Envelopes | Amp & Filter, Mod Envelopes, Pitch Envelope, MSEG 1 & 2 |
| FX & Mutation | Note Mutation, Bus Mutation, Space, Dirt, Motion, Rack |
| Modulation | LFOs, Mod Matrix, Assignments, Macros & Scenes, Cell Instability |
| Sequencers | Step Sequencer, Arp / Chord, DNA Sequencer |
| Lab | Mutate / Lab, Capture |

Moved from earlier versions:

| Was | Now |
|---|---|
| Oscillators (one page) | Oscillators > Osc 1-3 & Sub / Wavetables / Complex & SuperSaw |
| Unison | Oscillators > Unison |
| Mixer: FM and Ring matrices, FM 2>1 | Oscillators > FM & Ring |
| Filter & Env: filters | Filters |
| Filter & Env: Amp and Filter Envelopes | Envelopes > Amp & Filter |
| Filter & Env: Analog Warmth | Mixer > Analog |
| Modulation: LFO 1-3 Wave / Rate / Depth | Modulation > LFOs (they were also there already) |
| Modulation: Mod Envelopes 1-3 | Envelopes > Mod Envelopes |
| Modulation: Assignments | Modulation > Assignments (with the MIDI controller switches) |
| Modulation: Cell Instability | Modulation > Cell Instability |
| Env+ | Envelopes > Pitch Envelope and Envelopes > MSEG 1 & 2 |
| LFOs, Mod Matrix | Modulation > LFOs, Modulation > Mod Matrix |
| Macros/Scenes | Modulation > Macros & Scenes |
| Mutation | FX & Mutation > Note Mutation |
| Bus Mutation | FX & Mutation > Bus Mutation |
| Effects: Space / Dirt / Motion / Rack | FX & Mutation > Space / Dirt / Motion / Rack |
| Sequencer, Arp / Chord, DNA Seq | Sequencers > Step Sequencer / Arp / Chord / DNA Sequencer |
| Mutate / Lab, Capture | Lab > Mutate / Lab, Lab > Capture |

## Effects (FX & Mutation > Space / Dirt / Motion / Rack)

- **Space**: Tape / BBD Delay, 90s Reverb + Shimmer, Juno Chorus and the Reverse Reverb (as before), plus **Stereo Tools**: Bass Mono below 50-300 Hz, Width 0-200% and a Haas delay (0-30 ms) on the top end. It works in mid/side and never touches the mid, so the mono sum is exactly unchanged.
- **Dirt**:
  - **Vintage Sampler**: SP-1200 (12-bit, 26.04 kHz, no anti-alias filter: it aliases), Akai S950 (12-bit at Rate, a steep anti-alias filter and the resonant Filter), E-mu (12-bit, 27.8 kHz, gentle filters) or Free (Bits, Rate, Anti-Alias), with Noise (hiss that follows the signal, so a silent track stays silent) and an input clip. Quality High oversamples the clip.
  - **Multiband** (OTT style): three bands with adjustable crossovers, upward (up to 4:1 below -36 dB) and downward (up to 20:1 above -18 dB) compression per band, input and output gain per band, Depth, Time, Output and gain meters. The bands are split so that with nothing compressing they add back to exactly the input. Quality Eco updates the gains every 8 samples.
- **Motion**:
  - **Beat Repeat**: records the 1/4-1/32 starting at the trigger (you hear it live the first time), then repeats it (tempo synced). Shrink shortens each repeat (rolls), Pitch Drop lowers each one (tape-stop style), Reverse, Gate. Triggers: hold the pad (or automate Repeat Trigger), a MIDI note (that note doesn't play), Chance per beat (decided on the beat line), or each DNA Sequencer step. Duration sets how long a Chance / DNA repeat lasts. Short fades at every edge.
  - **Flanger / Phaser**: flanger 0.3-10 ms with through-zero and +/- feedback; phaser 4 / 8 / 12 stages; free or synced rate, stereo spread, and an envelope mode where the sweep follows the input level.
  - **Volume Shaper**: a drawn volume curve over 1/4, 1/2 or 1 bar, locked to the DAW's beat position, or restarted by each note or each DNA step, with presets (Pump, Gate, 1/8 Pump, Triplet), Depth and Smooth.
- **Rack**: every effect is a slot with On and Mix (Return for the send effects); drag rows to change the order. Tape Delay, Juno Chorus and the Reverbs are send effects: next to each other they all hear the same signal, exactly as before; with another effect between them each one hears what comes before it. Moving a slot dips the output for a few milliseconds instead of clicking. The order and the shaper curve are saved with patches and projects, and moves can be undone.
- All the new knobs can be automated and are Mod Matrix destinations. Synced settings follow the DAW's tempo and position (the synth's tempo when it's stopped). The effects add no latency.
- Patches and projects from before the rack load with every new effect off, the original effects on, and the original order.

## Filter (Filters tab)

- **Type**: Low Pass, High Pass, Band Pass, Notch, Peak / Bell, All Pass.
- **Slope**: 6, 12, 18 or 24 dB per octave for Low Pass, High Pass and All Pass; 12 or 24 for Band Pass and Notch; Peak / Bell has none (unavailable slopes are greyed out).
- **Model**: the 17 classic models. A model set to Low Pass at its own slope (picked automatically when you choose the model) is the original circuit, exactly as before. With any other type or slope the model gives the multimode filter its character: its input stage, how strong its resonance is, how hard the resonance saturates, and its output shaping.
- **Cutoff** 20 Hz - 20 kHz with equal knob travel per octave, shown in Hz below 1 kHz and kHz above (you can type either). It's kept below 45% of the sample rate.
- **Resonance**: strong peaks for Low and High Pass; the 24 dB Low Pass self-oscillates at the top of the knob, in tune with the cutoff. For Peak / Bell it sets the boost (+2 to +18 dB) and narrows it. Extreme settings stay stable and bounded.
- **Drive**: at 0 dB the filter is clean: it only filters. Turning Drive up blends in saturation (fully in by +12 dB), including the extra grit of the OTA, Acid and Polivoks models, with the level compensated so it adds density and bite rather than just volume. Only extreme peaks (far above full scale, or a screaming resonance) are rounded off at 0 dB.
- **Mix**: 0% = unfiltered, 100% = fully filtered, parallel in between.
- **Env Amt** -100% to +100% (100% = +10 kHz at the envelope's peak; negative sweeps down), **LFO Amt** -100% to +100% of any of the four LFOs (100% = +-4 octaves), **Key Track** 0-100% (100% = the cutoff follows the keyboard from middle C).
- The curve on the right is the real filter's response, worked out from the same code the voices run.
- Every filter control except the type, slope and model buttons can be a Mod Matrix destination (Cutoff and Resonance at audio rate too). Changing type, slope or model while a note plays crossfades over about 6 ms, so it doesn't click.
- Patches and projects from before this version load as they were: the classic model at its own slope, Low Pass, fully wet, no key tracking or filter LFO.
- **Filter 2**: a second, identical filter. **Filter 1 | Filter 2** in the panel header picks which one the controls show; **Filter 2 On** switches it in (off, it costs nothing and the sound is Filter 1 alone). **Serial** runs Filter 1 then Filter 2; **Parallel** feeds both the same sound and adds them, with **Balance** going from Filter 1 only, through both at full level in the middle, to Filter 2 only. **Stereo Split** (parallel only) puts Filter 1 on the left and Filter 2 on the right. **Copy 1 > 2** gives Filter 2 all of Filter 1's settings. Both filters share the Filter Envelope but each has its own Env Amt, LFO Amt and Key Track. Routing changes crossfade, the response curve shows both filters and the combined result, all of Filter 2's knobs and Balance are Mod Matrix destinations (cutoff and resonance at audio rate too), and the DNA Sequencer's Filter step, Cell Instability, Mutate and scenes cover both. Patches and projects from before Filter 2 load with it off.

## Filter character models (Filters tab: Filter 1, Filter 2 and the Master Filter)

Each filter has a **Character** menu next to its Model. *Off* uses the classic Model, Type and Slope exactly as before; choosing a character model replaces them (Type and Slope lock), and the **Character** knob does what the model needs. Ideas from Beep Street's Sunrizer, plus extras:

| Model | What it is | Character knob |
|---|---|---|
| Formant | three vowel resonances (male voice); Cutoff shifts them, 1 kHz = natural | vowel A E I O U |
| Formant 2 | a brighter, narrower set (female voice) | vowel |
| All Pass | two all-pass stages with the dry sound: moving notches | spread of the second stage |
| Power LP / Power BP | driven 24 dB filters that stay loud at high resonance | saturation |
| Wave Shaper | a wavefolder into a resonant low pass | fold amount |
| Resonator | a tuned comb / struck body at the cutoff (Key Track 100% plays it in tune); Resonance = brightness | decay |
| Resampler | sample-rate reduction as a filter, Cutoff = rate (no anti-aliasing) | bit depth |
| LP + Resample | resonant low pass, then sample-rate reduction | resample rate |
| Shifter | frequency shifter (up to +-2 kHz) into a resonant low pass | shift (centre = none) |
| Analogue LP | warm 4-pole ladder with drift and saturation in every stage | drift / heat |
| DJ Filter | one knob: low pass left, high pass right, flat in the middle (exactly the dry sound) | the DJ knob |
| Comb +/- | comb at the cutoff: left hollow (negative), right metallic (positive) | polarity and feedback |
| Vowel Morph | vowels that loop (A E I O U A), so an LFO cycles through them without a jump | vowel |
| Phaser Filter | 2-12 all-pass stages with feedback around the cutoff | stages |
| Ring Filter | a band pass ring-modulated at its own cutoff | ring amount |
| Decimator LP | a low pass whose resonance is bit-crushed inside the loop | crush |
| Dual Peak | two peaks, the second up to 3 octaves above the cutoff | second peak |

- Cutoff, Resonance, Drive, Mix, the envelope, LFO and Key Track work on every model (except where the table says a control isn't used). Switching models crossfades over 10 ms.
- The Character knob is a Mod Matrix destination (the voice reads it at control rate, every 16 samples). The models are chosen, not mutated: Mutate leaves them alone.
- The curve shows each model, measured from the real filter code. Resampler, LP + Resample, Shifter, Ring Filter and Decimator LP change the sound over time, so they show a note instead of a curve.
- CPU: 0.3-0.9% of one core per stereo voice per filter (Analogue LP is the heaviest), measured on our build machine.
- Older patches load with every Character set to Off.

## Mixer

A console, in the order the sound flows:

- **Sources**: a strip each for Osc 1, 2, 3, Sub, WT 1, WT 2, Complex, SuperSaw and the Ring modulator, with **Level**, **Pan**, **Voices** (unison; the SuperSaw's own voices; the ring modulator's wet / dry), **M**ute and **S**olo, a meter, and a **PITCH ENV** tag that lights when the pitch envelope moves that source.
  - Pan is a balance: centred, both sides stay at full level, so a centred mix sounds exactly as it did before the strips. Panning turns the other side down.
  - A muted source still works as an FM or ring modulator. With any Solo on, only soloed sources are heard. Mute and Solo ramp over a few milliseconds, so they don't click.
  - Pan is a Mod Matrix destination. The strips aren't changed by Mutate.
  - The strip meters show each source before the filter. They're only measured while the Mixer is on screen (about 0.2-1% of a core with 8 notes); the output meter costs next to nothing.
- **Engines**: the mutation stages (DNA Splice, Wave Mutation, Audio-Rate, Resonator, Granular, Spectral, Feedback), lit when they're doing something, with their Mix where they have one. Click one to open its page.
- **Effect Returns**: Delay, Reverb, Shimmer, Chorus, Reverse.
- **Analog**: Warmth, Bass Keep, Drift.
- **Output**: Master Volume (before the effects, as always), the Master Filter's On and Cutoff, the Output trim, and a stereo meter with the peak held.

## Performance FX (Perform > Performance FX)

Thirteen pads to play live on the whole mix. They sit after the effects rack and before the Master Filter and Output. With no pad engaged, and the echo and reverb tails gone, the stage doesn't touch the audio at all, so patches sound exactly as before.

- **Playing:** hold a pad to play it, and let go to stop. With **Latch** on, a click switches a pad on and the next click switches it off.
- **Quantise** (Off, 1/16, 1/8, 1/4, 1/2, 1 bar; default 1/16): a pressed pad waits for the next line of that grid, counted from the bar, then fades in over 4 ms. Letting go fades it out at once. The grid follows the DAW's position while it plays (a pad shows an amber outline while it waits), and the synth's own tempo otherwise.
- **Several pads at once** run in a fixed order, numbered on the pads:
  1. Time pads (the last one pressed wins): **Beat Repeat** (1/4, 1/8, 1/16, 1/32, 1/8T, 1/16T), **Reverse** (the last bar, backwards), **Tape Stop**, **Tape Start**, **Backspin**. Tape Time sets the stop, start and spin length in beats.
  2. **Pitch Down**: down by Pitch Amount (an octave by default), gliding over Glide, then gliding back up when released.
  3. **Stutter**: a hard gate at 1/16, 1/32 or 1/64.
  4. **Trance Gate**: 8 sixteen-step patterns, with a Depth control.
  5. **DJ Filter**: one knob, low pass to the left and high pass to the right, with Res.
  6. **Bitcrush**: Bits and Rate.
  7. **Vinyl**: wow, crackle, hiss and a duller top.
  8. **Echo Throw**: what you play while holding goes into a synced ping-pong delay (1/16 to 1/2) that keeps ringing after you let go.
  9. **Reverb Throw**: the same, into a reverb. With **Freeze** on, holding it fills the reverb for a 1/4 note, then holds the tail still until you let go.
- **XY Pads (two, Kaoss-style):**
  - X and Y are Mod Matrix sources ("XY Pad 1 X" and so on), so each axis can drive any Performance FX setting or any Mod Matrix destination.
  - **Assign X / Assign Y** adds the route; change its depth in the Mod Matrix. The XY sources are also in every knob's "Modulate with" menu.
  - **Spring:** the pad glides back to the centre when you let go. Switch it off and the pad stays where you leave it.
- **MIDI:**
  - **MIDI Keys** plays the 13 pads from 13 keys starting at C1, C2, C3, C4, C5 or C6. The key name is shown on each pad. Those keys play pads, not notes.
  - Right-click a pad for MIDI Learn (CC 64 and above switches it on).
  - Every pad and setting is a real, automatable parameter.
- **What's saved:** everything is saved in patches, projects, undo and A/B. It isn't part of Mutate or the scenes. Older patches and projects load with every pad off.
- **CPU:** about 0.08% of one core when idle (it keeps 8 s of recent audio for Reverse and Backspin), 0.4% with one pad, and 5.5% with all 13 at once. It's the same at every Quality setting.
- **Approximations:**
  - The tape and backspin pads read the recent audio at a changing speed, with linear interpolation (fine for this; not a high-end resampler).
  - Pitch Down is a two-tap delay-line shifter (50 ms window), so it has a little of that classic "chorusy" pitch-shift sound rather than a clean formant-preserving shift.
  - The reverb is a small Freeverb-style network (4 combs and 2 all-passes per side).
  - Echo and reverb tails are cut once they fall below -100 dB.

## Master Filter and Output (Filters tab, and the Mixer's Output section)

- A stereo filter at the very end of the chain: everything the synth makes, every effect included, passes through it, then the **Output** level.
- **On** (off by default: the sound passes straight through, untouched), **Type** (Low Pass, High Pass, Band Pass, Notch), **Slope** (12 or 24 dB/oct), **Cutoff**, **Resonance**, **Drive** and **Mix** (dry / wet). Its response is drawn next to the controls.
- Cutoff, Resonance, Drive, Mix and Output are Mod Matrix destinations. Because the filter comes after all the notes are mixed, routes onto it follow the newest note's sources, or the mod wheel, macros, aftertouch, CCs and the sidechain when no note is held.
- **Output** (-24 to +12 dB, 0 dB = untouched) is the last thing before the plug-in's output. **Master Volume** stays where it has always been, before the effects, so older patches with drive or clipping effects sound exactly the same.
- Not part of Mutate or the scenes' Output level; saved in patches, projects, undo and A/B. Older patches load with the filter off and Output at 0 dB.
- CPU: nothing when off; about 0.7% of one core at 24 dB with Drive on.

## Note Mutation (FX & Mutation > Note Mutation)

Three per-note modules sit between the oscillator mix and the filter, in this order. Each has its own mix, and at 0 it's switched out completely.

- **DNA Splice**: builds a new wave from two of the note's own sources (A and B: Osc 1-3, Sub, WT 1/2, Complex, SuperSaw) in one of seven ways: Waveform (A for part of each cycle, B for the rest), Crossover (A below a frequency, B above), Harmonic (every Nth harmonic from B), Spectral (B shaped by A's spectrum), Transient / Body (A's attack, B's sustain), Amplitude DNA (B following A's loudness) and Morph / Gene Shuffle. Changing mode or source mid-note crossfades.
- **Wave Mutation**: Drive, Bend, Asymmetry, Fold (wavefolding), Shape (saturation) and Rectify, run at twice the sample rate to keep aliasing down, then Bit Depth and Rate Reduce for deliberate digital grit.
- **Audio-Rate Transform**: a modulator (an internal sine at a ratio of the note plus an offset in Hz, one of the note's own oscillators, or noise) drives through-zero **FM** of the chosen oscillators, **AM**, **Ring** modulation and a **Frequency Shifter** (moves every partial by the same number of Hz, up or down).

- **Resonator** (after the filter and amp envelope): up to 16 tuned modes following each note, with six tunings (Harmonic, Odd, Bar, Membrane, Plate, Bell), Pitch offset, Decay, Damping (higher modes die faster), Stiffness (stretches the upper modes like a stiff string), stereo Spread and Feedback. Short notes strike it and it rings on after the note ends; sustained sounds are levelled so they don't overload it. With Feedback it can sustain on its own, so after a note ends it gets three times its decay time, then fades out.

## Bus Mutation (FX & Mutation > Bus Mutation)

These run once on all notes together, before the effects (per-note versions would cost about 16 times the CPU). Their knobs still take Mod Matrix routes, using the newest note's sources.

- **Granular**: records the last 4 seconds of the synth and plays it back as up to 64 grains: Size, Density, Position (how far back), Jitter, Pitch, Pitch Jitter, Reverse chance, stereo Spread and Feedback (grains written back into the recording). **Freeze** holds the recording, so the cloud keeps going after the notes end. Mix 0 = bypassed.
- **Spectral**: an FFT (phase vocoder) stage with Blur, Shift (every partial moved by the same number of Hz), Scramble, Tilt, Formant (tone colour without pitch change), Freeze, Morph (towards the frozen spectrum) and Feedback, at FFT sizes 512-4096. While it's switched **On** the synth's output is one FFT frame late (11-85 ms at 48 kHz); the plugin reports this to the DAW, which compensates. Off = no latency and no processing.
- **Bus order**: Granular before Spectral, or the other way round.
- **Feedback Matrix**: feeds the outputs of Granular, Spectral, the tape delay and the final output back into the inputs of Granular, Spectral and the delay (12 paths, each with its own amount). Every path arrives *Time* later (10 ms to 1 s; never less than one audio block, so there are no instant loops), through a DC blocker, a *Tone* low-pass, a soft clip at the safety ceiling and its own limiter. *Safety* lowers how hard loops can drive. While any path is up, the final output passes a soft ceiling that never exceeds 0 dBFS; with every path at 0 it is completely out of the signal.

## Capture / Resample (Lab > Capture)

**Record** captures up to 8 seconds of the synth, either after the effects (exactly what you hear) or before them. Drag the markers on the waveform to trim (optionally snapped to zero crossings), add fades, reverse and normalise. The pitch is detected automatically. Then send it back in: to **WT 1 / WT 2** (the root note is set from the detected pitch, so it plays in tune), to **DNA A / DNA B** (loaded into a wavetable and chosen as that DNA Splice source), to **Granular** (becomes the frozen grain buffer), or cut a **single cycle** at the detected pitch into WT 1 / WT 2 as a looping wave. **Save WAV** exports it. The last capture is saved with the project and in patch files.

## Cell Instability (Modulation > Cell Instability)

Each note gets its own smooth wandering noise on a set of targets, so no two notes (or two moments of a held note) are quite the same. **Amount** scales everything (0 = off), **Rate** sets how fast it wanders. Per-target depths: Pitch (up to +/-50 cents), Cutoff, Resonance, Osc Levels (the balance between oscillators shifts; switched-off oscillators stay off), Wave Fold, WT Scan, FM (only where FM is already up), DNA Splice, Envelopes (attack/decay/release, set per note at note-on) and Resonator. The noise glides with a smooth curve, so it never steps or clicks. It's also a Mod Matrix source ("Cell Instability") for anything else.

## Master Mutate (Lab > Mutate / Lab)

**Mutate** adds a seeded variation on top of the patch without moving any knob: 0% is exactly your patch, and the same patch, seed and amount always give exactly the same sound. **New seed** picks a random seed, **<** / **>** step through neighbouring seeds. 13 **locks** (Pitch, Oscillators, Wavetables, Levels, Filter, Envelopes, Modulation, Effects, DNA Splice, Wave / Audio-rate, Resonator, Granular / Spectral, Feedback) keep a group exactly as it is. Offsets move together within a group and are shaped: short attacks stay short until extreme amounts, feedback tends downwards, FM / modulator ratios lean to harmonic values, pitch moves in whole semitones, anything switched off stays off, and waveforms switch only at higher amounts. **Commit** writes the current mutation into the knobs and sets Mutate back to 0 (undoable; committed mutations are listed and saved with the patch). Mutate is an ordinary parameter, so it can be automated, put on a macro, or driven per note from the Mod Matrix (e.g. velocity -> Mutate). The sequencer, master volume, macros and scene positions are never mutated.

## Genetic Lab (Lab > Mutate / Lab)

Make two sounds the parents (**Current sound > A**, then build or load another and **Current sound > B**) and press **Breed**: you get eight children. Each child takes every one of the 13 groups (the same groups as the Mutate locks) whole from parent A or parent B, so a child might have A's oscillators and envelopes with B's filter, effects and Mod Matrix routes; **Variation** adds a light mutation on top (0 = pure combinations). Click a child to hear it, make a good one a parent (**Child > Parent A/B**) and breed again. **<** / **>** step back and forward through the generations, the family tree is shown for the sound you're playing and saved with the project, and a patch saved from the lab records its lineage. Breeding mixes the sound parameters and routes; samples, the sequence, scenes and captures stay as they are.

## Sonic DNA Sequencer (Sequencers > DNA Sequencer)

Up to 32 steps, each a **transform** and an **amount**: Fold, Crush, Decimate (Wave Mutation), Shift, Ring, FM (Audio-Rate Transform), Splice (DNA Splice), Resonate (Resonator), Filter (closes both filters), Grain (Granular), Blur (Spectral, which must be On), Mutate (Master Mutate) and Octave (+12 semitones). The running step pushes its transform's destinations, **Glide** crossfades into the next step and **Depth** scales everything.

- **Sync**: *Host* follows the DAW's tempo and song position (press play mid-bar and it's on the right step); when the DAW is stopped it carries on at the tempo. *Internal* uses the synth's own tempo. *Free* runs at Free Rate in Hz. **Rate** is the step length (1/32 to 1 bar).
- **Restart**: *Never* (runs on; in Host sync it's locked to the song position), *Each note*, *Each bar* (follows the DAW's time signature), *Host start*. **Start Offset** begins the pattern up to 31 steps in.
- **Swing** 0-75% on 1/16 or 1/8: every second unit starts late by that share of a unit (33% is about a triplet shuffle); downbeats don't move.
- **Direction**: forward, reverse, ping-pong or random.
- **Per step**: probability (the bar under the amount), ratchet (x1-x4 repeats inside the step), glide (G = the global Glide, G+ = always, G- = never) and lock.
- **Lane 2** has its own steps and its own length (**Lane 2 Steps**) for polymeter, e.g. 16 against 12; both lanes add their transforms together.
- **Patterns A-D**, each with both lanes. **Chain** plays them in order (e.g. AABA), one lane-1 cycle each. **MIDI select** lets four notes pick the pattern from a clip (those notes don't sound).
- **Copy / Paste / << / >> / Reverse / Clear** work on the lane you're editing (Clear keeps locked steps).
- **Generator**: Random, Euclidean (Fill's hits spread evenly, Rotate moves them) or Variation (small changes to the current pattern), with Fill, Min / Max amount, Extras (chance of ratchets and probabilities) and the transforms it may use. **Generate** picks a new seed; **<** / **>** step through seeds, and the same seed always gives the same pattern. Locked steps are never changed. Every edit can be undone.
- **Density** thins the pattern and **Probability** scales every step's probability (0% = nothing plays, 100% = as written, 200% = everything plays); with Glide and Depth they can be automated or driven from the Mod Matrix. Probability decisions follow the song position, so a bounce plays the same way twice.
- **Mod Matrix sources**: DNA Sequencer (lane 1's step amount), DNA Sequencer Lane 2, and DNA Step Gate (a pulse at every step and ratchet hit).
- **Mod Matrix destinations**: Glide, Depth, Density and Probability, plus Swing, Start Offset, Free Rate and Lane 2 Steps. The clock settings (Swing, Start Offset, Free Rate) follow the modulation with one audio block of delay.
- Patches and projects from before this version load with these settings off and their steps in pattern A, and play exactly as they did.

## Overview

The Overview shows the whole synth at a glance: the **signal flow** (each note's chain, then the bus, with the stages that are switched in lit up, cyan badges for Mod Matrix routes into each stage and red arcs for Feedback Matrix paths; click a block to open its page), the **genetic network** (every active route as a line from its source to its destination, pulsing with the source's live value), a **spectrum** of the output, and the **DNA strand**: 13 base pairs for the 13 genetic groups, glowing with how hard the current seed and Mutate amount push each one, grey when locked. All of it is drawn from values the audio already publishes; the only addition to the audio path is copying the output into the analyser's buffer.

## Unison (Oscillators > Unison)

- Osc 1-3, both wavetables and the Complex oscillator can each stack up to 8 voices: the oscillator itself in the centre, plus detuned copies.
- Per oscillator:
  - **Voices** (1 = off, exactly as before)
  - **Detune** (cents, at the outermost copies)
  - **Spread** (how far the copies pan across the stereo field)
  - **Blend** (the copies' level against the centre; the total is normalised so the level stays about the same)
  - **Detune Curve** (Linear, or SuperSaw-style with the outer voices further out)
- The extra voices start at random phases on every note unless **Same phase every note** is on.
- FM, ring modulation and DNA Splice use the centre voice. The SuperSaw keeps its own unison.
- Each extra voice costs about as much CPU as the oscillator itself: around 0.05% of one core per note per extra voice on our test machine.
- Detune, Spread and Blend are Mod Matrix destinations.

## Sidechain input

Mega Synth has an optional stereo sidechain input, so a kick or break on another track can drive it. In Ableton Live (11 and later), open the device's sidechain section, or the routing chooser in its title bar, and pick the track. In Logic, use the plug-in window's Side Chain menu. With nothing routed, nothing changes, and the sidechain is never heard in the output.

- **Volume Shaper > Sidechain hits**: each hit on the sidechain restarts the shaper's curve, instead of its Trigger. So the drawn pump follows your actual kick, even an off-grid one. **Threshold**, **Attack** and **Release** set what counts as a hit. The meter shows the sidechain level, with the threshold marked.
- **Multiband > Sidechain**: the downward compression listens to the sidechain, split into the same three bands; the upward side still listens to the synth. Good for ducking a sub under a kick.
- **Sidechain Envelope** is a Mod Matrix source.
- In VST3 the input is declared as an aux (sidechain) bus, so hosts still treat Mega Synth as an instrument.
- Old patches and projects load with both sidechain options off.

## LFOs (Modulation > LFOs)

Each of the four LFOs has, on top of Wave, Rate and Depth:

- **Sync**: Off uses the Rate knob in Hz. Otherwise one cycle lasts a note length (1/32 to 4 bars, with dotted and triplet) at the DAW's tempo, or the synth's own tempo when the DAW is stopped. With Sync on, the Rate knob is greyed out and has no effect.
- **Retrigger**: *Each note* (every note starts its own LFO, as before), *Free* (one LFO shared by all notes; when synced it is locked to the song position, so a 1/4 wobble always lands on the beat), *Each bar* (restarts at every bar line, following the DAW's time signature) or *Host start* (counts from where you pressed play).
- **Phase** (where it starts), **Mode** (Loop, or One-shot: runs once from the retrigger point and holds at the end), **Smooth** (slew, up to half a second), **Fade In** (after each note starts) and **Unipolar** (0..1 instead of -1..1).
- **Custom** shape: draw it in the editor next to the knobs. Drag points, double-click to add one, right-click a point for Linear / Smooth / Step or to delete it, shift-drag a segment to bend it. Snap to 1/4, 1/8, 1/16 or triplets. *Presets* has saw down, saw up, square, triangle, wub, stairs and random steps; *Copy* / *Paste* move a shape between LFOs. Drawing turns Custom on.
- A live playhead shows where the newest note's LFO is.
- Phase, Smooth and Fade In are Mod Matrix destinations. The new settings drive every LFO destination: the Mod Matrix, the assignment slots and the filters' LFO amount.
- Patches and projects from before this version load with Sync off and Each note retrigger, and sound exactly as they did.

## Arp / Chord (Sequencers > Arp / Chord)

- **Chord Memory**: one key plays a whole chord. Choose Minor 7, Major 7, Minor 9, Sus 2, Sus 4, Power, Rave minor (an open 1 5 b3 b7 9) or Octaves, or press **Learn**, play a chord and let go: it's stored relative to its lowest note. **Spread** moves every other note up by octaves to open the voicing; **Inversion** moves the lowest notes up (or the highest down). It works in mono too: a chord always gets a voice per note.
- **Arpeggiator**:
  - Modes: Up, Down, Up / Down, Down / Up, As played, Random, Chord (all together).
  - Octaves 1-4, Rate 1/32 to 1/4 (with dotted and triplet), Swing, Gate, Latch, Velocity (as played or fixed), Repeats per note, and Restart (on the first note, or every bar).
  - Steps land on the song's grid at the DAW's tempo. A key pressed slightly late (within 15% of a step) plays straight away.
  - A 16-step pattern: click a step for On / Accent / Rest, drag up or down for its probability. *Pattern Steps* sets the length. A rest or a skipped step doesn't move on to the next note.
- Chord memory feeds the arpeggiator: hold one key and the arp plays the chord.
- Both play through the voices, so the DNA Sequencer (restart on each note), the Volume Shaper (each note) and Beat Repeat follow the arpeggiated notes.
- **Arp Position** and **Arp Step Gate** are Mod Matrix sources. Arp Swing and Gate are Mod Matrix destinations; they follow the modulation with one audio block of delay.
- Arp On, Latch, Rate, Mode, Gate and Chord are also on the Perform tab, along with the Pitch Envelope amount and decay and Osc 1's unison voices and detune.
- Only notes you play are arpeggiated, not the step sequencer's.
- Old patches and projects load with both off.

## Pitch envelope and MSEGs (Envelopes > Pitch Envelope, MSEG 1 & 2)

- **Pitch Envelope**: a quick pitch sweep at the start of each note, for kick-like basses, zaps, laser stabs and scoops.
  - **Amount** (-48 to +48 semitones; negative scoops up from below).
  - **Attack** (rise to the peak; 0 starts at the peak) and **Decay** (back to the note).
  - **Curve** (how the decay bends) and **Velocity** (how much harder notes sweep further).
  - **Target**: every oscillator, or just one of Osc 1, Osc 2, Osc 3, Sub, Wavetable 1, Wavetable 2, Complex or SuperSaw.
  - Amount 0 is off, exactly as before.
- **MSEG 1 and 2**: drawable multi-stage envelopes, one per note, used as Mod Matrix sources.
  - Each has a start level and up to 8 stages, each with its own time, level and curve.
  - Editing: drag a point to move it, double-click to add one, and shift-drag a segment to bend it. Right-click a point to make it the sustain point, start or end a loop, or delete it.
  - While the key is held, the sustain stage holds and a loop repeats. On release, the envelope carries on after them.
  - **Sync** measures stage times in beats at the DAW's tempo instead of seconds. **Time Scale** (x0.25 to x4) stretches the whole shape.
  - *Presets*: Pluck, Swell, Rhythmic loop and ADSR.
  - A playhead shows where the newest note is.
  - A shape that is being edited while a note plays doesn't change that note; the next note uses the new shape.
- MSEG shapes are saved in patches, projects, undo history and A/B. Old patches and projects load with the pitch envelope off. Nothing is routed from the MSEGs until you add a route, so old sounds don't change.

## Factory presets

The patch menu (and *Browse*) starts with 32 built-in presets, followed by your saved patches. The arrows step through both.
Each preset is an ordinary patch - routes, macros (named), scenes, DNA steps and the effects rack included - so it can be
edited, saved, mutated or bred. Loading one can be undone.

- **Original 18**: Init Genome, Reese Mutation, Sub Helix, Neuro Splice, Folded Acid, Ring Mutant Bell, Frequency Shift Pad,
  Spectral Freeze Choir, Granular Cloud, Marimba Cell, Membrane Drum, Chaos Engine, Breeding Pad, Sequenced DNA,
  Feedback Organism, Jungle Stab, Mutant Lead, FM Helix Bass.
- **Jungle / DnB bank** (built around Filter 2, the DNA Sequencer update and the effects rack):
  - *Amen Reese*: a ladder, then a slow notch sweep on Filter 2.
  - *Neuro Rollers*: two DNA lanes (16 and 12 steps) with ratchets and an AAAB chain, moving parallel filters.
  - *Hoover 93*: a pitch scoop into each note and a flanger.
  - *Ragga Siren Stab*: an S950, then Beat Repeat. Hold E0 to stutter.
  - *Sub Pressure*: a clean sine sub.
  - *Liquid Pad*: a stereo split between the two filters, a phaser and a quarter-note pump.
  - *Dread Wobble*: a DNA-synced wobble with a triplet pump.
  - *Pirate Radio Lead*: SP-1200 crunch.
  - *Stutter Pluck*: random Beat Repeats.
  - *Jungle Atmos*: a reverb wash resampled by the E-mu, which is moved to the end of the rack.
  - *Rave Chord Stab*: chord memory turns one key into an open rave minor chord.
  - *Synced Wob Bass*: a drawn wub shape on LFO 1 at 1/8, locked to the song position.
  - *Arp Hoover*: an up/down 2-octave arpeggio with accents and rests.
  - *Liquid Arp*: one key plays a swung minor 9 arpeggio with probability steps.

  The sequenced and repeating presets follow your DAW's tempo, so set it to 160-175 BPM.

## Quality (header)

- **Eco** - Wave Mutation without oversampling, Resonator capped at 6 modes, Granular at 24 grains, Spectral FFT at 1024
  (also lowers latency). Use it for big projects.
- **Normal** - as designed (2x oversampled Wave Mutation).
- **High** - 4x oversampled Wave Mutation for the cleanest folding and crushing at high notes.

Quality is your setting rather than part of the sound, so loading a preset keeps it.

## Modulation matrix (Modulation > Mod Matrix)

Any source can drive any sound parameter: 32 routes, each with its own depth.

- **Sources**: LFO 1-4, amp / filter / mod envelopes 1-3, velocity, note number, key tracking, aftertouch, poly aftertouch, mod wheel, pitch bend, two assignable MIDI CCs (CC A / CC B), MPE pressure / slide / glide, random per note, smooth random, stepped random, sample & hold (of LFO 1), chaos, slow drift, note gate, note-on and note-off pulses, envelope / audio / transient followers, and the raw outputs of Osc 1, 2, 3 and Sub at audio rate.
- **Destinations**: every continuous parameter in the synth (151 of them), picked from a menu grouped by module or found with the search box. Depth is a share of the destination's full travel, so +25% means the same distance on a cutoff knob and on a level knob.
- **Per route**: on/off, curve (linear, exponential, logarithmic, S-curve, inverted, rectified, quantized, smooth, stepped), polarity (+/- swings around the knob, + only pushes up), a *via* source that scales the depth (e.g. mod wheel opens LFO vibrato) and smoothing.
- **Routes can modulate other routes' depths** (pick *Mod Matrix: Mod N Amount* as the destination).
- **Right-click any knob** to modulate it or to remove/find its routes. A modulated knob shows a cyan ring for the range the routes can reach and a white dot for the value the newest note is hearing.
- **Audio rate**: Osc 1/2/3/Sub outputs can drive oscillator pitch (FM), levels (AM), filter cutoff and resonance, FM depths, ring mix and the complex oscillator's FM and fold, sample by sample. They can't drive other destinations; the matrix marks those routes inactive.
- Envelope times are read when a note starts, so routes onto them use the note-on values (velocity → attack works; an LFO onto attack takes its value at note-on). Effects are shared by all notes, so routes onto effect parameters follow the newest note.
- **Macros** (Modulation > Macros & Scenes): 8 macro knobs you can rename. Right-click any knob > *Assign to macro* to add it (each assignment is a matrix route, so its depth, curve and direction can be changed there). Macros are also sources and destinations in the matrix, so the mod wheel or an LFO can drive a macro that drives ten knobs.
- **Scenes A-D**: *Store A-D* saves the whole sound (oscillators, filter, envelopes, LFOs, effects; not the routes, macros, volume or sequencer). Turn on *Morph* and drag the XY pad to blend them: knobs blend smoothly, switches such as waveform and filter type take the nearest scene's setting. With Morph on, the A-D buttons choose which scene the panel shows and edits; with it off they recall a scene. Scene X and Y are normal parameters, so they can be automated, put on a macro, or modulated per note from the matrix.
- The original LFO / envelope slots (Modulation > Assignments) still work exactly as before, alongside the matrix. Patches made before the matrix load with it empty and sound the same.

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
- **Analog Warmth** (Mixer): *Warmth* makes the filter's Drive saturation softer, with a little 2nd harmonic (it doesn't colour the filter at 0 dB Drive), and adds a gentle low lift and softer top on the master. *Bass Keep* holds onto low end in the classic filter models with high-pass or band-pass stages (Steiner-Parker, State Variable, MS-20, Oberheim, SEM, CS-15...). *Drift* lets each oscillator wander slightly and start free-running. They default to 0.5 / 0.5 / 0.3; set all three to 0 for the browser's exact sound.
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
