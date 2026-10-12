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
- **Random** (header): makes a random patch with the settings on Lab > Random Patch (see *Random Patch* below). One undo step.
- **Init** (header): a clean starting point. One saw on Osc 1, filter open, plain amp envelope, no effects, no mutation, sequencers or arp; the mod wheel adds vibrato. New instances start from it (a project you open always comes back exactly as saved). *Save current patch as Init* makes the current sound your own Init, used by the Init button, new instances and the browser's *Init* entry; *Reset Init to factory* goes back to the clean one. *Reset every control to its default* is the old Init.
- **Sequencer**: tick *Run Sequencer*. *Clock* chooses its own tempo knob or the DAW transport (steps lock to the DAW's 16ths and run while the DAW plays). *Random Phrase* uses the generator mode, scale and length.
- **Perform tab** (first tab): the 8 macros, the scene XY pad, Master Mutate (amount and seed), the DNA Sequencer's on / pattern / swing / density / depth, the main filter and effect knobs and the Beat Repeat pad, all on one page for playing live.
- **MIDI learn**: right-click any knob, switch or menu, choose *MIDI Learn*, then move a knob or fader on your controller (the MIDI IN panel shows what is waiting to be learned). The knob shows its CC number; right-click again to *Forget MIDI*. Moving a CC that's already used moves it to the new knob. The MIDI map is saved with your DAW project (not in patches), so changing patches keeps your controller layout. CC 120-127 (channel mode messages) can't be learned.
- **Window size**: drag the bottom-right corner, or use *Size* in the header (75 / 90 / 100 / 125 / 150%). The size is saved with the project.
- **A / B compare** (header): both slots start as the loaded patch. Edit one, switch to the other to hear the difference; *Copy* copies the slot you're playing to the other one. Switching can be undone; loading a patch resets A/B.
- **Browse** (header): the patch browser.
  - **Categories:** Reese, Sub & Bass, Neuro, Hoover & Rave, Stabs & Chords, Pads & Atmos, Leads, Breaks & Drums, FX & Risers, Keys & Plucks and Uncategorised. Then *Factory*, *Your patches*, each subfolder of your patch folder, and *Deleted*. Each shows how many patches it holds.
  - **Each patch's information:** a category, tags, an author and a note. It's saved inside the patch file (the sound isn't touched), so it goes with the patch when you copy, export or import it. Patches saved before this show as *Uncategorised*.
  - **Factory presets:** each has its information built in. Its old grouping ("Bass", "Jungle / DnB"...) is kept as tags, so searching for those still works. If you edit a factory preset's information, the edit is kept in `factory_meta.json` in the patch folder, and setting it back as it came removes the edit.
  - **Search and filters:** every word you type must match the name, category, tags, author, note or description. The *tag* and *author* menus narrow the list further. Favourites: click the star, or use the panel's *Favourite* box (stored in `favourites.txt`).
  - **The panel on the right** edits the selected patch's category, tags, author and note (*Save info*).
  - **Author:** type a name, or pick one used before from the list. The list holds every author you've saved with and the authors of your patches, most recent first, and it's kept in `authors.json` in the patch folder. *Forget* takes the name shown off the list; no patch changes.
  - **Several at once (Shift / Cmd-click):** choosing a category moves them all there. *Set author* and *Set note* give them all the same author or note; a blank note leaves each one's own. *Add tags* / *Remove tags* change them all. Each patch keeps the rest of its information. A value they all share is shown; otherwise the field says *(mixed)*.
  - The *tag* and *author* filters and the category counts update as soon as anything changes (saving, Save info, a change to several patches, Import, Delete, Restore). If the tag or author you're filtering by no longer exists, the filter goes back to *Any*.
  - **Deleting:** *Delete* (or the Delete key) moves your patches into a *Deleted* folder inside the patch folder, out of the patch menu. *Restore* puts them back where they were, with their information. *Delete forever* removes them for good, and asks first.
  - **Import / export:** *Import...* copies patch files from anywhere into your patch folder, with their information (a new name if one is taken; files that aren't patches are skipped and listed). *Export...* saves the selected patches, factory ones included, as patch files in a folder you choose.
  - *Load as you browse* lets the arrow keys audition patches. Enter or double-click loads and closes; Esc closes.
- **Save** (header): asks for the name, category, tags, author and note, then saves into your patch folder (*Save As...* for anywhere else). It offers the current patch's category and tags. The author is the same editable list as the browser's; the last one you used is offered next time; a factory preset's author ("Mega Synth") isn't copied.
- **Host presets**: in AU hosts (Logic's preset menu) and the standalone app, the factory presets are also listed as programs. VST3 shows a single program, because VST3 makes programs an automatable parameter that could load a preset over your settings; use *Browse* instead. A MIDI Program Change (0-127) loads that factory preset (the first 128) in every format.
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
| Oscillators | Osc 1-3 & Sub, Wavetables, Complex & SuperSaw, Unison, FM & Ring, Granular Synth, Break Chopper, Reese, Core A, Core B, Break React |
| Mixer | (one page) |
| Filters | (one page) |
| Envelopes | Amp & Filter, Mod Envelopes, Pitch Envelope, MSEG 1 & 2 |
| FX & Mutation | Note Mutation, Bus Mutation, Space, Dirt, Motion, Rack |
| Modulation | LFOs, Mod Matrix, Assignments, Macros & Scenes, Cell Instability |
| Sequencers | Step Sequencer, Arp / Chord, DNA Sequencer, Break Sequencer |
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

Each filter has **one Filter list**. It holds every classic model and every character model, grouped by family:
- **Clean:** Standard LP, Sallen-Key, State Variable and Switched Capacitor. The Master Filter also has **Clean (SVF)**, its original filter.
- **Analogue models.**
- **Acid:** Acid Filter Approx and TB-303.
- **Drive & character**, **Formant & vowel**, **Comb, phaser & peaks** and **Digital / lo-fi:** the character models.

How the list behaves:
- **A classic model:** chooses that model at its own slope, exactly as the old Model menu did.
- **A character model:** replaces Type and Slope, and the **Character** knob does what the model needs.
- **Greyed controls:** a control the chosen filter doesn't use is greyed out but keeps its setting. That's Type and Slope for the character models, Character for the classic models, Resonance for Resampler, and Cutoff (and what moves it) for DJ Filter.
- **Old patches:** the list stores the same two settings as before (Model and Character), so old patches and automation are unchanged.

The character models, with ideas from Beep Street's Sunrizer plus extras:

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

- **Sources**: a strip each for Osc 1, 2, 3, Sub, WT 1, WT 2, Complex, SuperSaw, the Ring modulator, the Granular Synth, the Break Chopper, the Reese engine and the two Core slots, with **Level**, **Pan**, **Voices** (unison; the SuperSaw's own voices; the ring modulator's wet / dry), **M**ute and **S**olo, a meter, and a **PITCH ENV** tag that lights when the pitch envelope moves that source.
  - Pan is a balance: centred, both sides stay at full level, so a centred mix sounds exactly as it did before the strips. Panning turns the other side down.
  - A muted source still works as an FM or ring modulator. With any Solo on, only soloed sources are heard. Mute and Solo ramp over a few milliseconds, so they don't click.
  - Pan is a Mod Matrix destination. The strips aren't changed by Mutate.
  - The strip meters show each source before the filter. They're only measured while the Mixer is on screen (about 0.2-1% of a core with 8 notes); the output meter costs next to nothing.
- **Engines**: the mutation stages (DNA Splice, Wave Mutation, Audio-Rate, Resonator, Granular, Spectral, Feedback), lit when they're doing something, with their Mix where they have one. Click one to open its page.
- **Effect Returns**: Delay, Reverb, Shimmer, Chorus, Reverse.
- **Analog**: Warmth, Bass Keep, Drift.
- **Output**: Master Volume (before the effects, as always), the Master Filter's On and Cutoff, the Output trim, and a stereo meter with the peak held.

## Reese Engine (Oscillators > Reese)

A second oscillator built for jungle and drum & bass basses: 2, 3 or 4 detuned voices per note whose beating you can lock to the song. It's **off by default** (Level 0). At Level 0 the sound is bit-for-bit what it was, whatever its other settings, so older patches and projects are unchanged and load with it off.

- **Reese Engine:** Level, **Voices** (2, 3 or 4), **Waveform** (Saw, Square, or a Saw/Square blend; band-limited), **Octave** and **Semi** (relative to the note; the pitch bend, pitch LFOs and the pitch envelope set to "all oscillators" move it too), **Retrigger** (Free: the voices carry on from where they were; Reset: every note starts the same; Random).
- **Detune:** **Mode** chooses how far apart the voices are:
  - **Cents** - the usual way: the beating speeds up as you play higher.
  - **Hz** - the same beating on every note.
  - **Beats (locked to tempo)** - the beating is a note value at the song's tempo (1/16 up to 4 bars, dotted and triplet), worked out for each note, so the wobble lands on the beat at any pitch. It follows the host's tempo (or the Sequencer's tempo when the host has none), and tempo changes.
  - **Voices 3-4:** beat at **x3** or **x5** the main rate (still locked: the whole pattern repeats once per note value), or at the **golden** ratio (free; never quite repeats).
  - The line under Detune shows the beating now and the voices' frequencies at A1.
- **Phase & Width:** **Twist** pushes the voices' phases back and forth at **Twist Rate** (slow: a swirl, fast: a growl). **Width** spreads the voices; **Rotation** moves them round the stereo field at **Rot. Rate**. **Mono Below** (60-250 Hz) keeps everything under it in the middle (the side signal is high-passed, 24 dB/octave).
- **Notch:** a moving notch, **Depth**, **Freq**, **Move** (up to two octaves either way). **Synced** moves it with the beating; otherwise at its own **Rate**.
- **Character:** **Sub** (a sine an octave below voice 1, phase-locked to it), **Drive** (a smooth saturation, faded in from zero) and **Tone** (a low-pass after the drive).
- It has its own **Mixer** strip (Level, Pan, Width, Mute, Solo, meter) and shows in the Overview ("Oscillators + Reese" lights when it's on). It goes into the voice's filters with the other oscillators.
- **Modulation:** every continuous control is a Mod Matrix destination (Octave, Semi and the switches aren't). The new source **Reese Phase** runs 0 to 1 once per beating cycle, so you can lock anything else (a filter, the notch, a width swing) to the wobble. Mutate leaves the Reese alone.
- **Quality:** the voices are band-limited at every setting. The Drive is oversampled x2 on Normal and x4 on High (with anti-aliasing on the curve itself); on Eco it isn't.
- **Measured** (our build machine, 48 kHz): the beating period within 0.04% of the note value across 90/140/172/174 BPM and C1-C5 (1% allowed); at C7 on High, aliasing at -114 dB without the drive, -64 dB (2 saws, drive 60%) and -70 dB (2 squares, drive 100%); with full drive on Normal it's about -48 dB and on Eco about -25 dB. Mono Below 120 Hz takes the side signal under 70 Hz from -5 dB to -34 dB. The sub stays locked to voice 1 (no measurable drift over 18 s).
- **CPU** (8 notes held): about 3-6% of one core for the Reese on top of Init at 2-4 voices; with the Drive at 4 voices, about 5% on Eco, 9% on Normal and 15% on High. The numbers vary a few percent from run to run.

## Break React (Oscillators > Break React)

Break-Reactive mode: the synth listens to the breaks and reacts on the hits you choose, so a bass locks to the drums. It's **off by default**; off, the sound is bit-for-bit what it was, and older patches and projects load with it off.

- **Listen to** All hits, Kicks, Snares or Hats, **From** any break or one of the three. A slice counts as a kick, snare or hat from the Break Chopper's analysis of its first few milliseconds (a rough guess, not a drum detector); **Sensitivity** lets in weaker ones (ghost notes).
- **Triggers:** **Breaks** (every slice played by the Break Sequencer, the slice keys or a fill), a **Note** of your own (Key; it isn't played, so a MIDI clip can fire the reactions anywhere), and the host's **Sidechain** (drum hits found in the sidechain input, with SC Sens). With the sidechain, listening for kicks, snares or hats needs 5 ms to tell them apart, so those hits land 5 ms after the transient; listening to all hits, they land on it.
- **Actions:**
  - **Phase Reset:** the ticked oscillators start their cycle again exactly on the hit (the old phase fades out over 1.5 ms so it doesn't click). For the Reese, its beating starts again too (Reese Phase goes back to 0).
  - **Pitch Kick** (±24 st), **Drive Kick** (a burst of saturation), **Level Duck** (smoothed over about 1 ms) and **Notch Jump** (the Reese's notch leaps up, two octaves at 100%, and slides back).
- **Oscillators:** Osc 1, Osc 2, Osc 3, Sub and Reese each have a tick box. Unticked ones don't react at all. The wavetables, Complex, SuperSaw and Granular Synth don't react.
- **Shape & Timing:** **Attack**, then **Decay** (down to 1% over that time) shapes the kicks and the **Break Hit** source. **Offset** (-10 to +30 ms) moves the reaction later, or earlier. Earlier works by playing the breaks that much later, because the synth can't know a hit is coming; the page says when that's happening. **Humanise** moves each hit a random few ms: either way for the breaks, only later for a note or the sidechain.
- **Mod Matrix:** two new sources, **Break Hit** (the attack/decay envelope, 0 to 1) and **Break Hit Type** (the last hit: 0 kick, 0.5 snare, 1 hat). Drag them from the chips in the title. The amounts are per voice, so they can be modulated; Sensitivity, the shape and the timing follow the global sources, like the arpeggiator's settings.
- **Measured:** the reaction lands on the slice's first sample, from the slice keys and from the Break Sequencer (0 samples apart); Offset +10 ms is 480 samples later at 48 kHz and -5 ms plays the break 240 samples late. After a MIDI trigger, the oscillator's phase is within 0.005 samples of a note started on that sample. The reset's largest step is about the same as the sine's own steepest step, so no click; the duck's is smaller.
- **CPU:** small. With 8 notes and a reaction on every 16th at 172 BPM it measured 1-3% of a core more, within run-to-run noise.

## Core A and Core B (Oscillators > Core A, Core B)

Two slots for a **Core engine**, each heard alongside everything else: its own Mixer strip, then the voice's filters. Both are **off by default** (Engine: Off). Off, the sound is bit-for-bit what it was, whatever the slot's other settings, so older patches and projects are unchanged and load with them off. The first engine is **Kernel FM**; more engines will be added to the same Engine list later (saved patches keep their choice).

**Kernel FM** is six-operator phase modulation ("FM") with a free matrix: any operator can modulate any other and itself.

- **Slot:** **Engine**, **Level**, **Octave**, **Semi**, **Fine** (relative to the note; the pitch bend, pitch LFOs and the pitch envelope set to "all oscillators" move it too), **Unison** (1, 2, 3, 4, 6 or 8 copies of the whole kernel) with **Detune** (the outer copies are +/- this many cents) and **Spread** (across the stereo field), **Retrigger** (Reset: every note starts the operators together, the usual for FM; Free: they run on from the last note; Random), **Bright** (scales every modulation amount, 0-200%: one knob for the whole timbre) and **Env Speed** (all six envelopes x0.25 to x4 as fast).
- **Operators** (one row each): **Wave** (Sine, Half Sine, Abs Sine, Quarter Sine, Alt Sine, Camel Sine, Soft Square, Soft Saw), **Ratio** (x0.25 to x32 of the note, in quarters) or **Fixed** at its own **Hz** (for clangs and noise), **Fine** (+/- 100 cents), **Level** (how loud it's heard and how hard it modulates), **Vel** (how much velocity changes the level), **Key** (key scaling: louder or quieter up the keyboard, +/- 6 dB per octave from middle C), **Out** (how much of it you hear; 0 = it only modulates), and its envelope: **Delay, Attack, Hold, Decay, Sustain, Release**.
- **Modulation:** the last six knobs on each row send that operator into operators 1-6, in radians (the modulation index: 1 at full level moves the other operator's phase by up to 1 radian). The one on its own row (**FB**) is feedback: it uses the average of the operator's last two samples, so it stays stable all the way up (it turns into noise rather than blowing up). Operators are worked out from 6 down to 1, so a higher operator modulating a lower one (the usual stacks) acts in the same sample; the other way round acts one (oversampled) sample later.
- **Routing:** a picture of what the matrix is doing right now (heard operators on the right, the ones modulating them to the left, a loop for feedback, thicker lines for more). **Algorithm...** writes one of 12 starting routings into the matrix (stacks, pairs, a Y, a tree, a fan, all six heard...): connections at 2, outputs for the heard operators, and a level for any operator it uses that was at 0. Then edit the matrix freely; the picture follows. These are in the spirit of the classic six-operator algorithms, not a copy of any synth's numbering. **Reset** puts the slot back to one sine (the engine choice kept). Both are one undo step.
- **Envelope** (on the Operators section, per slot): **Amp + Operators** (the default, and how it always worked) puts the voice's amp envelope on top of the operators' own envelopes, so a long operator Release is cut off when the amp envelope ends. **Operators only** leaves the amp envelope off this slot: the operators' envelopes shape it on their own, Release included, and the voice stays alive until they've finished (about -80 dB), and then frees itself. The other oscillators still follow the amp envelope (it's applied to them before the filter in this mode, so a driven filter or Wave Mutation sounds slightly different on their attack and release than in Amp + Operators). The sustain pedal holds the operators; their Release starts on pedal up. Voice stealing works as usual. When the amp envelope is cutting an operator Release short, an amber hint next to the switch says so ("Release 2.00 s is cut at the Amp Release (0.30 s): choose Operators only to hear it").
- **Space effects:** the Core goes through the same filter, Space, Dirt, Motion and Rack effects as every other source. Measured against Osc 1 playing the same sine through reverb, delay, chorus and the rest, across unison, every quality, Core B, rack orders, routes and presets, the level through each effect matches within 0.04 dB. If the reverb seemed to be skipped, it was the amp envelope cutting the Core's tail short (so less went into the reverb); Operators only fixes that.
- **Mixer:** each slot has its own strip (Level, Pan, Bright, Mute, Solo, meter), and the Overview's first block reads "Oscillators + Core A" (or B, or A/B) when they're on.
- **Mod Matrix:** every continuous control is a destination (Octave and Semi aren't; the switches aren't). New sources: **Core A (audio rate)** and **Core B (audio rate)** (the slot's output, for the audio-rate destinations like the filter cutoff), and **Core A Op 1 Env** / **Core B Op 1 Env** (operator 1's envelope). Mutate leaves the Core slots alone.
- **Quality:** the whole kernel runs oversampled on **Normal (x2)** and **High (x4)** and comes back down through half-band filters, so the sidebands a high index makes do not fold back as aliasing; on **Eco** it isn't oversampled.
- **Measured** (our build machine, 48 kHz): a 2-operator pair matches the Bessel functions (the textbook FM spectrum) within 0.03 dB at index 1 and 2, and a 1:1 pair matches the formula sin(wt + I sin wt) exactly; operator tuning within 0.0003 cents (ratios, fine and fixed Hz, every quality); envelope stages within 0.1 ms of their settings (Env Speed x1 and x2); feedback finite and bounded (peak below 1.6) up to 24 radians on every waveform and quality. Aliasing on a bright patch (index 4 with feedback) at C6: the worst non-harmonic component is -98 dB on High, -65 dB on Normal and -30 dB on Eco (no oversampling).
- **CPU** (4 notes held, all six operators in a stack with feedback, on top of Init): about 5-6% of one core on Eco, 8-10% on Normal and 15-17% on High with one unison voice; with 8 unison voices about 9%, 15-20% and 35-45%. Fewer operators cost less (silent ones are skipped). The numbers vary a few percent from run to run.

## Granular Synth (Oscillators > Granular Synth)

A playable granular oscillator. Each note plays its own cloud of grains from a sample you load. It's separate from the **Granular** bus stage (FX & Mutation > Bus Mutation), which grains the whole mix.

- **Sample:**
  - Load a WAV, AIFF, FLAC or MP3 (Ogg also works). Loading switches the synth on. The waveform shows Position, the area the grains come from, and the newest note's playhead. Click or drag on the wave to set Position.
  - The sample is saved inside the project and the patch, and comes back with undo and A/B.
  - **Length cap: 20 s.** A longer file is cut to its first 20 s, and only that part is saved (as a 24-bit WAV). That keeps a project to roughly 10 MB at most, which keeps saving, undo and A/B quick.
  - The page shows how much the sample adds to the project, and warns above 8 MB.
- **Grains:**
  - **Position:** where in the sample the grains come from.
  - **Scan:** how fast the playhead moves through the sample, from -2x to 2x. 0 freezes it.
  - **Spray:** random start offsets, 0 to 500 ms.
  - **Grain Size:** 5 to 500 ms.
  - **Density:** 1 to 100 grains a second.
  - **Grain Shape:** Hann, Triangle, Flat top, Percussive or Gaussian. Every shape starts and ends at zero, so grains don't click.
  - **Reverse:** the chance of a grain playing backwards.
  - **Spread:** how widely the grains are scattered across the stereo field.
- **Pitch:**
  - **Pitch** (semitones) and **Fine** (cents) shift the grains.
  - **Random** adds a random pitch to each grain.
  - **Root Note** is the key that plays the sample at its own pitch.
  - With **Key Track** on (the default), the grains follow the note you play. They also follow pitch bend, and the pitch envelope when it targets all oscillators. Portamento doesn't glide them.
- **Output:**
  - **Level** feeds the mix before the filters, so the grains go through the filters, amp envelope and effects like any oscillator.
  - It has its own strip on the Mixer page (pan, mute, solo, meter).
  - It can stand in for **DNA Splice** source A or B. A choice in its Output section does this, so the DNA source lists of older patches are unchanged and Mutate gives the same results.
- **Mod Matrix:** every continuous control is a per-voice destination, except Root Note. Nothing here is changed by Mutate.
- **Quality** limits the grains per note: **Eco 8, Normal 24, High 48**. With 8 notes of a very dense cloud (100 grains a second of 400 ms), the grain engine uses about 4% of one core at Eco, 11% at Normal and 21% at High. Typical settings (20 grains a second of 80 ms, about 2 overlapping) cost well under 1% per note.
- **Older patches** load with it off and no sample. When it's off, or has no sample, it costs nothing.
- **Approximations:**
  - Grains read the sample with linear interpolation.
  - Without Spray, the cloud repeats at the Density rate. Like any synchronous granular engine, that colours the sound with a comb of Density-spaced partials, so a little Spray gives a cleaner pitch.

## Break Chopper (Oscillators > Break Chopper)

Load a drum break and play it in slices, from the keys or from the Break Sequencer.

- **Loading:**
  - Load a WAV, AIFF, FLAC or MP3. Loading switches the chopper on.
  - The break is saved inside the project and the patch, and comes back with undo and A/B.
  - **Length cap: 30 s**, which is 8 bars at 64 BPM. A longer file is cut to its first 30 s, and only that part is saved. That keeps a project to roughly 10 MB at most.
  - The page shows the file's size in the project, and warns above 8 MB.
- **Tempo:**
  - **Bars** set to Auto picks the bar count that puts the break nearest 130 BPM, within 88-190, assuming the file is trimmed to whole 4/4 bars.
  - You can also choose the bar count, or type the **Tempo** in, which overrides both.
  - The page shows the break's BPM, the song's tempo, and the stretch ratio.
- **Slicing:**
  - **Transients** finds hits by how fast the level rises. **Sensitivity** sets how small a rise counts and how close together hits can be.
  - **Equal** 4, 8, 16 or 32 divides the break evenly.
  - **Manual markers:** click the wave to add a marker, and right-click to remove the nearest one (more under *Chopping by hand*). **Slices to markers** turns the current slices into markers you can then edit.
  - Up to 32 slices.
- **Keys:**
  - Slices play from **First Key** up. The default is C1 (MIDI 36), and the key names are drawn on the wave.
  - Those keys play slices only while the chopper is on. Performance FX keys, if set to the same range, take priority.
  - Clicking a slice plays it and selects it.
- **Per slice:** Pitch, Gain, Pan, Trim Start and Trim End, Attack, Decay (off means it plays to its end), Choke group, Stretch, Cycle, Reverse and Loop.
  - **Stretch** and **Cycle** override the break's stretch mode and Cycle Length for that slice. "Break's" means no override. A sequencer step's own Stretch and Cycle win over these.
  - **Choke group:** slices in the same group cut each other off with a 3 ms fade. The default is group 1, so a new hit stops the last one, like a mono sampler. Set it to none to let hits ring over each other.
  - **Loop:** the slice loops while its key is held.
- **Stretch:**
  - **Repitch:** played faster or slower, so pitch moves with tempo, like a sampler.
  - **Akai Cyclic:** the slice is cut into cycles of **Cycle Length** (10-200 ms). Each cycle plays at the slice's pitch from where the slice should be by now, so cycles repeat when slowing down and skip when speeding up. Successive cycles are joined with a **Crossfade**.
  - **Clean (WSOLA):** overlapping windows, each placed where it best continues the last (a search within a fifth of a window). This keeps tone and transients far cleaner. **Quality** sets how finely it searches.
  - **Tempo Sync** (on by default) stretches the break to the song's tempo. Off, it plays at its own speed.
- **Sound:**
  - **Colour:** the Vintage Sampler's SP-1200, S950 and E-mu models, or a plain 12-bit.
  - **Pitch** and **Velocity** (how much key velocity sets the level).
  - **Level**, plus its own strip on the Mixer page (pan, mute, solo, meter).
  - It joins the bus **before the bus stages and the effects**.
  - **Through Synth Filter** (off by default) runs it through Filter 1's model, type, slope, cutoff, resonance, drive and character.
- **Mod Matrix:** every continuous control except First Key and Tempo is a destination. Nothing here is changed by Mutate or the scenes.
- **CPU:** 8 slices at once cost about 0.3-0.6% of one core with Repitch and 0.4-0.9% with Akai Cyclic. Clean costs about 3-4% at Eco, 5-6% at Normal and 15-17% at High (its search). A single slice is a fraction of that.
- **How close is Akai Cyclic to a real S1000?**
  - It follows the same idea: fixed-length cycles, repeated or skipped, with a crossfade.
  - It isn't a copy of Akai's code or of its exact crossfade shape, timing or 16-bit/12-bit converters. Use the Colour models for the converter side.
  - It has the character you'd expect: a "flutter" at 1 / Cycle Length. A steady tone becomes a set of partials that far apart, for example 25 Hz apart at 40 ms.
  - Real S1000 users tuned the cycle length to the tempo to hide this; here you do it with Cycle Length.
- **Factory content:** no copyrighted breaks are included.
  - The three new presets (**Break Chop 170**, **Break Rollers**, **Break Keys**) use a two-bar 170 BPM break synthesised by the plugin itself: a sine-sweep kick, a tone-and-noise snare with ghost notes, and noise hats, in an original pattern.

### Chopping by hand (chop mode and Manual markers)

- **Chop mode:**
  - Switch on **Chop** and the Active Break plays from the start at its own speed. Every key or pad you hit, or the space bar or **Tap**, marks a slice there.
  - While chop mode plays, keys and pads only make markers; they don't play notes.
  - **Play** starts it again, and **Undo chop** removes the last tap.
  - Turning chop mode on switches that break to Manual markers, starting from the slices it already has. Use **Clear markers** to start from nothing.
- **Accuracy:**
  - Taps from MIDI keys and pads are placed to the sample: the marker goes where the break was when the key arrived, less **Latency**.
  - **Latency** (default 10 ms) is how long your audio interface takes to play what the plugin makes. Set it to your interface's output latency, which Live shows in its Audio preferences.
  - Taps from the space bar or the Tap button are timed on the screen's clock, so they're only accurate to a few milliseconds. Snapping fixes most of that.
- **Snapping:** **Transient** moves a tap to the nearest hit within 20 ms; the detector marks the start of the rise. **Zero** then moves it to the nearest zero crossing within 2 ms. Hold Alt when clicking to place a marker exactly.
- **Manual markers mode:**
  - Click the wave to add a marker, and double-click a slice to split it in half.
  - Right-click removes the nearest marker. Clicking a marker selects it, and Delete or Backspace removes it.
  - Clicking the strip of key names along the top always selects and plays a slice.
  - **Snap all** moves every marker onto the nearest hit. **Clear marks** removes every marker.
- **Live and the space bar:** Live uses the space bar for its transport, so it may never reach the plugin there. Use a key, a pad or the Tap button instead.

### Zooming and moving markers

- **Zoom:**
  - The mouse wheel or a trackpad pinch zooms around the pointer. Shift+wheel (or a sideways swipe) and the scroll bar under the wave scroll.
  - The **-**, **+**, **All** and **Slice** buttons zoom out, zoom in, show the whole break, and zoom to the selected slice.
  - Full zoom is 1 sample per pixel. Close in, the wave is drawn sample by sample; further out it comes from a min/max cache, so long breaks stay smooth.
  - The Active Break's small wave at the top shows the zoomed view as a frame. Click or drag there to move the view.
- **Moving markers:**
  - Drag a marker left or right. In Transient or Equal mode, dragging a slice start first switches that break to Manual markers, keeping the slices it had.
  - **Snap** decides where a dragged marker lands: none, zero crossing (the default), transient (within 20 ms), or a 1/8, 1/16 or 1/32 grid at the break's own tempo. Hold Alt for a fine move with no snap.
  - The arrow keys nudge the selected marker by 1 ms, or 10 ms with Shift.
  - A marker stops 64 samples before its neighbours, so markers never cross. Slice 1 always starts at 0.
  - Each drag or nudge is one Undo step, and markers are saved with the patch and the project.
- **Trim handles:** the two triangles along the bottom of the wave are the selected slice's Trim Start and Trim End. Drag them; the Trim knobs follow.

### Three breaks

- **Slots:** there are three break slots, Break 1, Break 2 and Break 3. Each has its own sample, slicing, markers, tempo, per-slice settings, stretch mode, colour, level and pan.
- **Active Break:** the selector at the top of the page picks the Active Break. That's the break you're editing, and the break the keys play.
  - The three small waveforms show all the breaks; click one to make it active.
  - The Active Break is a parameter, so you can automate it, MIDI-learn it, or drive it from the Mod Matrix.
- **One key zone:** all three breaks play from First Key up. The zone is as long as the Active Break's slice count and is tinted in its colour on the keyboard.
  - The three **Break Select** keys just below the zone switch the Active Break: A0, A#0 and B0 when the zone starts at C1. They move with First Key.
  - **On the Beat** makes the switch wait for the next beat.
  - A key you're holding keeps playing the slice it started; letting go stops that slice, whichever break it came from.
  - Older projects load with Break Select off, since those keys may be notes they play.
- **The sequencer:**
  - Each step plays the Active Break by default, or can be pinned to Break 1, 2 or 3. The grid shows a B1/B2/B3 tag and the break's colour.
  - **Layer** stacks a second hit from another break on a step, with its own slice and level.
  - **Pattern Break:** a pattern can make a break active when it starts, so pattern keys can change the break too.
  - **Alternate** swaps the Active steps to another break every other bar, every other 2 bars, or every other 4 bars.
- **New tools:**
  - **Franken (Frankenbreak):** the Active Break's groove, with its kicks, snares and hats each taken from a different loaded break, matched kind for kind using the kick/snare/hat analysis. **Amount** sets how many hits are swapped; each press gives a new mix.
  - **Swap:** the same pattern played with the Active Break's slices in the same places.
  - **Match Levels** evens out the loaded breaks' loudness with their level faders.
  - **Match Tempo** turns Tempo Sync on, so every break follows the song.
- **Mixing:**
  - All three share **one Break strip** on the Mixer (pan, mute, solo, meter, master level).
  - Each break has its own level (0-200%) and pan on the Break Chopper page.
  - The level faders are Mod Matrix destinations, and **Break Slot** (which break played last) is a Mod Matrix source.
- **Size:** each break keeps the 30 s cap. The page warns when the three together add more than 20 MB to a project.
- **CPU:** 8 slices spread over three breaks, each with a different stretch mode and colour, costs about 1.7% of one core.
- **Factory content:**
  - Two more breaks are synthesised by the plugin: a 2-step at 174 BPM and a ghost-note roller at 165 BPM.
  - **Break Frankenstein** loads all three and chains the original with Frankenbreak edits.
- **Key names:** the Break pages name keys the way Live does (MIDI 60 = C3). The on-screen keyboard has always labelled MIDI 60 as C4, so the zone that Live calls C1 shows as C2 there.

## Break Sequencer (Sequencers > Break Sequencer)

- **Patterns:**
  - Eight patterns, **A-H**, each of **16 or 32** sixteenth steps (one or two bars).
  - Each step has: on/off, **Slice**, **Velocity**, **Accent**, **Pitch**, **Reverse**, **Roll** (2, 3 or 4 hits within the step), **Probability**, and **Tie** (no new hit; the last slice rings on).
  - Click a step to edit it below the grid; double-click switches it on or off. The playing step is lit.
- **Timing:**
  - Locked to the song while the DAW plays, starting on the right step even if you press play mid-bar. **Swing** pushes the off-beat sixteenths.
  - It waits while the DAW is stopped. With no transport at all (standalone), it runs on the synth's own tempo.
- **Switching patterns:**
  - Pick a pattern with the A-H buttons, or from 8 keys (**Pattern Keys**: C0-G0, C4-G4, C5-G5 or C6-G6).
  - A new pattern starts on the **next step, beat or bar** (Switch).
  - **Chain** plays a typed order, e.g. AABA, moving on each time a pattern ends.
- **Tools:**
  - **Original:** the break's slices in order, each on the step where it falls.
  - **Reverse:** the pattern back to front.
  - **Shuffle:** the same slices in a new order.
  - **Jungle:** an edit of the original, with snare ghosts and swaps late in each bar, a kick double, a reversed hit, a cut-up of two 1/8s, and a rising snare roll into the end.
  - **Copy / Paste** and **Clear**.
  - Shuffle and Jungle use a seed that moves on with each press. Pressing again gives a new one, and undo goes back.
- **More tools (the third row):**
  - Each applies straight away when pressed, with a new seed each press. It doesn't preview first; **Undo** takes it back.
  - **Amount** (0-100%) sets how much each tool changes. **Variation** has its own knob.
  - **Tools draw from:** the Active Break, or all three loaded breaks (each new hit from a break picked at random, pinned to that break).
  - Kicks, snares and hats are chosen from the kick/snare/hat analysis. A break with no analysis falls back to where hits usually are: the kick on beat 1, the snare on beat 2, the hats on the off-beat 8ths.
  - An empty pattern is first filled with Original (Halftime builds its own).
  - **Rollers:** quiet 16th ghost snares around the backbeat.
  - **Two-Step:** kicks on 1 and the "and" of 2, snares on 2 and 4. The rest of the pattern stays, and Amount sets how many of its other kicks go.
  - **Halftime:** the snare on 3 only, kicks on 1 and the "and" of 3, and hats from the break.
  - **Double (double-time):** each half bar squeezed into a quarter and played twice.
  - **Snare Build:** a snare roll into the end of the pattern, x2, then x3, then x4, rising in pitch by up to an octave.
  - **Stutter:** a hit repeated 2-4 times as 1/16s or 1/32s, at random places.
  - **Rev Swell:** a reversed snare on the step before each backbeat.
  - **Cut-up:** 1/8s swapped at random, keeping the kick on 1 and the snares on 2 and 4.
  - **Ghosts:** quiet hits on empty steps.
  - **Hat Shuffle:** only the hat-like hits change places, and the velocities stay where they were.
  - **Thin Out:** for breakdowns, hits are removed or made less likely. The kick on 1 and the backbeats stay.
  - **Humanise:** small velocity changes, and off-beat hits played a little late (up to 16% of a step at full Amount). The beats stay on the grid.
  - **Pitch Dive:** the pitch falls over the last beat, down to an octave.
  - **Variation:** small random changes to the pattern as it is: another slice of the same kind, a velocity change, a roll switched, or a hit moved a 16th later.
- **Per-hit stretch:**
  - Each step has a **Stretch** setting: Default, Repitch, Akai (Cyclic) or Clean.
  - Each step also has an optional **Cycle** (the Akai Cyclic cycle length for that one hit). A long cycle crunches a single hit.
  - The order is: the step's setting, then the slice's (Break Chopper page), then the break's.
  - The grid tags these steps **R**, **A** or **C**. A **\*** means a cycle override.
- **Late:** how late a step plays, as a share of a step (0-50%). Humanise sets it, and you can set it by hand. The grid tags a late step with **>**.
- **Fills (the Fill panel):**
  - **Fill** plays the current pattern's fill. It can come from the **Fill** button, the **Fill Key** (a note you choose, which isn't played as a note), or the Fill parameter, which you can automate or MIDI-learn.
  - **Start:** the next beat, the next bar, or **End of pattern** (the fill starts as many steps before the pattern's end as it is long, so it lands at the end of the phrase).
  - **Length:** 1 beat, 2 beats, 1 bar or 2 bars. **Mode:** **One-shot** plays the Length; **Held** plays from the start until you let go.
  - **When it ends:**
    - **Pattern carries on** (the default): the pattern comes back exactly where it would have been, as if it had kept running underneath.
    - **Restart the pattern:** the pattern starts again from step 1.
    - A running chain carries on underneath either way.
  - **Fill Source** (per pattern):
    - **Auto** (the default) makes a fill from the pattern with one of Snare Build, Stutter, Rollers, Pitch Dive or Frankenbreak. The tool is chosen per pattern from a seed, so it's the same every time, and the knob shows which one.
    - **New Fills** picks again for every pattern; Undo goes back.
    - Or choose a pattern A-H as the fill.
    - A fill plays the last steps of its source, as many as its length. For example, a 1-bar fill from a 2-bar pattern plays that pattern's second bar.
  - **Fill Break** (per pattern) plays the fill's Active steps from another break, for example a fill from Break 2 over a groove from Break 1.
  - **Auto Fill:** a fill every 4, 8 or 16 bars, timed to end with the phrase. **Chance** sets how often it actually plays.
  - **On the grid:** the fill's steps get an orange bar, and its first step shows the countdown ("FILL in 5") and then its progress ("FILL 3/16"), with its length.
  - **Fill Active** (0 or 1) is a Mod Matrix source.
  - **What's saved:** fill settings are saved in patches, projects, undo and A/B. Older projects load with fills off. Auto fills aren't saved; they're remade from the pattern and the seed.
- **Key Entry** (on the Step section, off by default): with it on, playing a slice key (from the Break Chopper's key base) writes that slice into the selected step. The step is switched on, its velocity set from how hard you played, and it's pinned to that slice's break. The slice still plays, so you hear what you're entering. Hold **Shift** (on the page) or the **sustain pedal** to add the hit as a Layer on the step instead of replacing it. **Advance** (No advance, 1, 2 or 4) moves the selection on after each key, wrapping at the end of the pattern, so you can play a beat in step by step. Pattern keys, the Fill key and notes outside the slice range keep their usual jobs. Each key is one undo step. Key Entry and Advance are saved with patches and projects.
- **Mod Matrix sources:** **Break Seq Step** (where in the pattern), **Break Seq Pattern** (which one), **Break Slice** (the last slice played), **Break Slot** and **Fill Active**, all 0..1.
- **What's saved:** patterns, chain and per-slice settings are saved in patches, projects, undo and A/B. That includes each step's Stretch, Cycle and Late, and each slice's Stretch and Cycle.

## Performance FX (Perform > Performance FX)

Fourteen pads to play live on the whole mix. They sit after the effects rack and before the Master Filter and Output. With no pad engaged, and the echo and reverb tails gone, the stage doesn't touch the audio at all, so patches sound exactly as before.

- **Playing:** hold a pad to play it, and let go to stop. With **Latch** on, a click switches a pad on and the next click switches it off.
- **Quantise** (Off, 1/16, 1/8, 1/4, 1/2, 1 bar; default 1/16): a pressed pad waits for the next line of that grid, counted from the bar, then fades in over 4 ms. Letting go fades it out at once. The grid follows the DAW's position while it plays (a pad shows an amber outline while it waits), and the synth's own tempo otherwise.
- **Several pads at once** run in a fixed order, numbered on the pads:
  1. Time pads (the last one pressed wins): **Beat Repeat** (1/4, 1/8, 1/16, 1/32, 1/8T, 1/16T), **Reverse** (the last bar, backwards), **Tape Stop**, **Tape Start**, **Backspin**, **Akai Stretch**. Tape Time sets the stop, start and spin length in beats.
     - **Akai Stretch:** the Break Chopper's Akai Cyclic (S1000-style) time-stretch on the whole mix. Its modes:
       - **Crunch:** real time, one cycle late; **Akai Pitch** shifts it through the cycles.
       - **Slow:** from the press at x0.75 or x0.5, pitch kept.
       - **Fast:** the last bar looped at x1.5 or x2, pitch kept.
       - **Halftime:** the last bar stretched over two.
     - **Its settings:** **Akai Cycle** (10-200 ms, or synced to 1/32 or 1/64), **Akai Xfade**, **Akai Pitch** (±12 st) and **Akai Mix**. It quantises, latches, fades in and out and plays from MIDI keys like the others.
     - **Approximate:** it's the same algorithm as the chopper's Akai Cyclic, written again here (the chopper's code is untouched, so breaks render exactly as before). Its flutter is at 1 / Cycle Length. Slow keeps falling behind, so after about a minute at x0.5 it starts again from now. The optional sampler Colour isn't included.
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
  - **MIDI Keys** plays the 14 pads from 14 keys starting at C1, C2, C3, C4, C5 or C6. The key name is shown on each pad. Those keys play pads, not notes.
  - Right-click a pad for MIDI Learn (CC 64 and above switches it on).
  - Every pad and setting is a real, automatable parameter.
- **What's saved:** everything is saved in patches, projects, undo and A/B. It isn't part of Mutate or the scenes. Older patches and projects load with every pad off.
- **CPU:** about 0.08% of one core when idle (it keeps 8 s of recent audio for Reverse and Backspin), 0.4% with one pad, and about 3-6% with all 14 at once (it varies from run to run on the test machine). Akai Stretch on its own is 0.4-0.5% in every mode. It's the same at every Quality setting.
- **Approximations:**
  - The tape and backspin pads read the recent audio at a changing speed, with linear interpolation (fine for this; not a high-end resampler).
  - Pitch Down is a two-tap delay-line shifter (50 ms window), so it has a little of that classic "chorusy" pitch-shift sound rather than a clean formant-preserving shift.
  - The reverb is a small Freeverb-style network (4 combs and 2 all-passes per side).
  - Echo and reverb tails are cut once they fall below -100 dB.

## Routing: the sub and the breaks past the effects (Mixer > Routing)

- **Sub Route** (also on the Sub Oscillator panel):
  - **Normal** (the default): the sub goes through the filters and every effect, as always.
  - **Dry, before the Master Filter** and **Dry, after the Master Filter**: the sub skips the filters, Note Mutation, the bus stages, the effects and Performance FX. The dry sub keeps:
    - its amp envelope and level;
    - its Mixer strip (pan, mute, solo and meter);
    - Master Volume and Output.
  - "After" also skips the Master Filter.
  - **Mono < 120 Hz:** below 120 Hz the dry sub comes out of both sides equally, whatever its pan (a 4-pole Butterworth on the side signal).
- **Break Effects** (also in each break's Sound panel):
  - **Through the effects** (the default).
  - **Skip effects (Perf FX on):** past the bus stages and the effects rack, so the delay and reverb don't touch the breaks, but still into Performance FX and the Master Filter.
  - **Skip effects and Perf FX:** into the Master Filter only.
  - **Skip all:** after the Master Filter.
- **How the dry paths behave:**
  - Changing a route crossfades over 10 ms, and delay and reverb tails already ringing fade out naturally.
  - The dry paths are delayed by the Spectral stage's latency while Spectral is on, so they stay in time with the rest.
  - The Granular bus stage and the feedback matrix don't hear dry signals, and Capture's "before the effects" point doesn't include them.
- **Saved and old projects:** all three settings are saved in patches, projects, undo and A/B. Older projects load with everything through the effects, as before.
- **CPU:** the dry routes add almost nothing. 8 notes measure the same within the test machine's noise (about ±3% of a core).

## Master Filter and Output (Filters tab, and the Mixer's Output section)

- A stereo filter at the very end of the chain: everything the synth makes, every effect included, passes through it, then the **Output** level.
- **On** (off by default: the sound passes straight through, untouched), **Type** (Low Pass, High Pass, Band Pass, Notch), **Slope** (12 or 24 dB/oct), **Cutoff**, **Resonance**, **Drive** and **Mix** (dry / wet). Its response is drawn next to the controls.
- **Filter:** the same list as Filter 1 and 2.
  - **Clean (SVF)** is the original Master Filter and the default, so older projects sound the same.
  - **A classic model** (Ladder, MS-20, TB-303...) runs the same code as Filter 1, at the Master Filter's Type and Slope. Low Pass at the model's own slope is the original circuit. Its Drive is the model's own input stage.
  - **A character model** works as before.
  - Switching between filters crossfades over 10 ms.
- **Type and Slope:** they stay Low Pass, High Pass, Band Pass or Notch at 12 or 24 dB. Peak, All Pass, 6 and 18 dB aren't added, so existing automation of these two settings keeps its meaning.
- Cutoff, Resonance, Drive, Mix and Output are Mod Matrix destinations. Because the filter comes after all the notes are mixed, routes onto it follow the newest note's sources, or the mod wheel, macros, aftertouch, CCs and the sidechain when no note is held.
- **Output** (-24 to +12 dB, 0 dB = untouched) is the last thing before the plug-in's output. **Master Volume** stays where it has always been, before the effects, so older patches with drive or clipping effects sound exactly the same.
- Not part of Mutate or the scenes' Output level; saved in patches, projects, undo and A/B. Older patches load with the filter off and Output at 0 dB.
- CPU: nothing when off; about 0.7% of one core at 24 dB with Drive on. The heaviest model (Analogue LP) is about 1% of a core. The Master Filter runs once for the whole mix, so it costs the same at every Quality.

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

## Random Patch (Lab > Random Patch, and Random in the header)

Makes a new patch from scratch, playable straight away, in a chosen style.

- **Category:** Any, or one of the ten browser categories (Reese, Sub & Bass, Neuro, Hoover & Rave, Stabs & Chords, Pads & Atmos, Leads, Breaks & Drums, FX & Risers, Keys & Plucks). Each has its own recipes: envelopes, polyphony, filter models, which engines suit it, effects and modulation.
- **Complexity:** Simple (one or two sources, few routes), Medium or Wild (more engines, a second filter, mutation, audio-rate tricks, more routes).
- **Seed** with **<** / **>**: the same seed, category, complexity, tick boxes and sample folders always make the same patch. **Generate** (or the header's **Random**) picks a new random seed and makes a patch; **<** / **>** make the patch for the previous or next seed, and typing a seed and pressing Enter makes that one.
- **What you get:** a name (e.g. "Rolling Pressure"), the category, tags ("random" plus the engines used), author "Random", and a note with the seed and complexity, so you can make it again. The four macros are named and wired: Filter, Bright, Drive and Space. The mod wheel opens the filter.
- **Levels** are matched: each patch is played through a short phrase for its category at 172 BPM and its Output gain set so it peaks around -12 dBFS.
- **One undo step.** **Keep current** puts the new patch in B and your current sound in A, so you can compare them with the A/B buttons.
- **Use** tick boxes: what the generator may use. **No oscillators** (Osc 1-3 and the Sub stay at 0), then Wavetables, Complex, SuperSaw, Granular Synth, Reese, Core A / B, Break Chopper, Note Mutation, Bus Mutation, Effects, Mod Matrix, Sequencers and Arp / Chord. Break Chopper and Sequencers are off by default.
- **Keep** tick boxes: 15 groups of the current patch that stay exactly as they are (the 13 Mutate groups, plus Samples and Breaks).
- **Sample folders:** **Breaks folder** and **Samples folder** (Choose / Clear / Rescan). The generator picks breaks (1-30 seconds long) and samples (for the wavetables and the Granular Synth) at random from them, subfolders included. They're scanned in the background, cached, and capped at 10,000 files each. The folders are an app setting (kept in `random_folders.json` in the patch folder), not part of patches. With no folder, breaks come from built-in synthesised breaks, and samples from a tone the generator makes.
- **What's saved:** the category, complexity, seed and tick boxes are saved with the project (not in patches). The patch it makes is an ordinary patch: save it like any other.
- **CPU:** the generator follows rules so patches stay playable (fewer unison voices on polyphonic sounds, no second Core slot, resonator or arp with the Core on polyphonic sounds, shorter Core releases with Operators only). Measured over 300 seeds on our build machine, every patch plays its phrase under 60% of one core on Normal quality (the heaviest is listed in the test output). Wild patches with reverb, Core and several engines are the heaviest.

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

There are 136 built-in presets: the first 36 (the last four play the Break Chopper), then a 100-preset jungle / drum & bass bank. The patch menu lists them under their categories, followed by your saved patches; *Browse* filters them by category, tag and author. The arrows step through both.
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
- **The jungle / drum & bass bank (100):**
  - Every preset has a category, tags and named macros (Macro 1, 2... on the Perform tab).
  - The levels are matched: each peaks at about -12 dBFS on its audition phrase (a 2-bar phrase at 172 BPM suited to its category), so you can flick through without jumps.
  - The Reese, Sub & Bass and Neuro presets keep everything under 120 Hz mono (Stereo Tools' Bass Mono).
  - Many use the new Reese engine and Break React. *Kick-Locked Reese* and *Kick-Locked Sub* react to the kicks of a break, or to C0.
  - The Breaks & Drums presets use the synthesised demo breaks, so no samples of anyone's record are included.
  - *Reese* (15): Amen Brother Reese, Rolling Quarter Reese, Dark Matter Reese, Tech Reese Growl, Liquid Reese, Steppa Square Reese, Blend Reese 174, Reese Fold Stack, Ghost Reese, Rotating Reese, Classic 94 Reese, Kick-Locked Reese, Hoover Reese Hybrid, Sub Heavy Reese, Halftime Wide Reese.
  - *Sub & Bass* (12): Pure Sine Sub, 808 Boom, Triangle Rumble, Square Sub Steppa, Warm Saw Bass, Jump Up Wobble Sub, Dub Pressure Sub, Plucky Sub, Ragga Bass, Kick-Locked Sub, Bubble Bass, Rollers Sub.
  - *Neuro* (12): Fold Machine, Comb Growl, Formant Talker, FM Snarl, Ring Mod Neuro, Bit Grinder, Phaser Wub, Spectral Neuro, Neuro Reese Sequence, Resampled Mid, Tearout Squeal, Robot Voice.
  - *Hoover & Rave* (8): Dominator 92, Mentasm Stab, Rave Organ, Hoover Glide Lead, Belgian Hoover, Alarm Hoover, Hardcore Hoover Chords, Arp Rave Pulse.
  - *Stabs & Chords* (10): Minor 9 Stab, Dub Chord Delay, Open Rave Stab, Sus Skank, Power Stab, Glass Major 7, Jungle Horn Stab, Stutter Chord, Octave Sampler Stab, Reese Chord Stab.
  - *Pads & Atmos* (10): Liquid Strings, Dark Atmos, Shimmer Heaven, Granular Haze, Spectral Choir Pad, Rainforest Pad, Amen Dream Pad, Detroit Strings, Ambient Reese Pad, Sub Drone.
  - *Leads* (8): Pirate Square, Sine Whistle, Acid 303 Lead, Saw Lead 95, Ragga Horn Lead, SP Crunch Lead, FM Lead, Formant Lead.
  - *Breaks & Drums* (10): Original Break 170, Two-Step Roller, Rollers 165 Swing, Chopped Frankenbreak, Reverse Edits, Halftime Break, SP Crunch Break, Break Keys 174, Fill Machine, Akai Cyclic Stretch.
  - *FX & Risers* (8): Noise Riser, Downlifter, Laser Zap, Dub Siren, Impact Boom, Radio Static, Reverse Swell, Alarm Siren.
  - *Keys & Plucks* (7): Rhodes Bed, Ragga Skank Pluck, Glass Bell, Marimba Roll, Liquid Pluck, Organ Bounce, Kalimba Drop.

## Quality (header)

- **Eco** - Wave Mutation without oversampling, Resonator capped at 6 modes, Granular at 24 grains, Spectral FFT at 1024
  (also lowers latency). Use it for big projects.
- **Normal** - as designed (2x oversampled Wave Mutation).
- **High** - 4x oversampled Wave Mutation for the cleanest folding and crushing at high notes.
- The Reese engine's Drive and the Core slots follow it too (none / x2 / x4).

Quality is your setting rather than part of the sound, so loading a preset keeps it.

## Mod depth with the mouse (Serum-style)

- **Making a route:**
  - Drag a source chip onto any knob that can be modulated. A new route at 25% appears and is ready to edit.
  - The chips are next to each source's title: LFO 1-4, the Amp, Filter and Mod Envelopes, MSEG 1-2, the eight macros (Perform and Macros & Scenes), and the XY pads' X and Y.
  - Right-click > Modulate with still works too.
- **The arcs:** each route on a knob is an arc just inside the knob's own arc, in its source's colour, up to three per knob from the outside in. Bipolar sources reach both ways. The white dot is what the newest note is hearing now.
- **Setting the depth:**
  - Drag an arc up for more depth, or down for less or negative. Ctrl/Cmd-drag anywhere on the knob does the same.
  - Shift gives fine steps.
  - While you drag, the knob shows the route, its depth and the value it takes the knob to.
  - Each drag is one Undo step, and the Mod Matrix page follows live (and the other way round).
- **Other actions:** double-click an arc for depth 0, and Alt-click an arc to remove its route.
- **The chip on a knob:** the small chip in the knob's top-left corner names the route the mouse edits ("+" means there are more). Click it to go round them, or click a route's own arc.

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
