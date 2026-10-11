#pragma once
// The sample pages' shared parts: a waveform view of a sample slot, with markers drawn on top.
// Included from PluginEditor.cpp after PluginEditor.h.
#include <JuceHeader.h>

namespace tgui
{

class SampleWaveView : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    struct Marker { double frac; juce::Colour colour; juce::String label; bool thick; bool handle = false; };   // handle: a trim handle (drawn low)
    SampleWaveView (MegaSynthProcessor& p, int slotIndex, juce::Colour c) : proc (p), slot (slotIndex), colour (c) { startTimerHz (20); setWantsKeyboardFocus (true); }
    void setSlot (int s) { if (s != slot) { slot = s; built = nullptr; viewStart = 0.0; viewLen = 1.0; repaint(); } }
    void setColour (juce::Colour c) { colour = c; repaint(); }
    ~SampleWaveView() override { stopTimer(); }

    std::function<std::vector<Marker>()> markers;          // what to draw over the wave (fractions of its length)
    std::function<juce::Range<double>()> shade;            // a highlighted span (fractions), optional
    std::function<void (double frac, const juce::MouseEvent&)> onClick, onDrag;
    std::function<void (double frac)> onRightClick, onDoubleClick;
    std::function<bool (const juce::KeyPress&)> onKey;
    // dragging something on the wave (a marker, a trim handle): return true from onDragStart to take the drag
    std::function<bool (double frac, const juce::MouseEvent&)> onDragStart;
    std::function<void (double frac, const juce::MouseEvent&)> onDragMove;
    std::function<void()> onDragEnd, onViewChanged;
    bool zoomable = false;                                  // mouse wheel / pinch zoom, Shift+wheel scroll
    bool outlineShade = false;                              // draw the shaded span as a frame (the overview of a zoomed view)
    bool keyPressed (const juce::KeyPress& k) override { return onKey ? onKey (k) : false; }
    void mouseDoubleClick (const juce::MouseEvent& e) override { if (onDoubleClick && ! e.mods.isPopupMenu()) onDoubleClick (fracAt (e)); }
    juce::String emptyText = "Load a sample (WAV, AIFF, FLAC or MP3)";

    // ---- the view: which part of the sample is shown (fractions)
    double viewStart = 0.0, viewLen = 1.0;
    double minViewLen() const { const auto* w = proc.getSample (slot); return w != nullptr ? std::min (1.0, std::max (8.0, (double) inner().getWidth()) / std::max (1, w->length)) : 1.0; }   // 1 sample per pixel
    void setView (double start, double len)
    {
        len = juce::jlimit (minViewLen(), 1.0, len);
        start = juce::jlimit (0.0, 1.0 - len, start);
        if (start != viewStart || len != viewLen) { viewStart = start; viewLen = len; repaint(); if (onViewChanged) onViewChanged(); }
    }
    void zoomAround (double frac, double factor) { const double len = juce::jlimit (minViewLen(), 1.0, viewLen * factor); setView (frac - (frac - viewStart) * len / viewLen, len); }
    void zoomTo (juce::Range<double> r) { if (r.getLength() > 0.0) setView (r.getStart() - r.getLength() * 0.1, r.getLength() * 1.2); }
    double samplesPerPixel() const { const auto* w = proc.getSample (slot); return w != nullptr ? viewLen * w->length / std::max (1.0f, inner().getWidth()) : 1.0; }
    float xFor (double frac) const { auto in = inner(); return in.getX() + (float) ((frac - viewStart) / viewLen) * in.getWidth(); }

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
        auto in = inner();
        g.saveState();
        g.reduceClipRegion (in.toNearestInt());
        if (shade)
        {
            const auto s = shade();
            if (s.getLength() > 0)
            {
                const juce::Rectangle<float> sr (xFor (s.getStart()), in.getY(), std::max (2.0f, xFor (s.getEnd()) - xFor (s.getStart())), in.getHeight());
                g.setColour (colour.withAlpha (outlineShade ? 0.22f : 0.12f));
                g.fillRect (sr);
                if (outlineShade) { g.setColour (col::text.withAlpha (0.8f)); g.drawRect (sr, 1.0f); }
            }
        }
        // the wave: the lowest and highest sample under each pixel (from a cache when zoomed out)
        const float mid = in.getCentreY(), h = in.getHeight() * 0.5f;
        const float norm = 1.0f / std::max (0.05f, w->peak);
        const int cols = std::max (1, (int) in.getWidth());
        const double spp = samplesPerPixel();
        g.setColour (colour.withAlpha (0.85f));
        if (spp < 1.5)
        {
            // close in: the samples themselves, joined up
            juce::Path p; bool started = false;
            const int a = std::max (0, (int) std::floor (viewStart * w->length)), b = std::min (w->length - 1, (int) std::ceil ((viewStart + viewLen) * w->length));
            for (int i = a; i <= b; ++i)
            {
                const float x = xFor ((double) i / w->length), y = mid - juce::jlimit (-1.0f, 1.0f, w->ch[0][(size_t) i] * norm) * h;
                if (! started) { p.startNewSubPath (x, y); started = true; } else p.lineTo (x, y);
            }
            g.strokePath (p, juce::PathStrokeType (1.2f));
            if (spp < 0.25) for (int i = a; i <= b; ++i) g.fillEllipse (xFor ((double) i / w->length) - 1.5f, mid - juce::jlimit (-1.0f, 1.0f, w->ch[0][(size_t) i] * norm) * h - 1.5f, 3.0f, 3.0f);
        }
        else
            for (int c = 0; c < cols; ++c)
            {
                const double f0 = viewStart + viewLen * c / cols, f1 = viewStart + viewLen * (c + 1) / cols;
                float lo, hi; minMax (*w, (int) (f0 * w->length), std::max ((int) (f0 * w->length) + 1, (int) (f1 * w->length)), lo, hi);
                const float x = in.getX() + (float) c;
                g.drawVerticalLine ((int) x, mid - std::min (1.0f, hi * norm) * h, mid - std::max (-1.0f, lo * norm) * h + 1.0f);
            }
        if (markers)
            for (auto& m : markers())
            {
                if (m.frac < viewStart - 0.001 || m.frac > viewStart + viewLen + 0.001) continue;
                const float x = xFor (m.frac);
                g.setColour (m.colour);
                if (m.handle)
                {
                    juce::Path tri; tri.addTriangle (x - 5.0f, in.getBottom(), x + 5.0f, in.getBottom(), x, in.getBottom() - 9.0f);
                    g.fillPath (tri);
                    g.fillRect (juce::Rectangle<float> (x - 0.5f, in.getCentreY(), 1.0f, in.getHeight() * 0.5f));
                    continue;
                }
                g.fillRect (juce::Rectangle<float> (x - (m.thick ? 1.0f : 0.5f), in.getY(), m.thick ? 2.0f : 1.0f, in.getHeight()));
                if (m.label.isNotEmpty())
                {
                    g.setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::bold)));
                    g.drawText (m.label, juce::Rectangle<float> (x + 2.0f, in.getY(), 40.0f, 12.0f), juce::Justification::left);
                }
            }
        g.restoreState();
        if (viewLen < 0.999 && ! zoomable)   // (a zoomable view has its own scroll bar and overview)
        {
            // where this view is in the whole sample
            g.setColour (col::muted.withAlpha (0.5f));
            const float bx = r.getX() + 6.0f, bw = r.getWidth() - 12.0f;
            g.fillRect (juce::Rectangle<float> (bx + (float) viewStart * bw, r.getBottom() - 4.0f, std::max (3.0f, (float) viewLen * bw), 2.0f));
        }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (onKey) grabKeyboardFocus();
        dragging = false;
        if (e.mods.isPopupMenu()) { if (onRightClick) onRightClick (fracAt (e)); return; }
        if (onDragStart && onDragStart (fracAt (e), e)) { dragging = true; return; }
        if (onClick) onClick (fracAt (e), e);
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging) { if (onDragMove) onDragMove (fracAt (e), e); return; }
        if (onDrag) onDrag (fracAt (e), e);
    }
    void mouseUp (const juce::MouseEvent&) override { if (dragging) { dragging = false; if (onDragEnd) onDragEnd(); } }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& d) override
    {
        if (! zoomable) { juce::Component::mouseWheelMove (e, d); return; }
        const float dy = std::abs (d.deltaY) > std::abs (d.deltaX) ? d.deltaY : -d.deltaX;
        if (e.mods.isShiftDown() || std::abs (d.deltaX) > std::abs (d.deltaY)) setView (viewStart - (double) dy * viewLen * 0.5, viewLen);   // scroll
        else zoomAround (fracAt (e), std::pow (0.5, (double) dy * 2.0));
    }
    void mouseMagnify (const juce::MouseEvent& e, float scale) override { if (zoomable && scale > 0.0f) zoomAround (fracAt (e), 1.0 / scale); }
    double fracAt (const juce::MouseEvent& e) const { return fracAtX (e.position.x); }
    double fracAtX (float x) const { auto in = inner(); return juce::jlimit (0.0, 1.0, viewStart + viewLen * (double) ((x - in.getX()) / in.getWidth())); }
    juce::Rectangle<float> inner() const { return getLocalBounds().toFloat().reduced (4.0f, 6.0f); }
private:
    void timerCallback() override { if (isShowing()) repaint(); }
    // a min / max cache in levels of 64, 256, 1024 and 4096 samples per entry
    void rebuild (const tg::WaveSample* w)
    {
        if (w == built) return;
        built = w;
        for (int lv = 0; lv < 4; ++lv)
        {
            const int per = 64 << (2 * lv), n = (w->length + per - 1) / per;
            auto& lo = cacheLo[lv]; auto& hi = cacheHi[lv];
            lo.assign ((size_t) n, 0.0f); hi.assign ((size_t) n, 0.0f);
            for (int k = 0; k < n; ++k)
            {
                float a = 1e9f, b = -1e9f;
                if (lv == 0) for (int i = k * per; i < std::min (w->length, (k + 1) * per); ++i) { const float x = sampleAt (*w, i); a = std::min (a, x); b = std::max (b, x); }
                else for (int j = k * 4; j < std::min ((int) cacheLo[lv - 1].size(), k * 4 + 4); ++j) { a = std::min (a, cacheLo[lv - 1][(size_t) j]); b = std::max (b, cacheHi[lv - 1][(size_t) j]); }
                lo[(size_t) k] = a; hi[(size_t) k] = b;
            }
        }
    }
    static float sampleAt (const tg::WaveSample& w, int i)
    {
        const float a = w.ch[0][(size_t) i], b = w.ch[w.numChannels > 1 ? 1 : 0][(size_t) i];
        return std::abs (a) > std::abs (b) ? a : b;
    }
    void minMax (const tg::WaveSample& w, int a, int b, float& lo, float& hi) const
    {
        lo = 1e9f; hi = -1e9f;
        a = juce::jlimit (0, w.length - 1, a); b = juce::jlimit (a + 1, w.length, b);
        int lv = -1;
        for (int k = 3; k >= 0; --k) if (b - a >= (64 << (2 * k)) * 2) { lv = k; break; }
        if (lv < 0) { for (int i = a; i < b; ++i) { const float x = sampleAt (w, i); lo = std::min (lo, x); hi = std::max (hi, x); } return; }
        const int per = 64 << (2 * lv);
        const int k0 = a / per, k1 = std::max (k0 + 1, b / per);
        for (int k = k0; k < std::min (k1, (int) cacheLo[lv].size()); ++k) { lo = std::min (lo, cacheLo[lv][(size_t) k]); hi = std::max (hi, cacheHi[lv][(size_t) k]); }
        if (lo > hi) lo = hi = 0.0f;
    }
    MegaSynthProcessor& proc;
    int slot;
    juce::Colour colour;
    const tg::WaveSample* built = nullptr;
    std::vector<float> cacheLo[4], cacheHi[4];
    bool dragging = false;
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
