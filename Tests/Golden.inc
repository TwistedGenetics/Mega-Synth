// Deterministic "golden" renders used to prove a change leaves the sound untouched.
// Convolution reverbs are off except in the "Wet" renders (which wait for the impulse to load),
// and the sequence is fixed, so two builds can be compared sample for sample.
if (juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_GOLDEN", {}).isNotEmpty())
{
    auto golden = [&] (const juce::String& name, std::function<void (MegaSynthProcessor&)> setup,
                       std::vector<std::pair<double, juce::MidiMessage>> ev, double secs)
    {
        auto p = make();
        for (int i : { P_reverbMix, P_shimmerMix, P_reverseMix }) setP (*p, i, 0.0f);
        setup (*p);
        // what loading a patch from before the filter overhaul does: the model at its own slope
        setP (*p, P_filterSlope, (float) nativeFilterSlope ((int) std::lround (p->param (P_filterMode)->convertFrom0to1 (p->param (P_filterMode)->getValue()))));
        juce::AudioBuffer<float> cap;
        render (*p, secs, ev, &cap);
        writeWav (cap, sr, "golden_" + name + ".wav");
    };
    golden ("default", [] (MegaSynthProcessor&) {}, chord (0.05, 1.2, { 48, 52, 55, 60 }), 2.0);
    // The convolution reverbs load their impulse on a background thread, so these renders first run
    // silence (with the wall clock moving) until it's in place and any crossfade has finished.
    auto goldenWet = [&] (const juce::String& name, std::function<void (MegaSynthProcessor&)> setup,
                          std::vector<std::pair<double, juce::MidiMessage>> ev, double secs)
    {
        auto p = make();
        setup (*p);
        setP (*p, P_filterSlope, (float) nativeFilterSlope ((int) std::lround (p->param (P_filterMode)->convertFrom0to1 (p->param (P_filterMode)->getValue()))));
        juce::AudioBuffer<float> blk (2, 512);
        for (int b = 0; b < 300; ++b)
        {
            blk.clear(); juce::MidiBuffer none; p->processBlock (blk, none);
            juce::Thread::sleep (3);
        }
        juce::AudioBuffer<float> cap;
        render (*p, secs, ev, &cap);
        writeWav (cap, sr, "golden_" + name + ".wav");
    };
    goldenWet ("reverb", [] (MegaSynthProcessor& p) { setP (p, P_reverbMix, 0.6f); setP (p, P_shimmerMix, 0.0f); setP (p, P_reverseMix, 0.0f); setP (p, P_reverbSize, 2.0f); },
               chord (0.05, 0.6, { 48, 55, 63 }), 3.0);
    goldenWet ("shimmer_reverse", [] (MegaSynthProcessor& p) { setP (p, P_reverbMix, 0.0f); setP (p, P_shimmerMix, 0.5f); setP (p, P_reverseMix, 0.5f); },
               chord (0.05, 0.6, { 52, 59, 64 }), 3.0);
    for (int mode = 0; mode < 17; ++mode)
        golden ("filter" + juce::String (mode), [mode] (MegaSynthProcessor& p) { setP (p, P_filterMode, (float) mode); setP (p, P_filterRes, 12.0f); },
                chord (0.02, 0.6, { 36, 43 }), 0.9);
    golden ("fmring", [] (MegaSynthProcessor& p)
    {
        setP (p, P_fmAmount, 600); setP (p, P_fmSlot2Amt, 300); setP (p, P_ringMix, 0.6f); setP (p, P_ringSlot1Amt, 0.7f);
        setP (p, P_lfoAssignTarget1, (float) MT_complexFm); setP (p, P_lfoAssignAmt1, 0.5f);
        setP (p, P_envAssignTarget1, (float) MT_supersawSpread); setP (p, P_envAssignAmt1, 0.6f);
        setP (p, P_delaySync, 6); setP (p, P_chorusMix, 0.5f);
    }, chord (0.05, 1.2, { 45, 57, 64 }), 2.0);
    // the DNA Sequencer as it was before its update (internal clock, and the Free rate)
    for (int free = 0; free < 2; ++free)
        golden (free ? "dnaseq_free" : "dnaseq", [free] (MegaSynthProcessor& p)
        {
            const int t[8] = { DT_Fold, DT_Off, DT_Crush, DT_Filter, DT_Ring, DT_Off, DT_Splice, DT_Octave };
            for (int i = 0; i < 8; ++i) p.dnaSteps.set (i, t[i], 0.4f + 0.07f * i);
            setP (p, P_dsOn, 1.0f); setP (p, P_dsSteps, 8.0f); setP (p, P_dsGlide, 0.3f);
            setP (p, P_dsRate, free ? 8.0f : 2.0f); setP (p, P_dsFreeHz, 7.0f);
        }, chord (0.05, 1.6, { 45, 57 }), 2.0);
    golden ("sequence", [] (MegaSynthProcessor& p)
    {
        const int notes[8] = { 0, 3, -1, 7, 10, 7, 3, 0 };
        for (int i = 0; i < 8; ++i) p.steps.set (i, Step { notes[i], i % 3 == 0 ? -1 : 0, i % 4 == 1 ? TieSlide : (notes[i] < 0 ? TieRest : TieNormal), i % 2 == 0 });
        setP (p, P_seqLength, 8.0f); setP (p, P_filterMode, 12.0f); setP (p, P_filterRes, 14.0f); setP (p, P_seqRun, 1.0f);
    }, {}, 3.0);
}
