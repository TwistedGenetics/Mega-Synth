#include "FilterPanel.h"
#include "../PluginEditor.h"
#include "../ParamFormat.h"

using namespace tg;

namespace tgui
{

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
    auto* prm = proc.param (param);
    const int sel = juce::roundToInt (prm->convertFrom0to1 (prm->getValue()));
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
        g.drawText (labels[i], r, juce::Justification::centred);
    }
}

void Segmented::mouseMove (const juce::MouseEvent& e)
{
    const int s = segmentAt (e.getPosition());
    if (s != hover) { hover = s; repaint(); }
    if (tooltipFor && s >= 0) setTooltip (tooltipFor (s));
}

void Segmented::mouseDown (const juce::MouseEvent& e)
{
    const int s = segmentAt (e.getPosition());
    if (s < 0 || (mask >= 0 && (mask & (1 << s)) == 0)) return;
    auto* prm = proc.param (param);
    prm->beginChangeGesture();
    prm->setValueNotifyingHost (prm->convertTo0to1 ((float) s));
    prm->endChangeGesture();
    repaint();
}

//==============================================================================
void FilterResponse::update()
{
    auto pv = [this] (int i) { auto* p = proc.param (i); return p->convertFrom0to1 (p->getValue()); };
    const int m = juce::roundToInt (pv (P_filterMode)), t = juce::roundToInt (pv (P_filterType)), s = juce::roundToInt (pv (P_filterSlope));
    const float c = pv (P_filterCutoff), r = pv (P_filterRes), d = pv (P_filterDrive), mix = pv (P_filterMix), warm = pv (P_warmth), keep = pv (P_bassKeep);
    const juce::String key = juce::String (m) + "|" + juce::String (t) + "|" + juce::String (s) + "|" + juce::String (c, 1) + "|" + juce::String (r, 2)
                           + "|" + juce::String (d, 2) + "|" + juce::String (mix, 3) + "|" + juce::String (warm, 2) + "|" + juce::String (keep, 2);
    if (key == lastKey) return;
    lastKey = key;
    cutoff = c;

    // impulse response of the real filter code at a tiny level (so the drive stage stays linear)
    const double fs = 48000.0;
    FilterUnit u;
    u.setPreview (true);
    u.setAnalog (warm, keep);
    u.configure (m, t, s, true);
    u.setDrive (1.0f); u.setMix (mix);   // the curve shows the filter; Drive adds saturation on top
    juce::ignoreUnused (d);
    u.update (c, r, fs);
    constexpr int order = 13, N = 1 << order;
    std::vector<float> buf ((size_t) N * 2, 0.0f);
    const float amp = 1.0e-4f;
    for (int i = 0; i < N; ++i)
    {
        float yl, yr;
        u.processFrame (i == 0 ? amp : 0.0f, 0.0f, false, yl, yr);
        buf[(size_t) i] = yl / amp;
    }
    juce::dsp::FFT fft (order);
    fft.performFrequencyOnlyForwardTransform (buf.data());
    magDb.assign ((size_t) N / 2, -100.0f);
    for (int k = 1; k < N / 2; ++k) magDb[(size_t) k] = juce::Decibels::gainToDecibels (buf[(size_t) k], -100.0f);

    const int eff = effectiveFilterSlope (t, s);
    const bool classic = t == FT_LP && eff == nativeFilterSlope (m);
    caption = juce::String (kFilterTypeLabels[juce::jlimit (0, 5, t)]);
    if (filterSlopeMask (t) != 0) caption << "  " << kFilterSlopeLabels[eff];
    caption << "   |   " << kFilterLabels[juce::jlimit (0, 16, m)] << (classic ? "  (classic circuit)" : "  character");
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

    if (! magDb.empty())
    {
        juce::Path p;
        const double fs = 48000.0;
        const int N = (int) magDb.size() * 2;
        bool started = false;
        for (int px = 0; px <= (int) plot.getWidth(); px += 2)
        {
            const float f = lo * std::pow (hi / lo, px / plot.getWidth());
            const float bin = (float) (f * N / fs);
            const int k = juce::jlimit (1, (int) magDb.size() - 2, (int) bin);
            const float fr = bin - (float) k;
            const float db = magDb[(size_t) k] + (magDb[(size_t) k + 1] - magDb[(size_t) k]) * juce::jlimit (0.0f, 1.0f, fr);
            const float x = plot.getX() + (float) px, y = yOf (db);
            if (! started) { p.startNewSubPath (x, y); started = true; } else p.lineTo (x, y);
        }
        juce::Path fill (p);
        fill.lineTo (plot.getRight(), plot.getBottom()); fill.lineTo (plot.getX(), plot.getBottom()); fill.closeSubPath();
        g.setColour (col::filter.withAlpha (0.18f));
        g.fillPath (fill);
        g.setColour (col::filter.brighter (0.25f));
        g.strokePath (p, juce::PathStrokeType (2.0f));
    }
    // cutoff marker
    const float cx = xOf (juce::jlimit (lo, hi, cutoff));
    g.setColour (col::accent.withAlpha (0.8f));
    const float dashes[] = { 4.0f, 3.0f };
    g.drawDashedLine (juce::Line<float> (cx, plot.getY(), cx, plot.getBottom()), dashes, 2, 1.0f);
    g.setColour (col::text);
    g.setFont (juce::Font (juce::FontOptions (11.5f, juce::Font::bold)));
    g.drawText (formatParam (P_filterCutoff, cutoff), juce::Rectangle<float> (juce::jlimit (plot.getX(), plot.getRight() - 70, cx + 4), plot.getY(), 70, 14), juce::Justification::centredLeft);
    g.setColour (col::muted);
    g.setFont (juce::Font (juce::FontOptions (11.5f)));
    g.drawText (caption, juce::Rectangle<float> (r.getX() + 10, r.getY() + 3, r.getWidth() - 20, 16), juce::Justification::centredLeft);
}

//==============================================================================
FilterPanel::FilterPanel (MegaSynthProcessor& p)
    : proc (p),
      type (p, P_filterType, { "LP", "HP", "BP", "NOTCH", "PEAK", "AP" }, col::filter),
      slope (p, P_filterSlope, { "6", "12", "18", "24" }, col::filter),
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
    modelCap.setText ("Model (character)", juce::dontSendNotification);
    lfoCap.setText ("LFO", juce::dontSendNotification);
    info.setColour (juce::Label::textColourId, col::muted);
    info.setFont (juce::Font (juce::FontOptions (11.5f)));
    info.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (info);

    type.setTooltip ("Low Pass, High Pass, Band Pass, Notch, Peak / Bell, All Pass");
    slope.setTooltip ("Steepness. Band Pass and Notch: 12 or 24. Peak / Bell has no slope. "
                      "Low Pass at the model's own slope runs the classic circuit exactly.");
    addAndMakeVisible (type);
    addAndMakeVisible (slope);

    for (int i = 0; i < kListFilter.size; ++i) model.addItem (kListFilter.labels[i], i + 1);
    model.setTooltip ("The analogue model: as a Low Pass at its own slope it is the original circuit; "
                      "with other types and slopes it gives the multimode filter its character (input stage, resonance, saturation). "
                      "Choosing a model also selects its own slope.");
    model.onChange = [this]
    {
        const int m = model.getSelectedId() - 1;
        if (m < 0) return;
        setParam (P_filterMode, (float) m);
        setParam (P_filterSlope, (float) nativeFilterSlope (m));
    };
    addAndMakeVisible (model);

    for (int i = 0; i < kListFilterLfo.size; ++i) lfoSrc.addItem (kListFilterLfo.labels[i], i + 1);
    lfoAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, kParamIds[P_filterLfoSrc], lfoSrc);
    lfoSrc.setTooltip ("Which LFO LFO Amt uses (rate, shape and depth are on the Modulation tab)");
    addAndMakeVisible (lfoSrc);

    const std::pair<int, const char*> ks[] = { { P_filterCutoff, "Cutoff" }, { P_filterRes, "Resonance" }, { P_filterDrive, "Drive" }, { P_filterMix, "Mix" },
                                               { P_fEnvAmt, "Env Amt" }, { P_filterLfoAmt, "LFO Amt" }, { P_filterKeyTrack, "Key Track" } };
    for (auto& [idx, cap] : ks)
    {
        auto* k = knobs.add (new Knob (proc, idx, cap, col::filter));
        addAndMakeVisible (k);
    }
    knobs[0]->slider.setTooltip ("Cutoff, 20 Hz - 20 kHz (equal travel per octave)");
    knobs[2]->slider.setTooltip ("Pushes the signal into the filter: saturation, thicker bass, more bite. Level is compensated.");
    knobs[3]->slider.setTooltip ("Filter Mix: 0% = unfiltered, 100% = fully filtered (parallel filtering in between)");
    knobs[4]->slider.setTooltip ("Filter envelope amount, -100% .. +100% (100% = +10 kHz at the envelope's peak). Negative inverts the sweep.");
    knobs[5]->slider.setTooltip ("LFO to cutoff, -100% .. +100% (100% = +-4 octaves)");
    knobs[6]->slider.setTooltip ("Key tracking: 0% = cutoff fixed, 100% = cutoff follows the keyboard (from middle C)");

    addAndMakeVisible (response);
    sync();
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

void FilterPanel::timerCallback()
{
    if (isShowing()) sync();
}

void FilterPanel::sync()
{
    const int t = juce::roundToInt (plain (P_filterType)), s = juce::roundToInt (plain (P_filterSlope)), m = juce::roundToInt (plain (P_filterMode));
    slope.setMask (filterSlopeMask (t));
    slope.setShown (filterSlopeMask (t) == 0 ? -2 : effectiveFilterSlope (t, s));
    type.repaint(); slope.repaint();
    if (model.getSelectedId() != m + 1) model.setSelectedId (m + 1, juce::dontSendNotification);
    const bool classic = t == FT_LP && effectiveFilterSlope (t, s) == nativeFilterSlope (m);
    juce::String txt;
    if (t == FT_PEAK) txt = "Peak / Bell: Resonance sets the boost (+2 to +18 dB) and its width.";
    else if (classic) txt = "Classic circuit: the original model, exactly.";
    else txt = "Multimode filter with the model's character.";
    if (t == FT_LP && effectiveFilterSlope (t, s) == 3 && ! classic) txt << "\nSelf-oscillates at full Resonance.";
    info.setText (txt, juce::dontSendNotification);
    response.update();
}

void FilterPanel::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (col::panel);
    g.fillRoundedRectangle (r, 12.0f);
    g.setColour (col::border);
    g.drawRoundedRectangle (r, 12.0f, 1.0f);
    g.setColour (col::filter);
    g.fillRoundedRectangle (juce::Rectangle<float> (r.getX() + 12, r.getY() + 9, 4, 16), 2.0f);
    g.setColour (col::text);
    g.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
    g.drawText ("Filter", juce::Rectangle<float> (r.getX() + 22, r.getY() + 6, 200, 22), juce::Justification::centredLeft);
    // divider between the main knobs and the modulation knobs
    g.setColour (col::border);
    g.drawVerticalLine (330, 150.0f, 230.0f);
    g.setColour (col::muted);
    g.setFont (juce::Font (juce::FontOptions (10.5f, juce::Font::bold)));
    g.drawText ("MAIN", juce::Rectangle<float> (16, 136, 120, 12), juce::Justification::centredLeft);
    g.drawText ("MODULATION", juce::Rectangle<float> (340, 136, 160, 12), juce::Justification::centredLeft);
}

void FilterPanel::resized()
{
    typeCap.setBounds (14, 30, 200, 16);
    type.setBounds (14, 47, 390, 30);
    slopeCap.setBounds (418, 30, 200, 16);
    slope.setBounds (418, 47, 240, 30);
    modelCap.setBounds (14, 84, 200, 16);
    model.setBounds (14, 101, 240, 26);
    info.setBounds (266, 98, 400, 34);
    int x = 14;
    for (int i = 0; i < 4; ++i) { knobs[i]->setBounds (x, 150, 72, 84); x += 76; }
    x = 340;
    knobs[4]->setBounds (x, 150, 72, 84); x += 76;
    knobs[5]->setBounds (x, 150, 72, 84); x += 76;
    lfoCap.setBounds (x, 150, 80, 16);
    lfoSrc.setBounds (x, 168, 80, 26); x += 86;
    knobs[6]->setBounds (x, 150, 72, 84);
    response.setBounds (680, 30, getWidth() - 694, getHeight() - 44);
}

} // namespace tgui
