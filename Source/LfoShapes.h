#pragma once
// Custom LFO shapes: up to 32 points per LFO (x 0..1 across one cycle, y 0..1), each with a curve
// for the segment that follows it (linear, smooth or stepped, with a bend). The message thread
// edits the points and re-renders a 512-entry table per LFO; the audio thread only reads the tables.
#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <vector>

namespace tg
{

class LfoShapeStore
{
public:
    static constexpr int kLfos = 4, kMaxPoints = 32, kTable = 512;
    enum Curve { CV_Linear, CV_Smooth, CV_Step };
    struct Point { float x = 0, y = 0; int curve = CV_Linear; float bend = 0; };   // bend -1..1 (linear segments)
    enum Preset { PR_SawDown, PR_SawUp, PR_Square, PR_Triangle, PR_Wub, PR_Stairs, PR_RandomSteps, PR_COUNT };
    static const char* presetName (int p)
    {
        static const char* n[PR_COUNT] = { "Saw down", "Saw up", "Square", "Triangle", "Wub", "Stairs", "Random steps" };
        return n[juce::jlimit (0, PR_COUNT - 1, p)];
    }

    LfoShapeStore() { for (int k = 0; k < kLfos; ++k) setPreset (k, PR_Triangle); }

    // ---- message thread
    std::vector<Point> getPoints (int k) const { const juce::ScopedLock sl (lock); return pts[(size_t) clampK (k)]; }
    void setPoints (int k, std::vector<Point> p)
    {
        k = clampK (k);
        for (auto& q : p) { q.x = juce::jlimit (0.0f, 1.0f, q.x); q.y = juce::jlimit (0.0f, 1.0f, q.y); q.curve = juce::jlimit (0, 2, q.curve); q.bend = juce::jlimit (-1.0f, 1.0f, q.bend); }
        std::stable_sort (p.begin(), p.end(), [] (const Point& a, const Point& b) { return a.x < b.x; });
        if (p.size() > (size_t) kMaxPoints) p.resize ((size_t) kMaxPoints);
        if (p.empty()) p.push_back ({ 0.0f, 0.5f });
        p.front().x = 0.0f;   // the first point always sits at the start of the cycle
        {
            const juce::ScopedLock sl (lock);
            pts[(size_t) k] = p;
        }
        render (k);
        version.fetch_add (1);
    }
    void setPreset (int k, int preset, uint32_t seed = 1)
    {
        std::vector<Point> p;
        switch (preset)
        {
            case PR_SawDown:  p = { { 0.0f, 1.0f }, { 0.999f, 0.0f, CV_Step } }; break;
            case PR_SawUp:    p = { { 0.0f, 0.0f }, { 0.999f, 1.0f, CV_Step } }; break;
            case PR_Square:   p = { { 0.0f, 1.0f, CV_Step }, { 0.5f, 0.0f, CV_Step } }; break;
            case PR_Wub:      p = { { 0.0f, 0.05f, CV_Linear, -0.7f }, { 0.35f, 1.0f, CV_Smooth }, { 0.55f, 0.6f, CV_Linear, 0.5f }, { 0.85f, 0.05f } }; break;
            case PR_Stairs:   for (int i = 0; i < 8; ++i) p.push_back ({ i / 8.0f, i / 7.0f, CV_Step }); break;
            case PR_RandomSteps:
            {
                uint32_t r = seed * 2654435761u + 12345u;
                for (int i = 0; i < 16; ++i) { r = r * 1664525u + 1013904223u; p.push_back ({ i / 16.0f, (float) ((r >> 8) & 0xFFFF) / 65535.0f, CV_Step }); }
                break;
            }
            case PR_Triangle:
            default:          p = { { 0.0f, 0.5f }, { 0.25f, 1.0f }, { 0.75f, 0.0f } }; break;
        }
        setPoints (k, p);
    }

    juce::var toVar() const
    {
        juce::Array<juce::var> all;
        for (int k = 0; k < kLfos; ++k)
        {
            juce::StringArray a;
            for (auto& q : getPoints (k)) a.add (juce::String (q.x, 4) + ":" + juce::String (q.y, 4) + ":" + juce::String (q.curve) + ":" + juce::String (q.bend, 3));
            all.add (a.joinIntoString (","));
        }
        return juce::var (all);
    }
    void fromVar (const juce::var& v)   // missing or invalid: the default triangles
    {
        for (int k = 0; k < kLfos; ++k)
        {
            std::vector<Point> p;
            if (v.isArray() && k < v.size())
            {
                juce::StringArray a; a.addTokens (v[k].toString(), ",", {});
                for (auto& t : a)
                {
                    juce::StringArray f; f.addTokens (t, ":", {});
                    if (f.size() >= 2) p.push_back ({ f[0].getFloatValue(), f[1].getFloatValue(), f.size() > 2 ? f[2].getIntValue() : 0, f.size() > 3 ? f[3].getFloatValue() : 0.0f });
                }
            }
            if (p.empty()) setPreset (k, PR_Triangle); else setPoints (k, p);
        }
    }
    juce::String toJson() const { return juce::JSON::toString (toVar(), true); }
    uint32_t getVersion() const { return version.load(); }

    // ---- audio thread: the shape at a phase (0..1), 0..1
    float eval (int k, double phase) const
    {
        const auto& t = table[(size_t) clampK (k)];
        const double p = (phase - std::floor (phase)) * kTable;
        const int i0 = (int) p;
        const float f = (float) (p - i0);
        const float a = t[(size_t) (i0 & (kTable - 1))].load (std::memory_order_relaxed);
        const float b = t[(size_t) ((i0 + 1) & (kTable - 1))].load (std::memory_order_relaxed);
        return stepAt[(size_t) clampK (k)][(size_t) (i0 & (kTable - 1))].load (std::memory_order_relaxed) ? a : a + (b - a) * f;
    }

    // the curve itself, exactly (for drawing and tests)
    static float shapeAt (const std::vector<Point>& p, double x, bool* stepped = nullptr)
    {
        if (stepped) *stepped = false;
        if (p.empty()) return 0.5f;
        x = x - std::floor (x);
        size_t i = 0;
        while (i + 1 < p.size() && p[i + 1].x <= x) ++i;
        const Point& a = p[i];
        const Point& b = i + 1 < p.size() ? p[i + 1] : p.front();
        const double bx = i + 1 < p.size() ? b.x : 1.0;
        const double span = std::max (1.0e-6, bx - a.x);
        const double u = juce::jlimit (0.0, 1.0, (x - a.x) / span);
        switch (a.curve)
        {
            case CV_Step:   if (stepped) *stepped = true; return a.y;
            case CV_Smooth: return (float) (a.y + (b.y - a.y) * (0.5 - 0.5 * std::cos (juce::MathConstants<double>::pi * u)));
            default:
            {
                // bend: >0 bows the segment up (fast start), <0 down (slow start)
                const double e = std::pow (2.0, -3.0 * a.bend);
                return (float) (a.y + (b.y - a.y) * std::pow (u, e));
            }
        }
    }

private:
    static int clampK (int k) { return juce::jlimit (0, kLfos - 1, k); }
    void render (int k)
    {
        const auto p = getPoints (k);
        for (int i = 0; i < kTable; ++i)
        {
            bool st = false;
            const float y = shapeAt (p, (double) i / kTable, &st);
            // a step edge inside this cell: hold, so the jump isn't smeared over a cell
            bool st2 = false; shapeAt (p, (double) (i + 1) / kTable - 1.0e-9, &st2);
            table[(size_t) k][(size_t) i].store (y, std::memory_order_relaxed);
            stepAt[(size_t) k][(size_t) i].store (st || st2, std::memory_order_relaxed);
        }
    }
    juce::CriticalSection lock;
    std::array<std::vector<Point>, kLfos> pts;
    std::array<std::array<std::atomic<float>, kTable>, kLfos> table;
    std::array<std::array<std::atomic<bool>, kTable>, kLfos> stepAt;
    std::atomic<uint32_t> version { 0 };
};

} // namespace tg
