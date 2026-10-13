#include "FilterPanel.h"
#include "../PluginEditor.h"
#include "../ParamFormat.h"

using namespace tg;

namespace tgui
{

const FilterParams& filterParams (int w)
{
    static const FilterParams f[2] = {
        { P_filterMode, P_filterType, P_filterSlope, P_filterCutoff, P_filterRes, P_filterDrive, P_filterMix, P_fEnvAmt, P_filterLfoAmt, P_filterLfoSrc, P_filterKeyTrack, P_flt1Char, P_flt1CharAmt },
        { P_filter2Mode, P_filter2Type, P_filter2Slope, P_filter2Cutoff, P_filter2Res, P_filter2Drive, P_filter2Mix, P_filter2EnvAmt, P_filter2LfoAmt, P_filter2LfoSrc, P_filter2KeyTrack, P_flt2Char, P_flt2CharAmt } };
    return f[w & 1];
}

static const juce::Colour kF1 = col::filter;   // Filter 1: violet
static const juce::Colour kF2 = col::fm;       // Filter 2: cyan

//==============================================================================
Segmented::Segmented (MegaSynthProcessor& p, int idx, juce::StringArray l, juce::Colour c)
    : proc (p), param (idx), labels (std::move (l)), colour (c) {}

int Segmented::segmentAt (juce::Point<int> pt) const
{
    if (labels.isEmpty() || ! getLocalBounds().contains (pt)) return -1;
    return juce::jlimit (0, labels.size() - 1, pt.x * labels.size() / std::max (1, getWidth()));
}

void Segmented::paint (juce::Graphics& g)
{
    const int n = labels.size();
    const float w = (float) getWidth() / (float) n, h = (float) getHeight();
    int sel = local;
    if (param >= 0) { auto* prm = proc.param (param); sel = juce::roundToInt (prm->convertFrom0to1 (prm->getValue())); }
    for (int i = 0; i < n; ++i)
    {
        auto r = juce::Rectangle<float> (i * w, 0.0f, w, h).reduced (1.5f, 0.5f);
        const bool allowed = mask < 0 || (mask & (1 << i)) != 0;
        const bool on = i == (shown >= 0 ? shown : sel) && allowed;
        g.setColour (on ? colour : (i == hover && allowed ? col::panel3.brighter (0.08f) : col::panel2));
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (on ? colour.brighter (0.3f) : col::border);
        g.drawRoundedRectangle (r, 6.0f, on ? 1.5f : 1.0f);
        // the stored choice, when the type can't use it, gets a dotted outline
        if (! on && i == sel && shown >= 0 && shown != sel)
        {
            g.setColour (colour.withAlpha (0.7f));
            const float dashes[] = { 3.0f, 3.0f };
            juce::Path p; p.addRoundedRectangle (r.reduced (2.0f), 5.0f);
            juce::Path d; juce::PathStrokeType (1.0f).createDashedStroke (d, p, dashes, 2);
            g.fillPath (d);
        }
        g.setColour (on ? juce::Colours::white : (allowed ? col::text : col::muted.withAlpha (0.35f)));
        g.setFont (juce::Font (juce::FontOptions (12.5f, on ? juce::Font::bold : juce::Font::plain)));
        const bool small = (cornerText != nullptr || footText != nullptr) && r.getHeight() >= 26.0f;
        g.drawFittedText (labels[i], (small ? r.withTrimmedBottom (8.0f).withTrimmedTop (1.0f) : r).toNearestInt(), juce::Justification::centred, 1, 0.72f);   // (squeezed a little rather than cut short)
        if (small)
        {
            g.setFont (juce::Font (juce::FontOptions (8.5f, juce::Font::bold)));
            const auto ink = on ? juce::Colours::white.withAlpha (0.9f) : col::muted;
            if (cornerText != nullptr) { const auto t = cornerText (i); if (t.isNotEmpty()) { g.setColour (ink); g.drawText (t, r.reduced (3.0f, 1.0f).toNearestInt(), juce::Justification::topRight, false); } }
            if (footText != nullptr) { const auto t = footText (i); if (t.isNotEmpty()) { g.setColour (on ? juce::Colours::white.withAlpha (0.85f) : col::seq.brighter (0.2f)); g.drawFittedText (t, r.reduced (2.0f, 1.0f).toNearestInt(), juce::Justification::centredBottom, 1, 0.6f); } }
        }
    }
}

void Segmented::mouseMove (const juce::MouseEvent& e)
{
    const int s = segmentAt (e.getPosition());
    if (s != hover) { hover = s; repaint(); }
}

void Segmented::mouseDown (const juce::MouseEvent& e)
{
    const int s = segmentAt (e.getPosition());
    if (s < 0 || (mask >= 0 && (mask & (1 << s)) == 0)) return;
    if (e.mods.isPopupMenu() && onRightClick) { onRightClick (s); return; }
    if (param < 0)
    {
        local = s;
        if (onSelect) onSelect (s);
    }
    else
    {
        auto* prm = proc.param (param);
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 ((float) s));
        prm->endChangeGesture();
    }
    repaint();
}

//==============================================================================
// ids: Clean = 1 (Master Filter only); classic model m = 100 + m; character model c = 200 + c
FilterPicker::FilterPicker (MegaSynthProcessor& p, int mp, int cp, bool m) : proc (p), modelParam (mp), charParam (cp), master (m)
{
    struct Entry { int id; const char* name; };
    auto classic = [] (int k) { return Entry { 100 + k, kFilterLabels[k] }; };
    auto charM = [] (int c) { return Entry { 200 + c, charModelInfo (c).name }; };
    auto group = [this] (const char* heading, std::initializer_list<Entry> items)
    {
        addSectionHeading (heading);
        for (auto& e : items) addItem (e.name, e.id);
    };
    if (master) group ("Clean", { Entry { 1, "Clean (SVF)" }, classic (0), classic (1), classic (4), classic (7) });
    else group ("Clean", { classic (0), classic (1), classic (4), classic (7) });
    group ("Analogue models", { classic (2), classic (3), classic (5), classic (6), classic (9), classic (10), classic (11), classic (13), classic (14), classic (15), classic (16) });
    group ("Acid", { classic (8), classic (12) });
    group ("Drive & character", { charM (CM_PowerLP), charM (CM_PowerBP), charM (CM_WaveShaper), charM (CM_AnalogueLP), charM (CM_DJ) });
    group ("Formant & vowel", { charM (CM_Formant), charM (CM_Formant2), charM (CM_VowelMorph) });
    group ("Comb, phaser & peaks", { charM (CM_AllPass), charM (CM_Resonator), charM (CM_Comb), charM (CM_Phaser), charM (CM_DualPeak) });
    group ("Digital / lo-fi", { charM (CM_Resampler), charM (CM_LPResample), charM (CM_Shifter), charM (CM_Ring), charM (CM_DecimatorLP) });
    getProperties().set ("megaParam", modelParam);
    getProperties().set ("megaParam2", charParam);
    setTooltip (master ? "The Master Filter: Clean (the state-variable filter, as before), one of the classic models of Filter 1 and 2, "
                         "or a character model. Controls a filter doesn't use are greyed out."
                       : "The filter: a classic model (as a Low Pass at its own slope it is the original circuit; with other types and "
                         "slopes it gives the multimode filter its character) or a character model, which replaces Type and Slope. "
                         "Controls a filter doesn't use are greyed out.");
    onChange = [this] { picked(); };
    sync();
    startTimerHz (10);
}

int FilterPicker::idFor (bool master, int model, int charModel)
{
    if (charModel > 0) return 200 + charModel;
    if (master) return model <= 0 ? 1 : 100 + model - 1;
    return 100 + juce::jlimit (0, 16, model);
}

void FilterPicker::sync()
{
    auto plain = [this] (int i) { auto* prm = proc.param (i); return juce::roundToInt (prm->convertFrom0to1 (prm->getValue())); };
    const int id = idFor (master, plain (modelParam), plain (charParam));
    if (getSelectedId() != id) setSelectedId (id, juce::dontSendNotification);
}

void FilterPicker::picked()
{
    const int id = getSelectedId();
    if (id <= 0) return;
    auto set = [this] (int i, float v)
    {
        auto* prm = proc.param (i);
        if (std::abs (prm->convertFrom0to1 (prm->getValue()) - v) < 1.0e-4f) return;
        prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->convertTo0to1 (v)); prm->endChangeGesture();
    };
    if (id >= 200) { set (charParam, (float) (id - 200)); return; }   // a character model (the model underneath stays)
    set (charParam, 0.0f);
    if (master) { set (modelParam, id == 1 ? 0.0f : (float) (id - 100 + 1)); return; }
    const int m = id - 100;
    set (modelParam, (float) m);
    const auto& f = filterParams (modelParam == P_filter2Mode ? 1 : 0);
    set (f.slope, (float) nativeFilterSlope (m));   // (choosing a model also selects its own slope, as before)
}

//==============================================================================
void FilterResponse::update (int selected)
{
    auto pv = [this] (int i) { auto* p = proc.param (i); return p->convertFrom0to1 (p->getValue()); };
    juce::String key;
    for (int w = 0; w < 2; ++w)
    {
        const auto& f = filterParams (w);
        for (int i : { f.mode, f.type, f.slope, f.cutoff, f.res, f.mix, f.charModel, f.charAmt }) key << juce::String (pv (i), 2) << "|";
    }
    for (int i : { P_filter2On, P_filterRouting, P_filterStereoSplit, P_filterBalance, P_warmth, P_bassKeep }) key << juce::String (pv (i), 2) << "|";
    key << selected;
    if (key == lastKey) return;
    lastKey = key;
    sel = selected;

    const bool on2 = pv (P_filter2On) > 0.5f;
    routing = ! on2 ? 0 : (juce::roundToInt (pv (P_filterRouting)) == 0 ? 1 : (pv (P_filterStereoSplit) > 0.5f ? 3 : 2));
    const float bal = pv (P_filterBalance);
    const float gA = std::min (1.0f, 2.0f * (1.0f - bal)), gB = std::min (1.0f, 2.0f * bal);

    // impulse responses of the real filter code at a tiny level (so the drive stages stay linear)
    const double fs = 48000.0;
    constexpr int order = 13, N = 1 << order;
    FilterUnit u[2];
    for (int w = 0; w < 2; ++w)
    {
        const auto& f = filterParams (w);
        u[w].setPreview (true);
        u[w].setAnalog (pv (P_warmth), pv (P_bassKeep));
        const int cm = juce::roundToInt (pv (f.charModel));
        u[w].setCharacter (pv (f.charAmt));
        u[w].configure (juce::roundToInt (pv (f.mode)), juce::roundToInt (pv (f.type)), juce::roundToInt (pv (f.slope)), true, cm);
        noCurve[w] = cm > 0 && charModelInfo (cm).curveApprox;
        u[w].setDrive (1.0f);   // the curve shows the filter; Drive adds saturation on top
        u[w].setMix (pv (f.mix));
        u[w].update (pv (f.cutoff), pv (f.res), fs);
        cutoff[w] = pv (f.cutoff);
    }
    FilterUnit serial2 = u[1];
    std::vector<float> ir[3];
    for (auto& v : ir) v.assign ((size_t) N * 2, 0.0f);
    const float amp = 1.0e-4f;
    for (int i = 0; i < N; ++i)
    {
        const float x = i == 0 ? amp : 0.0f;
        float a, b, c, d;
        u[0].processFrame (x, x, false, a, d);
        u[1].processFrame (x, x, false, b, d);
        ir[0][(size_t) i] = a / amp;
        ir[1][(size_t) i] = b / amp;
        if (routing == 1) { serial2.processFrame (a, a, false, c, d); ir[2][(size_t) i] = c / amp; }
        else if (routing == 2) ir[2][(size_t) i] = (gA * a + gB * b) / amp;
        else ir[2][(size_t) i] = a / amp;
    }
    juce::dsp::FFT fft (order);
    for (int k = 0; k < 3; ++k)
    {
        fft.performFrequencyOnlyForwardTransform (ir[k].data());
        mag[k].assign ((size_t) N / 2, -100.0f);
        for (int b = 1; b < N / 2; ++b) mag[k][(size_t) b] = juce::Decibels::gainToDecibels (ir[k][(size_t) b], -100.0f);
    }

    auto describe = [&] (int w)
    {
        const auto& f = filterParams (w);
        const int m = juce::roundToInt (pv (f.mode)), t = juce::roundToInt (pv (f.type)), s = juce::roundToInt (pv (f.slope));
        const int eff = effectiveFilterSlope (t, s);
        if (const int cm = juce::roundToInt (pv (f.charModel)); cm > 0)
            return juce::String (charModelInfo (cm).name) + (charModelInfo (cm).curveApprox ? " (no fixed curve)" : "");
        juce::String d = kFilterTypeKeys[juce::jlimit (0, 5, t)];
        d = d.toUpperCase();
        if (filterSlopeMask (t) != 0) d << " " << kFilterSlopeKeys[eff];
        d << " " << kFilterLabels[juce::jlimit (0, 16, m)];
        if (t == FT_LP && eff == nativeFilterSlope (m)) d << " (classic)";
        return d;
    };
    switch (routing)
    {
        case 0:  caption = describe (0) + "   |   Filter 2 off"; break;
        case 1:  caption = "Serial:  " + describe (0) + "  >  " + describe (1); break;
        case 2:  caption = "Parallel:  " + describe (0) + "  +  " + describe (1); break;
        default: caption = "Stereo split:  L " + describe (0) + "   R " + describe (1); break;
    }
    repaint();
}

void FilterResponse::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (col::panel2);
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (col::border);
    g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
    auto plot = r.reduced (34.0f, 22.0f).withTrimmedTop (4.0f);
    const float lo = 20.0f, hi = 20000.0f, dbTop = 24.0f, dbBot = -48.0f;
    auto xOf = [&] (float f) { return plot.getX() + plot.getWidth() * std::log (f / lo) / std::log (hi / lo); };
    auto yOf = [&] (float db) { return plot.getY() + plot.getHeight() * (dbTop - juce::jlimit (dbBot, dbTop, db)) / (dbTop - dbBot); };

    g.setFont (juce::Font (juce::FontOptions (10.5f)));
    for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        g.setColour (col::border.withAlpha (0.6f));
        g.drawVerticalLine ((int) xOf (f), plot.getY(), plot.getBottom());
        g.setColour (col::muted);
        g.drawText (f >= 1000 ? juce::String ((int) (f / 1000)) + "k" : juce::String ((int) f), juce::Rectangle<float> (xOf (f) - 20, plot.getBottom() + 2, 40, 14), juce::Justification::centred);
    }
    for (float db : { 12.0f, 0.0f, -12.0f, -24.0f, -36.0f })
    {
        g.setColour (db == 0.0f ? col::muted.withAlpha (0.6f) : col::border.withAlpha (0.6f));
        g.drawHorizontalLine ((int) yOf (db), plot.getX(), plot.getRight());
        g.setColour (col::muted);
        g.drawText ((db > 0 ? "+" : "") + juce::String ((int) db), juce::Rectangle<float> (r.getX() + 2, yOf (db) - 7, 28, 14), juce::Justification::centredRight);
    }

    auto curve = [&] (const std::vector<float>& m)
    {
        juce::Path p;
        if (m.empty()) return p;
        const double fs = 48000.0;
        const int N = (int) m.size() * 2;
        for (int px = 0; px <= (int) plot.getWidth(); px += 2)
        {
            const float f = lo * std::pow (hi / lo, px / plot.getWidth());
            const float bin = (float) (f * N / fs);
            const int k = juce::jlimit (1, (int) m.size() - 2, (int) bin);
            const float fr = juce::jlimit (0.0f, 1.0f, bin - (float) k);
            const float db = m[(size_t) k] + (m[(size_t) k + 1] - m[(size_t) k]) * fr;
            if (px == 0) p.startNewSubPath (plot.getX(), yOf (db)); else p.lineTo (plot.getX() + (float) px, yOf (db));
        }
        return p;
    };
    auto fillUnder = [&] (const juce::Path& p, juce::Colour c)
    {
        juce::Path f (p);
        f.lineTo (plot.getRight(), plot.getBottom()); f.lineTo (plot.getX(), plot.getBottom()); f.closeSubPath();
        g.setColour (c.withAlpha (0.16f));
        g.fillPath (f);
    };
    const juce::Colour cols[2] = { kF1, kF2 };
    if (noCurve[0] || (routing != 0 && noCurve[1]))
    {
        // a model that changes over time (resampling, shifting, ring modulation): there's no fixed curve to draw
        g.setColour (col::muted);
        g.setFont (juce::Font (juce::FontOptions (12.0f)));
        g.drawFittedText ("This character model changes the sound over time\n(sample rate, frequency shift or ring modulation),\nso it has no fixed curve. Listen instead!",
                          plot.toNearestInt().reduced (10), juce::Justification::centred, 3);
    }
    else if (routing == 0)
    {
        auto p = curve (mag[0]);
        fillUnder (p, kF1);
        g.setColour (kF1.brighter (0.25f)); g.strokePath (p, juce::PathStrokeType (2.0f));
    }
    else if (routing == 3)
    {
        for (int w = 0; w < 2; ++w)
        {
            auto p = curve (mag[w]);
            fillUnder (p, cols[w]);
            g.setColour (cols[w].brighter (0.25f)); g.strokePath (p, juce::PathStrokeType (w == sel ? 2.4f : 1.6f));
        }
    }
    else
    {
        for (int w = 0; w < 2; ++w)
        {
            g.setColour (cols[w].withAlpha (w == sel ? 0.75f : 0.45f));
            g.strokePath (curve (mag[w]), juce::PathStrokeType (1.2f));
        }
        auto p = curve (mag[2]);
        fillUnder (p, col::text);
        g.setColour (col::text); g.strokePath (p, juce::PathStrokeType (2.2f));
    }
    // cutoff markers
    const float dashes[] = { 4.0f, 3.0f };
    for (int w = 0; w < (routing == 0 ? 1 : 2); ++w)
    {
        const float cx = xOf (juce::jlimit (lo, hi, cutoff[w]));
        g.setColour (cols[w].withAlpha (w == sel ? 0.95f : 0.55f));
        g.drawDashedLine (juce::Line<float> (cx, plot.getY(), cx, plot.getBottom()), dashes, 2, 1.0f);
        g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        g.drawText (juce::String (w + 1) + ": " + formatParam (w == 0 ? P_filterCutoff : P_filter2Cutoff, cutoff[w]),
                    juce::Rectangle<float> (juce::jlimit (plot.getX(), plot.getRight() - 84, cx + 4), plot.getY() + w * 14.0f, 84, 14), juce::Justification::centredLeft);
    }
    g.setColour (col::muted);
    g.setFont (juce::Font (juce::FontOptions (11.5f)));
    g.drawFittedText (caption, juce::Rectangle<int> ((int) r.getX() + 10, (int) r.getY() + 3, (int) r.getWidth() - 20, 16), juce::Justification::centredLeft, 1, 0.8f);
}

//==============================================================================
FilterPanel::FilterPanel (MegaSynthProcessor& p)
    : proc (p),
      which (p, -1, { "FILTER 1", "FILTER 2" }, kF1),
      routing (p, P_filterRouting, { "SERIAL", "PARALLEL" }, col::accent),
      response (p)
{
    for (auto* l : { &typeCap, &slopeCap, &modelCap, &lfoCap })
    {
        l->setColour (juce::Label::textColourId, col::muted);
        l->setFont (juce::Font (juce::FontOptions (11.5f)));
        addAndMakeVisible (*l);
    }
    typeCap.setText ("Type", juce::dontSendNotification);
    slopeCap.setText ("Slope (dB / octave)", juce::dontSendNotification);
    modelCap.setText ("Filter", juce::dontSendNotification);
    lfoCap.setText ("LFO", juce::dontSendNotification);
    info.setColour (juce::Label::textColourId, col::muted);
    info.setFont (juce::Font (juce::FontOptions (11.5f)));
    info.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (info);

    which.setTooltip ("Which filter the controls below show");
    which.onSelect = [this] (int w) { show (w); };
    addAndMakeVisible (which);
    routing.setTooltip ("Serial: Filter 1 then Filter 2.  Parallel: both get the same sound and are added (Balance sets the mix).");
    addAndMakeVisible (routing);
    f2OnAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, kParamIds[P_filter2On], tagParam (f2On, P_filter2On));
    splitAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, kParamIds[P_filterStereoSplit], tagParam (split, P_filterStereoSplit));
    f2On.setTooltip ("Switch Filter 2 in. Off, it uses no CPU and the sound is Filter 1 alone.");
    split.setTooltip ("Parallel only: Filter 1 on the left, Filter 2 on the right (Balance tilts between them)");
    addAndMakeVisible (f2On);
    addAndMakeVisible (split);
    copyBtn.setTooltip ("Filter 2 takes all of Filter 1's settings (undoable)");
    copyBtn.onClick = [this] { proc.copyFilter1To2(); };
    addAndMakeVisible (copyBtn);
    balance = std::make_unique<Knob> (proc, P_filterBalance, "Balance", col::accent);
    balance->slider.setTooltip ("Parallel / stereo split: Filter 1 <> Filter 2. In the middle both play at full level.");
    addAndMakeVisible (*balance);


    for (int w = 0; w < 2; ++w)
    {
        const auto& f = filterParams (w);
        const auto c = w == 0 ? kF1 : kF2;
        auto* t = type.add (new Segmented (proc, f.type, { "LP", "HP", "BP", "NOTCH", "PEAK", "AP" }, c));
        t->setTooltip ("Low Pass, High Pass, Band Pass, Notch, Peak / Bell, All Pass");
        auto* s = slope.add (new Segmented (proc, f.slope, { "6", "12", "18", "24" }, c));
        s->setTooltip ("Steepness. Band Pass and Notch: 12 or 24. Peak / Bell has no slope. "
                       "Low Pass at the model's own slope runs the classic circuit exactly.");
        addChildComponent (t);
        addChildComponent (s);
        auto* box = lfoSrc.add (new juce::ComboBox());
        for (int i = 0; i < kListFilterLfo.size; ++i) box->addItem (kListFilterLfo.labels[i], i + 1);
        lfoAtt.add (new juce::AudioProcessorValueTreeState::ComboBoxAttachment (proc.apvts, kParamIds[f.lfoSrc], tagParam (*box, f.lfoSrc)));
        box->setTooltip ("Which LFO LFO Amt uses (rate, shape and depth are on Modulation > LFOs)");
        addChildComponent (box);

        addChildComponent (picker.add (new FilterPicker (proc, f.mode, f.charModel, false)));
        const std::pair<int, const char*> ks[] = { { f.cutoff, "Cutoff" }, { f.res, "Resonance" }, { f.drive, "Drive" }, { f.mix, "Mix" },
                                                   { f.env, "Env Amt" }, { f.lfoAmt, "LFO Amt" }, { f.keyTrack, "Key Track" }, { f.charAmt, "Character" } };
        for (auto& [idx, cap] : ks)
        {
            auto* k = knobs[w].add (new Knob (proc, idx, cap, c));
            addChildComponent (k);
        }
        knobs[w][0]->slider.setTooltip ("Cutoff, 20 Hz - 20 kHz (equal travel per octave)");
        knobs[w][2]->slider.setTooltip ("0 dB = clean. Turning it up pushes the signal into the filter: saturation, thicker bass, more bite (fully in by +12 dB). Level is compensated.");
        knobs[w][3]->slider.setTooltip ("Mix: 0% = unfiltered, 100% = fully filtered (parallel filtering in between)");
        knobs[w][4]->slider.setTooltip ("Filter envelope amount, -100% .. +100% (100% = +10 kHz at the envelope's peak). Negative inverts the sweep. Both filters share the Filter Envelope.");
        knobs[w][5]->slider.setTooltip ("LFO to cutoff, -100% .. +100% (100% = +-4 octaves)");
        knobs[w][6]->slider.setTooltip ("Key tracking: 0% = cutoff fixed, 100% = cutoff follows the keyboard (from middle C)");
    }

    addAndMakeVisible (response);
    show (0);
    startTimerHz (20);
}

FilterPanel::~FilterPanel() { stopTimer(); }

float FilterPanel::plain (int idx) const { auto* p = proc.param (idx); return p->convertFrom0to1 (p->getValue()); }

void FilterPanel::setParam (int idx, float v)
{
    auto* p = proc.param (idx);
    p->beginChangeGesture();
    p->setValueNotifyingHost (p->convertTo0to1 (v));
    p->endChangeGesture();
}

void FilterPanel::show (int w)
{
    sel = w & 1;
    which.setColour (sel == 0 ? kF1 : kF2);
    for (int i = 0; i < 2; ++i)
    {
        const bool v = i == sel;
        type[i]->setVisible (v); slope[i]->setVisible (v); lfoSrc[i]->setVisible (v); picker[i]->setVisible (v);
        for (auto* k : knobs[i]) k->setVisible (v);
    }
    sync();
}

void FilterPanel::timerCallback()
{
    if (isShowing()) sync();
}

void FilterPanel::sync()
{
    const auto& f = filterParams (sel);
    const int t = juce::roundToInt (plain (f.type)), s = juce::roundToInt (plain (f.slope)), m = juce::roundToInt (plain (f.mode));
    slope[sel]->setMask (filterSlopeMask (t));
    slope[sel]->setShown (filterSlopeMask (t) == 0 ? -2 : effectiveFilterSlope (t, s));
    type[sel]->repaint(); slope[sel]->repaint(); routing.repaint();
    picker[sel]->sync();

    const bool on2 = plain (P_filter2On) > 0.5f, parallel = juce::roundToInt (plain (P_filterRouting)) == 1;
    routing.setAlpha (on2 ? 1.0f : 0.45f);
    split.setEnabled (parallel);
    split.setAlpha (on2 && parallel ? 1.0f : 0.45f);
    balance->setAlpha (on2 && parallel ? 1.0f : 0.45f);
    // the controls of a filter that's switched off are dimmed (they still work)
    const float a = sel == 1 && ! on2 ? 0.5f : 1.0f;
    for (auto* k : knobs[sel]) k->setAlpha (a);
    type[sel]->setAlpha (a); slope[sel]->setAlpha (a); picker[sel]->setAlpha (a);

    const bool classic = t == FT_LP && effectiveFilterSlope (t, s) == nativeFilterSlope (m);
    const int cm = juce::roundToInt (plain (f.charModel));
    // a character model replaces the Model / Type / Slope: lock those, explain the model and its Character knob
    type[sel]->setMask (cm > 0 ? 0 : -1);
    slope[sel]->setMask (cm > 0 ? 0 : filterSlopeMask (t));
    if (cm > 0) slope[sel]->setShown (-2);
    type[sel]->setAlpha (cm > 0 ? 0.35f : a); slope[sel]->setAlpha (cm > 0 ? 0.35f : a);
    auto* ck = knobs[sel][7];
    ck->setAlpha (cm > 0 ? a : 0.4f);
    ck->slider.setTooltip (cm > 0 ? juce::String ("Character: ") + charModelInfo (cm).character : juce::String ("Character: only the character models use it (this filter doesn't)"));
    // the controls the chosen filter doesn't use are greyed out (they still keep their settings)
    const bool resUsed = cm == 0 || charModelInfo (cm).usesRes, cutUsed = cm != CM_DJ;
    knobs[sel][1]->setAlpha (resUsed ? a : 0.4f);
    knobs[sel][0]->setAlpha (cutUsed ? a : 0.4f);
    for (int i : { 4, 5, 6 }) knobs[sel][i]->setAlpha (cutUsed ? a : 0.4f);   // (envelope, LFO and key tracking move the cutoff)
    knobs[sel][1]->slider.setTooltip (resUsed ? juce::String ("Resonance") : juce::String ("Resonance: this filter doesn't use it"));
    juce::String txt;
    if (sel == 1 && ! on2) txt = "Filter 2 is off: switch it on above.\n";
    if (cm > 0)
    {
        txt << charModelInfo (cm).about << "  Character: " << charModelInfo (cm).character << ".";
        if (charModelInfo (cm).curveApprox) txt << " (Changes over time: no fixed curve.)";
        info.setText (txt, juce::dontSendNotification);
        response.update (sel);
        return;
    }
    if (t == FT_PEAK) txt << "Peak / Bell: Resonance sets the boost (+2 to +18 dB) and its width.";
    else if (classic) txt << "Classic circuit: the original model, exactly.";
    else txt << "Multimode filter with the model's character.";
    if (t == FT_LP && effectiveFilterSlope (t, s) == 3 && ! classic && ! (sel == 1 && ! on2)) txt << "\nSelf-oscillates at full Resonance.";
    info.setText (txt, juce::dontSendNotification);
    response.update (sel);
}

void FilterPanel::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (col::panel);
    g.fillRoundedRectangle (r, 12.0f);
    g.setColour (col::border);
    g.drawRoundedRectangle (r, 12.0f, 1.0f);
    g.setColour (sel == 0 ? kF1 : kF2);
    g.fillRoundedRectangle (juce::Rectangle<float> (r.getX() + 12, r.getY() + 9, 4, 16), 2.0f);
    g.setColour (col::text);
    g.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
    g.drawText ("Filter", juce::Rectangle<float> (r.getX() + 22, r.getY() + 6, 60, 22), juce::Justification::centredLeft);
    g.setColour (col::border);
    g.drawVerticalLine (334, 168.0f, 246.0f);
    g.drawVerticalLine (676, 168.0f, 246.0f);
    g.setColour (col::muted);
    g.setFont (juce::Font (juce::FontOptions (10.5f, juce::Font::bold)));
    g.drawText ("MAIN", juce::Rectangle<float> (16, 154, 120, 12), juce::Justification::centredLeft);
    g.drawText ("MODULATION", juce::Rectangle<float> (340, 154, 160, 12), juce::Justification::centredLeft);
    g.drawText ("ROUTING", juce::Rectangle<float> (686, 154, 80, 12), juce::Justification::centredLeft);
}

void FilterPanel::resized()
{
    // header: which filter, Filter 2 On, routing, stereo split, copy
    which.setBounds (80, 6, 210, 26);
    f2On.setBounds (304, 7, 110, 24);
    routing.setBounds (420, 6, 200, 26);
    split.setBounds (630, 7, 112, 24);
    copyBtn.setBounds (750, 6, 96, 26);

    typeCap.setBounds (14, 40, 200, 16);
    slopeCap.setBounds (418, 40, 200, 16);
    modelCap.setBounds (14, 98, 160, 16);
    info.setBounds (300, 98, 460, 52);
    for (int w = 0; w < 2; ++w)
    {
        type[w]->setBounds (14, 57, 390, 30);
        slope[w]->setBounds (418, 57, 252, 30);
        picker[w]->setBounds (14, 115, 270, 26);
        int x = 10;
        for (int i : { 0, 1, 2, 3, 7 }) { knobs[w][i]->setBounds (x, 168, 62, 84); x += 64; }
        x = 340;
        knobs[w][4]->setBounds (x, 168, 72, 84); x += 76;
        knobs[w][5]->setBounds (x, 168, 72, 84); x += 76;
        lfoSrc[w]->setBounds (x, 186, 80, 26);
        knobs[w][6]->setBounds (x + 86, 168, 72, 84);
    }
    lfoCap.setBounds (492, 168, 80, 16);
    balance->setBounds (684, 168, 72, 84);
    response.setBounds (770, 40, getWidth() - 784, getHeight() - 54);
}

} // namespace tgui
