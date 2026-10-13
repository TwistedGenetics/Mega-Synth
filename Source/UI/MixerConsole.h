#pragma once
// The Mixer as a console, in signal order: a channel strip per source (level, pan, unison, mute / solo,
// a meter and whether the pitch envelope moves it), the mutation engines (lit when on; click to open),
// then the output. Included from PluginEditor.cpp after PluginEditor.h (it uses Knob and the colours).
#include <JuceHeader.h>

namespace tgui
{

// A vertical level meter: the peak since the last frame, falling back slowly, with a held peak line
class LevelMeter : public juce::Component
{
public:
    void push (float peak)
    {
        level = std::max (peak, level * 0.85f);
        if (peak >= hold) { hold = peak; holdFrames = 45; }
        else if (--holdFrames < 0) hold = std::max (level, hold * 0.95f);
        repaint();
    }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (col::panel2); g.fillRoundedRectangle (r, 2.0f);
        auto yFor = [&] (float v)
        {
            const float db = v > 1.0e-6f ? 20.0f * std::log10 (v) : -96.0f;
            return r.getBottom() - juce::jlimit (0.0f, 1.0f, (db + 48.0f) / 54.0f) * r.getHeight();   // -48 .. +6 dB
        };
        const float y = yFor (level), y0 = yFor (1.0f);
        g.setColour (col::mixer.withAlpha (0.9f));
        g.fillRect (juce::Rectangle<float> (r.getX(), juce::jmax (y, y0), r.getWidth(), r.getBottom() - juce::jmax (y, y0)));
        if (y < y0) { g.setColour (juce::Colour (0xffef4444)); g.fillRect (juce::Rectangle<float> (r.getX(), y, r.getWidth(), y0 - y)); }
        g.setColour (col::muted.withAlpha (0.6f)); g.drawHorizontalLine ((int) y0, r.getX(), r.getRight());   // 0 dB
        if (hold > 1.0e-5f) { g.setColour (hold > 1.0f ? juce::Colour (0xffef4444) : col::text); g.drawHorizontalLine ((int) yFor (hold), r.getX(), r.getRight()); }
    }
private:
    float level = 0.0f, hold = 0.0f;
    int holdFrames = 0;
};

// One source's strip
class ChannelStrip : public juce::Component, public juce::SettableTooltipClient
{
public:
    ChannelStrip (MegaSynthProcessor& p, int strip, const juce::String& name, int levelParam, int extraParam, const juce::String& extraCaption, int pEnvTarget, juce::Colour c)
        : proc (p), index (strip), title (name), colour (c), pEnvIndex (pEnvTarget)
    {
        const int b = tg::mxParam (strip);
        level = std::make_unique<Knob> (p, levelParam, "Level", c);
        pan = std::make_unique<Knob> (p, b, "Pan", c);
        addAndMakeVisible (*level); addAndMakeVisible (*pan);
        if (extraParam >= 0) { unison = std::make_unique<Knob> (p, extraParam, extraCaption, c); addAndMakeVisible (*unison); }
        for (auto* t : { &mute, &solo })
        {
            t->setClickingTogglesState (true);
            t->setColour (juce::TextButton::buttonOnColourId, t == &mute ? juce::Colour (0xffef4444) : juce::Colour (0xfff59e0b));
            addAndMakeVisible (*t);
        }
        mute.setTooltip ("Mute: takes " + name + " out of the mix (it still works as an FM / ring modulator)");
        solo.setTooltip ("Solo: only soloed sources are heard");
        muteAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, tg::kParamIds[b + 1], tagParam (mute, b + 1));
        soloAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, tg::kParamIds[b + 2], tagParam (solo, b + 2));
        addAndMakeVisible (meter);
    }
    Knob* levelKnob() const { return level.get(); }
    std::function<void()> onOpen;   // (v0.1.74) a click on the name: that source's page
    void mouseDown (const juce::MouseEvent& e) override { if (e.y < 34 && ! e.mods.isPopupMenu() && onOpen) onOpen(); }
    void mouseMove (const juce::MouseEvent& e) override { setMouseCursor (e.y < 34 && onOpen ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor); }
    void pushMeter (float pk) { meter.push (pk); }
    void refresh()
    {
        // the pitch envelope's target: all oscillators, or this one
        bool moves = false;
        if (pEnvIndex >= 0 && std::abs (plain (tg::P_pEnvAmt)) > 1.0e-4f)
        {
            const int t = juce::roundToInt (plain (tg::P_pEnvTarget));
            moves = t == 0 || t == pEnvIndex;
        }
        if (moves != pEnvLit) { pEnvLit = moves; repaint(); }
    }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (col::panel); g.fillRoundedRectangle (r, 8.0f);
        g.setColour (col::border); g.drawRoundedRectangle (r, 8.0f, 1.0f);
        g.setColour (colour); g.fillRoundedRectangle (juce::Rectangle<float> (r.getX() + 8, r.getY() + 8, r.getWidth() - 16, 3), 1.5f);
        g.setColour (col::text);
        g.setFont (juce::Font (juce::FontOptions (12.5f, juce::Font::bold)));
        g.drawFittedText (title, juce::Rectangle<float> (r.getX() + 2, r.getY() + 13, r.getWidth() - 4, 18).toNearestInt(), juce::Justification::centred, 1, 0.75f);
        if (pEnvIndex >= 0)
        {
            const auto tag = juce::Rectangle<float> (r.getX() + 8, r.getBottom() - 22, r.getWidth() - 16, 15);
            g.setColour (pEnvLit ? col::env : col::panel2); g.fillRoundedRectangle (tag, 4.0f);
            g.setColour (pEnvLit ? col::bg : col::muted);
            g.setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::bold)));
            g.drawFittedText ("PITCH ENV", tag.toNearestInt(), juce::Justification::centred, 1, 0.6f);
        }
    }
    void resized() override
    {
        const int w = getWidth(), mw = 6, kx = 4, kw = w - mw - 12;
        level->setBounds (kx, 34, kw, 84);
        pan->setBounds (kx, 122, kw, 84);
        if (unison != nullptr) unison->setBounds (kx, 210, kw, 84);
        meter.setBounds (w - mw - 5, 34, mw, 262);
        const int by = 302;
        const int bw = (w - 12) / 2;
        mute.setBounds (4, by, bw, 24);
        solo.setBounds (w - 4 - bw, by, bw, 24);
    }
private:
    float plain (int i) const { return proc.param (i)->convertFrom0to1 (proc.param (i)->getValue()); }
    MegaSynthProcessor& proc;
    const int index;
    juce::String title;
    juce::Colour colour;
    const int pEnvIndex;
    bool pEnvLit = false;
    std::unique_ptr<Knob> level, pan, unison;
    juce::TextButton mute { "M" }, solo { "S" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> muteAtt, soloAtt;
    LevelMeter meter;
};

// A mutation engine: lit when it's doing something; click the name to open its page
class EngineChip : public juce::Component, public juce::SettableTooltipClient
{
public:
    EngineChip (MegaSynthProcessor& p, const juce::String& name, int mixParam, std::function<bool()> isOn, juce::Colour c)
        : proc (p), title (name), on (std::move (isOn)), colour (c)
    {
        if (mixParam >= 0) { knob = std::make_unique<Knob> (p, mixParam, "Mix", c); addAndMakeVisible (*knob); }
        setTooltip ("Click to open " + name);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        juce::ignoreUnused (proc);
    }
    std::function<void()> onOpen;
    void refresh() { const bool l = on(); if (l != lit) { lit = l; repaint(); } }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (lit ? col::panel3 : col::panel2); g.fillRoundedRectangle (r, 7.0f);
        g.setColour (lit ? colour : col::border); g.drawRoundedRectangle (r, 7.0f, lit ? 1.5f : 1.0f);
        g.setColour (lit ? colour : col::muted); g.fillEllipse (r.getX() + 8, r.getY() + 9, 7, 7);
        g.setColour (lit ? col::text : col::muted);
        g.setFont (juce::Font (juce::FontOptions (11.5f, juce::Font::bold)));
        g.drawFittedText (title, juce::Rectangle<float> (r.getX() + 19, r.getY() + 4, r.getWidth() - 22, 17).toNearestInt(), juce::Justification::centredLeft, 1, 0.75f);
        if (knob == nullptr)
        {
            g.setFont (juce::Font (juce::FontOptions (10.5f)));
            g.drawText (lit ? "on" : "off", r.withTrimmedTop (24), juce::Justification::centred);
        }
    }
    void resized() override { if (knob != nullptr) knob->setBounds (4, 22, getWidth() - 8, 84); }
    void mouseUp (const juce::MouseEvent& e) override { if (e.mouseWasClicked() && ! e.mods.isPopupMenu() && onOpen) onOpen(); }
private:
    MegaSynthProcessor& proc;
    juce::String title;
    std::function<bool()> on;
    juce::Colour colour;
    bool lit = false;
    std::unique_ptr<Knob> knob;
};

} // namespace tgui
