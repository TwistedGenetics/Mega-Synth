#pragma once
// The Master Filter's response curve (the same design the audio uses), redrawn when its settings change.
#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "../Fx/MasterFilter.h"

namespace tgui
{

class MasterFilterView : public juce::Component, private juce::Timer
{
public:
    explicit MasterFilterView (MegaSynthProcessor& p) : proc (p) { startTimerHz (15); }
    ~MasterFilterView() override { stopTimer(); }

    void paint (juce::Graphics& g) override
    {
        const auto bg = juce::Colour (0xff141a24), grid = juce::Colour (0xff2a3444), text = juce::Colour (0xff8a97ab), curve = juce::Colour (0xff8b7cf6);
        auto r = getLocalBounds().toFloat();
        g.setColour (bg); g.fillRoundedRectangle (r, 6.0f);
        const auto st = settings();
        const float lo = std::log (20.0f), hi = std::log (20000.0f);
        auto xFor = [&] (float f) { return r.getX() + (std::log (f) - lo) / (hi - lo) * r.getWidth(); };
        auto yFor = [&] (float db) { return r.getY() + 8.0f + (12.0f - juce::jlimit (-48.0f, 12.0f, db)) / 60.0f * (r.getHeight() - 22.0f); };
        g.setFont (juce::Font (juce::FontOptions (9.5f)));
        for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
        {
            g.setColour (grid); g.drawVerticalLine ((int) xFor (f), r.getY() + 6.0f, r.getBottom() - 14.0f);
            g.setColour (text); g.drawText (f >= 1000.0f ? juce::String ((int) (f / 1000.0f)) + "k" : juce::String ((int) f), (int) xFor (f) - 15, (int) r.getBottom() - 13, 30, 12, juce::Justification::centred);
        }
        for (float db : { 0.0f, -24.0f })
        {
            g.setColour (grid); g.drawHorizontalLine ((int) yFor (db), r.getX(), r.getRight());
            g.setColour (text); g.drawText (juce::String ((int) db) + " dB", (int) r.getX() + 4, (int) yFor (db) - 12, 40, 11, juce::Justification::left);
        }
        if (! st.on)
        {
            g.setColour (text);
            g.setFont (juce::Font (juce::FontOptions (12.0f)));
            g.drawText ("Off: the sound passes straight through", r, juce::Justification::centred);
            return;
        }
        juce::Path path;
        const int N = juce::jmax (2, (int) r.getWidth());
        for (int i = 0; i < N; ++i)
        {
            const float f = std::exp (lo + (hi - lo) * (float) i / (float) (N - 1));
            float db = tg::MasterFilter::magnitudeDb (f, st, proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0);
            // the dry/wet mix flattens the curve
            const float wet = std::pow (10.0f, db / 20.0f);
            db = 20.0f * std::log10 (std::max (1.0e-6f, std::abs (1.0f - st.mix + st.mix * wet)));
            const float x = xFor (f), y = yFor (db);
            if (i == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
        }
        g.setColour (curve);
        g.strokePath (path, juce::PathStrokeType (2.0f));
        g.setColour (curve.withAlpha (0.6f));
        g.drawVerticalLine ((int) xFor (st.cutoff), r.getY() + 6.0f, r.getBottom() - 14.0f);
    }

private:
    tg::MasterFilter::Settings settings() const
    {
        auto plain = [this] (int i) { return proc.param (i)->convertFrom0to1 (proc.param (i)->getValue()); };
        tg::MasterFilter::Settings st;
        st.on = plain (tg::P_mfOn) > 0.5f; st.type = juce::roundToInt (plain (tg::P_mfType)); st.steep = juce::roundToInt (plain (tg::P_mfSlope)) == 1;
        st.cutoff = plain (tg::P_mfCutoff); st.res = plain (tg::P_mfRes); st.driveDb = plain (tg::P_mfDrive); st.mix = plain (tg::P_mfMix);
        return st;
    }
    void timerCallback() override
    {
        juce::String key;
        for (int i : { (int) tg::P_mfOn, (int) tg::P_mfType, (int) tg::P_mfSlope, (int) tg::P_mfCutoff, (int) tg::P_mfRes, (int) tg::P_mfMix })
            key << proc.param (i)->getValue() << ",";
        if (key != lastKey) { lastKey = key; repaint(); }
    }
    MegaSynthProcessor& proc;
    juce::String lastKey;
};

} // namespace tgui
