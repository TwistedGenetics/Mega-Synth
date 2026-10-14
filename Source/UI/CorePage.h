#pragma once
// The Core slots' pages: the routing picture (what the matrix is doing right now, drawn from the
// parameters, so it is always the truth: carriers on the right, the operators that modulate them to
// the left, feedback as a loop).
#include <JuceHeader.h>
#include "../PluginEditor.h"

namespace tgui
{

class CoreAlgoView : public juce::Component, private juce::Timer
{
public:
    CoreAlgoView (MegaSynthProcessor& p, int s, juce::Colour c) : proc (p), slot (s), colour (c) { startTimerHz (10); }
    ~CoreAlgoView() override { stopTimer(); }

    // (also for the tests) the routing as the picture reads it
    struct Graph { float m[6][6] {}; float out[6] {}; float level[6] {}; bool on = false; };
    Graph read() const
    {
        Graph gr;
        auto plain = [this] (int i) { return proc.param (i)->convertFrom0to1 (proc.param (i)->getValue()); };
        gr.on = plain (tg::coreParam (slot, tg::CS_Engine)) > 0.5f;
        for (int k = 0; k < 6; ++k)
        {
            gr.level[k] = plain (tg::coreOpParam (slot, k, tg::CO_Level));
            gr.out[k] = plain (tg::coreOpParam (slot, k, tg::CO_Out));
            for (int j = 0; j < 6; ++j) gr.m[k][j] = plain (tg::coreOpParam (slot, k, tg::CO_M1 + j));
        }
        return gr;
    }
    // each operator's column (0 = heard, then one per step away from the output; -1 = silent)
    static void depths (const Graph& gr, int d[6])
    {
        for (int k = 0; k < 6; ++k) d[k] = (gr.level[k] > 0.0f && gr.out[k] > 0.0f) ? 0 : -1;
        for (int pass = 0; pass < 6; ++pass)
            for (int k = 0; k < 6; ++k)
            {
                if (gr.level[k] <= 0.0f) continue;
                for (int j = 0; j < 6; ++j)
                    if (j != k && gr.m[k][j] > 0.0f && d[j] >= 0 && (d[k] < 0 || d[k] < d[j] + 1) && d[j] + 1 <= 5) d[k] = d[j] + 1;
            }
    }

    void paint (juce::Graphics& g) override
    {
        const auto gr = read();
        int d[6]; depths (gr, d);
        int maxD = 0; for (int k = 0; k < 6; ++k) maxD = std::max (maxD, d[k]);
        auto r = getLocalBounds().toFloat().reduced (4.0f);
        const float outX = r.getRight() - 6.0f;
        const float colW = (r.getWidth() - 30.0f) / (float) std::max (1, maxD + 1);
        juce::Point<float> pos[6];
        // the silent ones in a row along the bottom-left, dimmed
        int nCol[6] {};
        for (int k = 0; k < 6; ++k) if (d[k] >= 0) ++nCol[d[k]];
        int iCol[6] {}, iSilent = 0;
        for (int k = 0; k < 6; ++k)
        {
            if (d[k] < 0) { pos[k] = { r.getX() + 10.0f + 20.0f * (float) iSilent++, r.getBottom() - 9.0f }; continue; }
            const int c = d[k], n = nCol[c], i = iCol[c]++;
            const float x = outX - 24.0f - colW * (float) c - (n > 4 && i >= 4 ? colW * 0.45f : 0.0f);
            const int m = n > 4 ? (i < 4 ? 4 : n - 4) : n, ii = n > 4 && i >= 4 ? i - 4 : i;
            const float span = r.getHeight() - 22.0f;
            pos[k] = { x, r.getY() + 9.0f + (m == 1 ? span * 0.5f : span * (float) ii / (float) (m - 1)) };
        }
        // the output bar and the carriers' lines to it
        g.setColour (col::muted.withAlpha (0.6f));
        g.fillRect (outX, r.getY() + 4.0f, 3.0f, r.getHeight() - 22.0f);
        g.setFont (juce::Font (juce::FontOptions (10.0f)));
        g.drawText ("out", juce::Rectangle<float> (outX - 14.0f, r.getBottom() - 16.0f, 28.0f, 12.0f), juce::Justification::centred);
        for (int k = 0; k < 6; ++k)
            if (d[k] == 0)
            {
                g.setColour (colour.withAlpha (0.35f + 0.65f * std::min (1.0f, gr.out[k])));
                g.drawLine (pos[k].x + 9.0f, pos[k].y, outX, pos[k].y, 1.6f);
            }
        // the modulation arrows (thicker for more), then feedback loops
        for (int k = 0; k < 6; ++k)
            for (int j = 0; j < 6; ++j)
            {
                if (j == k || d[k] < 0 || d[j] < 0 || gr.m[k][j] <= 0.0f) continue;
                const float a = std::min (1.0f, gr.m[k][j] / 4.0f);
                juce::Line<float> ln (pos[k], pos[j]);
                const auto dir = (pos[j] - pos[k]) / std::max (1.0f, ln.getLength());
                juce::Line<float> arrow (pos[k] + dir * 10.0f, pos[j] - dir * 11.0f);
                g.setColour (colour.withAlpha (0.35f + 0.6f * a));
                g.drawArrow (arrow, 1.0f + 1.5f * a, 7.0f, 7.0f);
            }
        for (int k = 0; k < 6; ++k)
        {
            if (d[k] < 0 || gr.m[k][k] <= 0.0f) continue;
            juce::Path loop;
            loop.addCentredArc (pos[k].x, pos[k].y - 11.0f, 6.0f, 5.0f, 0.0f, -2.2f, 2.2f, true);
            g.setColour (col::text.withAlpha (0.4f + 0.5f * std::min (1.0f, gr.m[k][k] / 3.0f)));
            g.strokePath (loop, juce::PathStrokeType (1.4f));
        }
        // the operators
        for (int k = 0; k < 6; ++k)
        {
            const bool live = d[k] >= 0;
            const auto c = juce::Rectangle<float> (18.0f, 18.0f).withCentre (pos[k]);
            g.setColour (live ? (d[k] == 0 ? colour : colour.darker (0.5f)) : col::panel3);
            g.fillEllipse (c);
            g.setColour (live ? col::text : col::muted);
            g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
            g.drawText (juce::String (k + 1), c, juce::Justification::centred);
        }
        if (! gr.on)
        {
            g.setColour (col::muted);
            g.setFont (juce::Font (juce::FontOptions (11.5f)));
            g.drawText ("Engine Off", getLocalBounds().removeFromTop (14), juce::Justification::centredLeft);
        }
    }

private:
    void timerCallback() override
    {
        if (! isShowing()) return;
        const auto gr = read();
        juce::MemoryBlock mb (&gr, sizeof (gr));
        if (mb != last) { last = mb; repaint(); }
    }
    MegaSynthProcessor& proc;
    const int slot;
    juce::Colour colour;
    juce::MemoryBlock last;
};

} // namespace tgui
