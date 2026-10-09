#include "Overview.h"
#include "../PluginEditor.h"
#include "../Registry.h"
#include "../ParamFormat.h"

using namespace tg;

namespace tgui
{

static float plainOf (MegaSynthProcessor& p, int i) { return p.param (i)->convertFrom0to1 (p.param (i)->getValue()); }

//==============================================================================
// Signal flow: the per-note chain, then the bus, with live on/off state, Mod Matrix route
// counts per stage and the Feedback Matrix paths drawn as arcs.
namespace
{
    struct Box { const char* name; const char* modules[4]; };
    const Box kBoxes[] = {
        { "Oscillators", { "Oscillator", "Wavetable", "Complex", "SuperSaw" } },
        { "DNA Splice", { "DNA Splice", nullptr } },
        { "Wave Mutation", { "Wave Mutation", nullptr } },
        { "Audio-Rate", { "Audio-Rate Transform", nullptr } },
        { "Filter", { "Filter", "Filter Envelope", nullptr } },
        { "Amp", { "Amp Envelope", "Mixer", nullptr } },
        { "Resonator", { "Resonator", nullptr } },
        { "Granular", { "Granular", nullptr } },
        { "Spectral", { "Spectral", nullptr } },
        { "Effects", { "Tape Delay", "Reverb + Shimmer", "Juno Chorus", "Reverse Reverb" } },
        { "Out", { nullptr } } };
    constexpr int kNumBoxes = (int) (sizeof (kBoxes) / sizeof (kBoxes[0]));
}

void SignalFlowView::update()
{
    // repaint only when something it shows has changed
    juce::String key;
    for (int i : { (int) P_dnaMix, (int) P_wmMix, (int) P_arFm, (int) P_arAm, (int) P_arRing, (int) P_arShiftMix, (int) P_resMix, (int) P_grMix,
                   (int) P_spOn, (int) P_busOrder, (int) P_delayMix, (int) P_reverbMix, (int) P_chorusMix })
        key << juce::String (plainOf (proc, i), 3) << ",";
    for (int i = P_fbGrGr; i <= P_fbOutDl; ++i) key << juce::String (plainOf (proc, i), 2) << ",";
    key << (int) proc.routes.getVersion();
    if (key != lastKey) { lastKey = key; repaint(); }
}

void SignalFlowView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (col::panel);
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (col::border);
    g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);

    bool active[kNumBoxes];
    for (auto& a : active) a = true;
    active[1] = plainOf (proc, P_dnaMix) > 0.0001f;
    active[2] = plainOf (proc, P_wmMix) > 0.0001f;
    active[3] = plainOf (proc, P_arFm) + plainOf (proc, P_arAm) + plainOf (proc, P_arRing) + plainOf (proc, P_arShiftMix) > 0.0001f;
    active[6] = plainOf (proc, P_resMix) > 0.0001f;
    active[7] = plainOf (proc, P_grMix) > 0.0001f;
    active[8] = plainOf (proc, P_spOn) > 0.5f;
    active[9] = plainOf (proc, P_delayMix) + plainOf (proc, P_reverbMix) + plainOf (proc, P_chorusMix) + plainOf (proc, P_shimmerMix) + plainOf (proc, P_reverseMix) > 0.0001f;
    const bool specFirst = plainOf (proc, P_busOrder) > 0.5f;

    // route counts per box
    int routesTo[kNumBoxes] = {};
    for (int k = 0; k < kNumRoutes; ++k)
    {
        const auto c = proc.routes.get (k);
        if (! c.active()) continue;
        const auto& mod = meta (c.dst).module;
        for (int b = 0; b < kNumBoxes; ++b)
            for (auto* m : kBoxes[b].modules) if (m != nullptr && mod.startsWith (m)) { ++routesTo[b]; goto counted; }
        counted:;
    }

    // layout: order of the bus boxes follows Bus Order
    int order[kNumBoxes] = { 0, 1, 2, 3, 4, 5, 6, specFirst ? 8 : 7, specFirst ? 7 : 8, 9, 10 };
    const float gap = 10.0f, top = 40.0f, h = 46.0f;
    const float bw = (r.getWidth() - 20.0f - gap * (kNumBoxes - 1)) / kNumBoxes;
    juce::Rectangle<float> rects[kNumBoxes];
    for (int k = 0; k < kNumBoxes; ++k) rects[order[k]] = { 10.0f + k * (bw + gap), top, bw, h };

    g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
    g.setColour (col::muted);
    g.drawText ("EACH NOTE", juce::Rectangle<float> (rects[0].getX(), 10, rects[6].getRight() - rects[0].getX(), 16), juce::Justification::centred);
    g.drawText ("ALL NOTES TOGETHER", juce::Rectangle<float> (rects[order[7]].getX(), 10, rects[10].getRight() - rects[order[7]].getX(), 16), juce::Justification::centred);
    g.drawHorizontalLine (28, rects[0].getX(), rects[6].getRight());
    g.drawHorizontalLine (28, rects[order[7]].getX(), rects[10].getRight());

    for (int k = 0; k + 1 < kNumBoxes; ++k)
    {
        const auto a = rects[order[k]], b = rects[order[k + 1]];
        g.setColour (col::border);
        g.drawArrow ({ a.getRight(), a.getCentreY(), b.getX(), b.getCentreY() }, 1.5f, 6.0f, 6.0f);
    }
    for (int b = 0; b < kNumBoxes; ++b)
    {
        const auto rb = rects[b];
        g.setColour (active[b] ? col::panel3 : col::panel2);
        g.fillRoundedRectangle (rb, 7.0f);
        g.setColour (active[b] ? col::accent : col::border);
        g.drawRoundedRectangle (rb, 7.0f, active[b] ? 1.6f : 1.0f);
        g.setColour (active[b] ? col::text : col::muted);
        g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
        g.drawFittedText (kBoxes[b].name, rb.reduced (3).toNearestInt(), juce::Justification::centred, 2);
        if (routesTo[b] > 0)
        {
            const auto badge = juce::Rectangle<float> (rb.getRight() - 22, rb.getY() - 8, 24, 16);
            g.setColour (col::mod);
            g.fillRoundedRectangle (badge, 8.0f);
            g.setColour (col::bg);
            g.setFont (juce::Font (juce::FontOptions (10.5f, juce::Font::bold)));
            g.drawText (juce::String (routesTo[b]), badge, juce::Justification::centred);
        }
    }

    // feedback paths, as arcs underneath
    const int tapBox[4] = { 7, 8, 9, 10 }, dstBox[3] = { 7, 8, 9 };
    for (int s = 0; s < 4; ++s)
        for (int d = 0; d < 3; ++d)
        {
            const float a = plainOf (proc, P_fbGrGr + s * 3 + d);
            if (a <= 0.0001f) continue;
            const auto from = rects[tapBox[s]], to = rects[dstBox[d]];
            const float x0 = from.getCentreX() + 6, x1 = to.getCentreX() - 6, y = from.getBottom();
            juce::Path p;
            p.startNewSubPath (x0, y);
            p.cubicTo (x0, y + 34 + 6 * s, x1, y + 34 + 6 * s, x1, y + 2);
            g.setColour (col::complex.withAlpha (0.35f + 0.65f * a));
            g.strokePath (p, juce::PathStrokeType (1.0f + 2.5f * a));
            g.fillEllipse (juce::Rectangle<float> (6, 6).withCentre ({ x1, y + 3 }));
        }
    g.setColour (col::muted);
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    g.drawText ("cyan badges: Mod Matrix routes into a stage    red arcs: Feedback Matrix paths    lit boxes: stages switched in",
                juce::Rectangle<float> (10, r.getBottom() - 18, r.getWidth() - 20, 14), juce::Justification::centredLeft);
}

//==============================================================================
// Genetic network: every active route as a line from its source to its destination,
// brightening with the source's live value.
void NetworkView::update() { phase += 0.04f; if (phase > 1.0f) phase -= 1.0f; repaint(); }

void NetworkView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (col::panel);
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (col::border);
    g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);
    g.setColour (col::text);
    g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
    g.drawText ("Genetic network  (live Mod Matrix)", 12, 6, 400, 18, juce::Justification::centredLeft);

    std::vector<int> srcs, dsts;
    struct L { int s, d; float amt; bool uni; };
    std::vector<L> lines;
    for (int k = 0; k < kNumRoutes; ++k)
    {
        const auto c = proc.routes.get (k);
        if (! c.active()) continue;
        if (std::find (srcs.begin(), srcs.end(), c.src) == srcs.end()) srcs.push_back (c.src);
        if (std::find (dsts.begin(), dsts.end(), c.dst) == dsts.end()) dsts.push_back (c.dst);
        lines.push_back ({ c.src, c.dst, plainOf (proc, P_mod1Amt + k), c.unipolar });
    }
    if (lines.empty())
    {
        g.setColour (col::muted);
        g.setFont (juce::Font (juce::FontOptions (12.5f)));
        g.drawFittedText ("No routes yet. Right-click any knob to modulate it, or use the Mod Matrix tab.", r.reduced (20).toNearestInt(), juce::Justification::centred, 3);
        return;
    }
    const int maxRows = std::max (1, (int) ((r.getHeight() - 58) / 17) + 1);
    auto rowY = [&] (int i, int n) { const float span = r.getHeight() - 58; return 40.0f + (n <= 1 ? span * 0.5f : span * (float) i / (float) (std::min (n, maxRows) - 1)); };
    const float xs = 150.0f, xd = r.getWidth() - 220.0f;
    auto idx = [] (const std::vector<int>& v, int x) { return (int) (std::find (v.begin(), v.end(), x) - v.begin()); };

    for (auto& l : lines)
    {
        const int si = idx (srcs, l.s), di = idx (dsts, l.d);
        if (si >= maxRows || di >= maxRows) continue;
        const float y0 = rowY (si, (int) srcs.size()), y1 = rowY (di, (int) dsts.size());
        const float live = std::abs (proc.getEngine().liveSrc[(size_t) l.s].load (std::memory_order_relaxed));
        juce::Path p;
        p.startNewSubPath (xs, y0);
        p.cubicTo (xs + (xd - xs) * 0.45f, y0, xs + (xd - xs) * 0.55f, y1, xd, y1);
        const auto c = l.amt >= 0 ? col::mod : col::complex;
        g.setColour (c.withAlpha (0.25f + 0.6f * juce::jlimit (0.0f, 1.0f, live)));
        g.strokePath (p, juce::PathStrokeType (1.0f + 3.0f * std::abs (l.amt)));
        // a pulse travelling along the line, faster for stronger sources
        const float t = std::fmod (phase * (0.5f + 2.0f * live) + 0.13f * (float) si, 1.0f);
        const auto pt = p.getPointAlongPath (p.getLength() * t);
        g.setColour (c.brighter (0.4f));
        g.fillEllipse (juce::Rectangle<float> (5, 5).withCentre (pt));
    }
    g.setFont (juce::Font (juce::FontOptions (11.5f)));
    for (int i = 0; i < (int) srcs.size() && i < maxRows; ++i)
    {
        const float y = rowY (i, (int) srcs.size());
        const float live = proc.getEngine().liveSrc[(size_t) srcs[(size_t) i]].load (std::memory_order_relaxed);
        g.setColour (col::panel3);
        g.fillRoundedRectangle (10, y - 8, xs - 16, 16, 4);
        g.setColour (col::mod.withAlpha (0.5f));
        g.fillRoundedRectangle (10, y - 8, (xs - 16) * juce::jlimit (0.0f, 1.0f, std::abs (live)), 16, 4);
        g.setColour (col::text);
        g.drawText (kModSrcNames[srcs[(size_t) i]], 14, (int) y - 8, (int) xs - 20, 16, juce::Justification::centredLeft);
        g.fillEllipse (xs - 4, y - 3, 6, 6);
    }
    for (int i = 0; i < (int) dsts.size() && i < maxRows; ++i)
    {
        const float y = rowY (i, (int) dsts.size());
        const auto& m = meta (dsts[(size_t) i]);
        g.setColour (col::text);
        g.fillEllipse (xd - 2, y - 3, 6, 6);
        g.drawText (m.module + ": " + m.name, (int) xd + 8, (int) y - 8, (int) (r.getWidth() - xd) - 14, 16, juce::Justification::centredLeft);
    }
    if ((int) srcs.size() > maxRows || (int) dsts.size() > maxRows)
    {
        g.setColour (col::muted);
        g.drawText ("(more in the Mod Matrix)", (int) xs, (int) r.getHeight() - 16, 200, 14, juce::Justification::centredLeft);
    }
}

//==============================================================================
// Spectrum of the synth's output (4096-point FFT, log frequency, peak hold).
void SpectrumView::analyse()
{
    std::fill (data.begin(), data.end(), 0.0f);
    proc.readScope (data.data(), 4096);
    for (int i = 0; i < 4096; ++i) data[(size_t) i] *= 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * i / 4095.0f);
    fft.performFrequencyOnlyForwardTransform (data.data());
    for (int k = 0; k < 2048; ++k)
    {
        const float d = juce::Decibels::gainToDecibels (data[(size_t) k] / 1024.0f, -120.0f);
        db[(size_t) k] = d > db[(size_t) k] ? d : db[(size_t) k] + (d - db[(size_t) k]) * 0.25f;   // fast rise, smooth fall
        peak[(size_t) k] = std::max (d, peak[(size_t) k] - 0.4f);
    }
}

void SpectrumView::update() { analyse(); repaint(); }

void SpectrumView::paint (juce::Graphics& g)
{
    if (db[10] <= -99.0f) analyse();
    auto r = getLocalBounds().toFloat();
    g.setColour (col::panel);
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (col::border);
    g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);
    g.setColour (col::text);
    g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
    g.drawText ("Spectrum", 12, 6, 200, 18, juce::Justification::centredLeft);
    auto plot = r.reduced (12).withTrimmedTop (22).withTrimmedBottom (14);
    const double sr = std::max (8000.0, proc.getSampleRate());
    auto xOf = [&] (double hz) { return plot.getX() + plot.getWidth() * (float) (std::log10 (hz / 20.0) / std::log10 (20000.0 / 20.0)); };
    auto yOf = [&] (float d) { return plot.getY() + plot.getHeight() * juce::jlimit (0.0f, 1.0f, -d / 96.0f); };
    g.setFont (juce::Font (juce::FontOptions (10.5f)));
    for (double hz : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
    {
        g.setColour (col::panel3);
        g.drawVerticalLine ((int) xOf (hz), plot.getY(), plot.getBottom());
        g.setColour (col::muted);
        g.drawText (hz >= 1000 ? juce::String (hz / 1000.0, 0) + "k" : juce::String ((int) hz), (int) xOf (hz) - 15, (int) plot.getBottom() + 1, 30, 12, juce::Justification::centred);
    }
    for (float d : { -24.0f, -48.0f, -72.0f })
    {
        g.setColour (col::panel3);
        g.drawHorizontalLine ((int) yOf (d), plot.getX(), plot.getRight());
        g.setColour (col::muted);
        g.drawText (juce::String ((int) d) + " dB", (int) plot.getRight() - 44, (int) yOf (d) - 12, 42, 11, juce::Justification::centredRight);
    }
    juce::Path fill, pk;
    fill.startNewSubPath (plot.getX(), plot.getBottom());
    bool first = true;
    for (int k = 1; k < 2048; ++k)
    {
        const double hz = k * sr / 4096.0;
        if (hz < 20.0 || hz > 20000.0) continue;
        const float x = xOf (hz);
        fill.lineTo (x, yOf (db[(size_t) k]));
        if (first) { pk.startNewSubPath (x, yOf (peak[(size_t) k])); first = false; } else pk.lineTo (x, yOf (peak[(size_t) k]));
    }
    fill.lineTo (plot.getRight(), plot.getBottom());
    fill.closeSubPath();
    g.setGradientFill (juce::ColourGradient (col::accent.withAlpha (0.55f), plot.getX(), plot.getY(), col::mod.withAlpha (0.15f), plot.getX(), plot.getBottom(), false));
    g.fillPath (fill);
    g.setColour (col::text.withAlpha (0.5f));
    g.strokePath (pk, juce::PathStrokeType (1.0f));
}

//==============================================================================
// DNA strand: a double helix whose 13 base pairs are the 13 genetic groups. Unlocked groups
// glow with how far the current seed and Mutate amount push them; locked ones are grey.
void DnaStrandView::update()
{
    const float amt = plainOf (proc, P_mutAmount);
    const bool seq = plainOf (proc, P_dsOn) > 0.5f;
    spin += 0.015f + 0.08f * amt + (seq ? 0.03f : 0.0f);
    repaint();
}

void DnaStrandView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (col::panel);
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (col::border);
    g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);
    g.setColour (col::text);
    g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
    const float amt = plainOf (proc, P_mutAmount);
    const int seed = juce::roundToInt (plainOf (proc, P_mutSeed));
    g.drawText ("DNA  (Mutate " + juce::String (juce::roundToInt (amt * 100)) + "%, seed #" + juce::String (seed) + ")", 12, 6, 400, 18, juce::Justification::centredLeft);

    // how hard the seed pushes each group: mean |direction| of its parameters
    MutationTable t; t.build ((uint32_t) seed);
    float push[ML_COUNT] = {}; int cnt[ML_COUNT] = {};
    for (int i = 0; i < P_COUNT; ++i) { const int gI = mutLockOf (i); if (gI >= 0) { push[gI] += std::abs (t.dir[i]); ++cnt[gI]; } }

    const float x0 = 40.0f, x1 = r.getWidth() - 40.0f, cy = r.getHeight() * 0.48f, amp = r.getHeight() * 0.22f;
    juce::Path a, b;
    for (int i = 0; i <= 200; ++i)
    {
        const float x = x0 + (x1 - x0) * i / 200.0f;
        const float ph = spin + 4.0f * juce::MathConstants<float>::pi * i / 200.0f;
        const float ya = cy + amp * std::sin (ph), yb = cy - amp * std::sin (ph);
        if (i == 0) { a.startNewSubPath (x, ya); b.startNewSubPath (x, yb); } else { a.lineTo (x, ya); b.lineTo (x, yb); }
    }
    g.setColour (col::accent.withAlpha (0.7f));
    g.strokePath (a, juce::PathStrokeType (2.0f));
    g.setColour (col::wavetable.withAlpha (0.7f));
    g.strokePath (b, juce::PathStrokeType (2.0f));
    g.setFont (juce::Font (juce::FontOptions (10.0f)));
    for (int k = 0; k < ML_COUNT; ++k)
    {
        const float u = (k + 0.5f) / ML_COUNT;
        const float x = x0 + (x1 - x0) * u;
        const float ph = spin + 4.0f * juce::MathConstants<float>::pi * u;
        const float ya = cy + amp * std::sin (ph), yb = cy - amp * std::sin (ph);
        const bool locked = plainOf (proc, P_mutLock1 + k) > 0.5f;
        const float strength = cnt[k] > 0 ? amt * push[k] / (float) cnt[k] : 0.0f;
        const auto c = locked ? col::muted.withAlpha (0.5f) : col::complex.interpolatedWith (col::supersaw, 1.0f - juce::jlimit (0.0f, 1.0f, strength * 2.0f))
                                                                .withAlpha (0.35f + 0.65f * juce::jlimit (0.0f, 1.0f, strength * 2.0f + 0.2f));
        g.setColour (c);
        g.drawLine (x, ya, x, yb, locked ? 2.0f : 3.0f + 4.0f * strength);
        g.setColour (locked ? col::muted : col::text);
        g.drawFittedText (juce::String (locked ? "[locked] " : "") + kMutLockNames[k], juce::Rectangle<int> ((int) (x - 45), (int) (r.getHeight() - 30), 90, 26),
                          juce::Justification::centredTop, 2);
    }
}

} // namespace tgui
