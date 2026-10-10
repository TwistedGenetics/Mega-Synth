// Deterministic "golden" renders used to prove a change leaves the sound untouched.
// Convolution reverbs are off (their impulse loads on a background thread) and the
// sequence is fixed, so two builds can be compared sample for sample.
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
    golden ("sequence", [] (MegaSynthProcessor& p)
    {
        const int notes[8] = { 0, 3, -1, 7, 10, 7, 3, 0 };
        for (int i = 0; i < 8; ++i) p.steps.set (i, Step { notes[i], i % 3 == 0 ? -1 : 0, i % 4 == 1 ? TieSlide : (notes[i] < 0 ? TieRest : TieNormal), i % 2 == 0 });
        setP (p, P_seqLength, 8.0f); setP (p, P_filterMode, 12.0f); setP (p, P_filterRes, 14.0f); setP (p, P_seqRun, 1.0f);
    }, {}, 3.0);
}
