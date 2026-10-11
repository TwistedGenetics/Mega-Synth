#pragma once
// MIDI input monitor, next to the keyboard: what the plug-in is actually receiving from the host.
// Pitch bend (bar + semitones), mod wheel (bar + what it drives), sustain pedal, the last controller,
// and a pending MIDI Learn. It only reads values the audio thread publishes.
#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "../Registry.h"

namespace tgui
{

class MidiMonitor : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    explicit MidiMonitor (MegaSynthProcessor& p) : proc (p)
    {
        setTooltip ("MIDI input: what Mega Synth is receiving from your keyboard or DAW. "
                    "If the bend or wheel bars don't move when you move them, the host isn't sending them to the plug-in.");
        startTimerHz (20);
    }
    ~MidiMonitor() override { stopTimer(); }

    void paint (juce::Graphics& g) override
    {
        const auto bg = juce::Colour (0xff1b2230), line = juce::Colour (0xff334155), text = juce::Colour (0xffe2e8f0),
                   muted = juce::Colour (0xff94a3b8), accent = juce::Colour (0xff38bdf8), warn = juce::Colour (0xfff59e0b);
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (bg); g.fillRoundedRectangle (r, 8.0f);
        g.setColour (line); g.drawRoundedRectangle (r, 8.0f, 1.0f);
        const int w = getWidth();
        g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        g.setColour (muted); g.drawText ("MIDI IN", 8, 4, w - 16, 14, juce::Justification::centredLeft);

        const float bend = proc.monBend.load(), wheel = proc.monWheel.load();
        const float range = proc.param (tg::P_bendRange)->convertFrom0to1 (proc.param (tg::P_bendRange)->getValue());
        auto bar = [&] (int y, const juce::String& label, float v, bool centred, const juce::String& value)
        {
            g.setFont (juce::Font (juce::FontOptions (10.5f)));
            g.setColour (muted); g.drawText (label, 8, y, 40, 14, juce::Justification::centredLeft);
            const juce::Rectangle<float> track (50.0f, (float) y + 3.0f, (float) w - 112.0f, 8.0f);
            g.setColour (line); g.fillRoundedRectangle (track, 3.0f);
            g.setColour (accent);
            if (centred)
            {
                const float mid = track.getCentreX(), x = mid + v * track.getWidth() * 0.5f;
                g.fillRoundedRectangle (juce::Rectangle<float> (std::min (mid, x), track.getY(), std::max (2.0f, std::abs (x - mid)), track.getHeight()), 3.0f);
            }
            else g.fillRoundedRectangle (track.withWidth (std::max (2.0f, track.getWidth() * v)), 3.0f);
            g.setColour (text); g.drawText (value, w - 60, y, 54, 14, juce::Justification::centredRight);
        };
        bar (22, "Bend", bend, true, (bend >= 0 ? "+" : "") + juce::String (bend * range, 2) + " st");
        bar (40, "Wheel", wheel, false, juce::String (juce::roundToInt (wheel * 127)));

        g.setFont (juce::Font (juce::FontOptions (10.5f)));
        g.setColour (muted);
        g.drawText ("Wheel: " + wheelTargets(), 8, 57, w - 16, 14, juce::Justification::centredLeft, true);
        const int cc = proc.monCC.load();
        juce::String last = cc >= 0 ? "Last CC " + juce::String (cc) + " = " + juce::String (proc.monCCVal.load()) : juce::String ("No controllers yet");
        if (proc.monCh.load() > 0) last << "  (ch " << proc.monCh.load() << ")";
        g.drawText (last, 8, 73, w - 16, 14, juce::Justification::centredLeft, true);
        g.setColour (proc.monSustain.load() ? accent : muted);
        g.drawText (proc.monSustain.load() ? "Sustain: down" : "Sustain: up", 8, 89, w - 16, 14, juce::Justification::centredLeft);

        const int learn = proc.learningParam();
        if (learn >= 0)
        {
            g.setColour (warn);
            g.drawText ("Learning " + tg::meta (learn).name + ": move a controller", 8, 104, w - 16, 14, juce::Justification::centredLeft, true);
        }
        else
        {
            g.setColour (muted);
            g.drawText ("Bend msgs " + juce::String (proc.monBendCount.load()) + ", CC msgs " + juce::String (proc.monCCCount.load()),
                        8, 104, w - 16, 14, juce::Justification::centredLeft, true);
        }
    }

private:
    juce::String wheelTargets() const
    {
        juce::StringArray t;
        if (proc.param (tg::P_wheelLfo1)->getValue() > 0.5f) t.add ("LFO 1 Depth");
        for (int i = 0; i < tg::kNumRoutes; ++i)
        {
            const auto r = proc.routes.get (i);
            if (r.active() && (r.src == tg::MS_ModWheel || r.via == tg::MS_ModWheel) && r.dst >= 0 && r.dst < tg::P_COUNT) t.add (tg::meta (r.dst).name);
        }
        return t.isEmpty() ? juce::String ("nothing (route it in the Mod Matrix)") : t.joinIntoString (", ");
    }
    void timerCallback() override
    {
        const auto key = juce::String (proc.monBend.load()) + "|" + juce::String (proc.monWheel.load()) + "|" + juce::String (proc.monCC.load()) + "|"
                       + juce::String (proc.monCCVal.load()) + "|" + juce::String ((int) proc.monSustain.load()) + "|" + juce::String (proc.learningParam()) + "|"
                       + juce::String ((int) proc.monCCCount.load()) + "|" + juce::String ((int) proc.monBendCount.load()) + "|" + juce::String ((int) proc.routes.getVersion());
        if (key != lastKey) { lastKey = key; repaint(); }
    }
    MegaSynthProcessor& proc;
    juce::String lastKey;
};

} // namespace tgui
