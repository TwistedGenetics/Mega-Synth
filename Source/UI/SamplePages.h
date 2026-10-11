#pragma once
// The sample pages' shared parts: a waveform view of a sample slot, with markers drawn on top.
// Included from PluginEditor.cpp after PluginEditor.h.
#include <JuceHeader.h>

namespace tgui
{

class SampleWaveView : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    struct Marker { double frac; juce::Colour colour; juce::String label; bool thick; };
    SampleWaveView (MegaSynthProcessor& p, int slotIndex, juce::Colour c) : proc (p), slot (slotIndex), colour (c) { startTimerHz (20); }
    ~SampleWaveView() override { stopTimer(); }

    std::function<std::vector<Marker>()> markers;          // what to draw over the wave (fractions of its length)
    std::function<juce::Range<double>()> shade;            // a highlighted span (fractions), optional
    std::function<void (double frac, const juce::MouseEvent&)> onClick, onDrag;
    std::function<void (double frac)> onRightClick;
    juce::String emptyText = "Load a sample (WAV, AIFF, FLAC or MP3)";

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (col::panel2); g.fillRoundedRectangle (r, 6.0f);
        g.setColour (col::border); g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
        const auto* w = proc.getSample (slot);
        if (w == nullptr || w->length < 2)
        {
            g.setColour (col::muted); g.setFont (juce::Font (juce::FontOptions (12.0f)));
            g.drawText (emptyText, r, juce::Justification::centred);
            return;
        }
        rebuild (w);
        auto in = r.reduced (4.0f, 6.0f);
        if (shade)
        {
            const auto s = shade();
            if (s.getLength() > 0)
            {
                g.setColour (colour.withAlpha (0.12f));
                g.fillRect (juce::Rectangle<float> (in.getX() + (float) s.getStart() * in.getWidth(), in.getY(), (float) s.getLength() * in.getWidth(), in.getHeight()));
            }
        }
        const float mid = in.getCentreY(), h = in.getHeight() * 0.5f;
        g.setColour (colour.withAlpha (0.8f));
        const int n = (int) peaks.size();
        for (int i = 0; i < n; ++i)
        {
            const float x = in.getX() + (float) i * in.getWidth() / (float) std::max (1, n - 1);
            g.drawVerticalLine ((int) x, mid - peaks[(size_t) i] * h, mid + peaks[(size_t) i] * h + 1.0f);
        }
        if (markers)
            for (auto& m : markers())
            {
                if (m.frac < 0.0 || m.frac > 1.0) continue;
                const float x = in.getX() + (float) m.frac * in.getWidth();
                g.setColour (m.colour);
                g.fillRect (juce::Rectangle<float> (x - (m.thick ? 1.0f : 0.5f), in.getY(), m.thick ? 2.0f : 1.0f, in.getHeight()));
                if (m.label.isNotEmpty())
                {
                    g.setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::bold)));
                    g.drawText (m.label, juce::Rectangle<float> (x + 2.0f, in.getY(), 40.0f, 12.0f), juce::Justification::left);
                }
            }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { if (onRightClick) onRightClick (fracAt (e)); return; }
        if (onClick) onClick (fracAt (e), e);
    }
    void mouseDrag (const juce::MouseEvent& e) override { if (onDrag) onDrag (fracAt (e), e); }
    double fracAt (const juce::MouseEvent& e) const
    {
        auto in = getLocalBounds().toFloat().reduced (4.0f, 6.0f);
        return juce::jlimit (0.0, 1.0, (double) ((e.position.x - in.getX()) / in.getWidth()));
    }
private:
    void timerCallback() override { if (isShowing()) repaint(); }
    void rebuild (const tg::WaveSample* w)
    {
        const int cols = std::max (8, getWidth() - 8);
        if (w == built && cols == (int) peaks.size()) return;
        built = w;
        peaks.assign ((size_t) cols, 0.0f);
        const float norm = 1.0f / std::max (0.05f, w->peak);
        for (int c = 0; c < cols; ++c)
        {
            const int a = (int) ((int64_t) c * w->length / cols), b = std::max (a + 1, (int) ((int64_t) (c + 1) * w->length / cols));
            float pk = 0.0f;
            for (int i = a; i < b && i < w->length; i += std::max (1, (b - a) / 64))
                pk = std::max (pk, std::max (std::abs (w->ch[0][(size_t) i]), std::abs (w->ch[w->numChannels > 1 ? 1 : 0][(size_t) i])));
            peaks[(size_t) c] = std::min (1.0f, pk * norm);
        }
    }
    MegaSynthProcessor& proc;
    const int slot;
    juce::Colour colour;
    const tg::WaveSample* built = nullptr;
    std::vector<float> peaks;
};

// "name, length, size in the project" for a sample slot, with a warning when it's large
inline juce::String sampleInfoText (const MegaSynthProcessor& p, int slot)
{
    const auto* w = p.getSample (slot);
    if (w == nullptr) return "No sample loaded";
    const double mb = (double) p.getSampleBytes (slot) / (1024.0 * 1024.0);
    juce::String t = p.getSampleName (slot) + "   " + juce::String (w->duration(), 2) + " s   " + juce::String (w->sampleRate / 1000.0, 1) + " kHz   "
                     + (w->numChannels > 1 ? "stereo" : "mono") + "   saved in the project: " + juce::String (mb, 1) + " MB";
    if (mb > 8.0) t << "  (large: projects and undo get slower)";
    if (w->duration() >= MegaSynthProcessor::sampleCapSeconds (slot) - 0.01) t << "  (cut to " << juce::String ((int) MegaSynthProcessor::sampleCapSeconds (slot)) << " s)";
    return t;
}

} // namespace tgui
