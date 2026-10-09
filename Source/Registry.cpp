#include "Registry.h"
#include "ModMatrix.h"

namespace tg
{

namespace
{
    struct RawDef { const char* id; const char* name; float mn, mx, df, st, sk; int kind; const ChoiceList* list; };
    enum { KFloat, KChoice, KBool, KAmount };

    const RawDef kRaw[] = {
       #define TG_F(id, name, mn, mx, df, st, sk) { #id, name, (float) (mn), (float) (mx), (float) (df), (float) (st), (float) (sk), KFloat, nullptr },
       #define TG_C(id, name, list, df) { #id, name, 0.0f, 0.0f, (float) (df), 1.0f, 0.0f, KChoice, &list },
       #define TG_B(id, name, df) { #id, name, 0.0f, 1.0f, (df) ? 1.0f : 0.0f, 1.0f, 0.0f, KBool, nullptr },
       #define TG_A(id, name, df) { #id, name, -1.0f, 1.0f, (float) (df), 0.0f, 0.0f, KAmount, nullptr },
        TG_PARAMS (TG_F, TG_C, TG_B, TG_A)
       #undef TG_F
       #undef TG_C
       #undef TG_B
       #undef TG_A
    };
    static_assert (sizeof (kRaw) / sizeof (kRaw[0]) == P_COUNT, "registry out of step with TG_PARAMS");

    bool isAny (const juce::String& id, std::initializer_list<const char*> ids)
    {
        for (auto* s : ids) if (id == s) return true;
        return false;
    }

    bool startsAny (const juce::String& id, std::initializer_list<const char*> prefixes)
    {
        for (auto* s : prefixes) if (id.startsWith (s)) return true;
        return false;
    }

    juce::String moduleFor (const juce::String& id, Category c)
    {
        if (id.startsWith ("osc1")) return "Oscillator 1";
        if (id.startsWith ("osc2")) return "Oscillator 2";
        if (id.startsWith ("osc3")) return "Oscillator 3";
        if (id.startsWith ("osc4")) return "Wavetable 1";
        if (id.startsWith ("wt2")) return "Wavetable 2";
        if (id.startsWith ("sub")) return "Sub Oscillator";
        if (id.startsWith ("complex")) return "Complex";
        if (id.startsWith ("supersaw")) return "SuperSaw";
        if (id.startsWith ("fmSlot") || id == "fmAmount") return "FM Matrix";
        if (id.startsWith ("ringSlot") || id.startsWith ("ring")) return "Ring Mod";
        if (id.startsWith ("amp")) return "Amp Envelope";
        if (id.startsWith ("fEnv")) return "Filter Envelope";
        if (id.startsWith ("mEnv")) return "Mod Envelope " + id.substring (4, 5);
        if (id.startsWith ("lfoAssign")) return "LFO Slots";
        if (id.startsWith ("mod") && id.endsWith ("Amt")) return "Mod Matrix";
        if (id == "randRate") return "Random";
        if (id.startsWith ("macro")) return "Macros";
        if (id.startsWith ("scene")) return "Scenes";
        if (id.startsWith ("cc")) return "MIDI";
        if (id.startsWith ("envAssign")) return "Envelope Slots";
        if (id.startsWith ("lfo")) return "LFO " + id.substring (3, 4);
        if (id.startsWith ("delay") || id.startsWith ("tape") || id.startsWith ("flutter")) return "Tape Delay";
        if (id.startsWith ("reverb") || id.startsWith ("shimmer")) return "Reverb + Shimmer";
        if (id.startsWith ("chorus")) return "Juno Chorus";
        if (id.startsWith ("reverse")) return "Reverse Reverb";
        switch (c)
        {
            case Category::Performance: return "Performance";
            case Category::Filter:      return "Filter";
            case Category::Seq:         return "Sequencer";
            case Category::Analog:      return "Analog Warmth";
            default:                    return "Mixer";
        }
    }

    juce::String unitFor (const juce::String& id)
    {
        if (id == "filterCutoff" || id.endsWith ("Tone") || id == "shimmerBright" || id.endsWith ("Rate")) return "Hz";
        if (id.containsIgnoreCase ("Detune") || id == "supersawSpread" || id == "supersawDrift") return "cents";
        if (id.endsWith ("Semi") || id == "bendRange") return "st";
        if (id.endsWith ("Oct") || id == "keyboardOctave") return "oct";
        if (id == "chorusDepth" || id == "tapeFlutter" || id == "reversePitch") return "ms";
        if (id == "porta" || id == "delayTime" || id == "reverseTime" || id == "reverbSize") return "s";
        if ((id.startsWith ("amp") || id.startsWith ("fEnv") || id.startsWith ("mEnv")) && ! id.endsWithChar ('S') && id != "fEnvAmt") return "s";
        if (id == "seqTempo") return "BPM";
        if (id == "polyphony" || id == "supersawVoices") return "voices";
        if (id.endsWith ("Root")) return "note";
        if (id.endsWith ("Position") || id.endsWith ("Window") || id.endsWith ("LoopStart") || id.endsWith ("LoopEnd")
            || id == "seqAccentAmt" || id == "velSens") return "%";
        return {};
    }

    std::vector<ParamMeta> build()
    {
        std::vector<ParamMeta> out;
        out.reserve (P_COUNT);
        for (int i = 0; i < P_COUNT; ++i)
        {
            const RawDef& r = kRaw[i];
            const juce::String id (r.id);
            ParamMeta m;
            m.index = i;
            m.id = id;
            m.name = r.name;
            m.def = r.df;
            m.step = r.st;
            m.skewCentre = r.sk;
            m.choices = r.list;
            if (r.kind == KChoice) { m.min = 0; m.max = (float) (r.list->size - 1); m.scale = Scale::Choice; }
            else if (r.kind == KBool) { m.min = 0; m.max = 1; m.scale = Scale::Toggle; }
            else
            {
                m.min = r.mn; m.max = r.mx;
                const bool integer = r.st >= 1.0f && std::floor (r.mn) == r.mn && std::floor (r.mx) == r.mx;
                m.scale = r.sk > 0 ? Scale::Log : (integer ? Scale::Integer : Scale::Linear);
            }
            m.bipolar = m.min < 0 && m.max > 0;

            // ---- category and mutation group
            const bool pitchy = id.endsWith ("Oct") || id.endsWith ("Semi") || id.containsIgnoreCase ("Detune");
            if (isAny (id, { "masterVolume", "polyphony", "keyboardOctave", "porta", "velSens", "bendRange" }))
                { m.category = Category::Performance; m.group = id == "porta" ? MutGroup::Pitch : MutGroup::None; }
            else if (isAny (id, { "warmth", "bassKeep", "analogDrift" }))
                { m.category = Category::Analog; m.group = id == "bassKeep" ? MutGroup::Filter : MutGroup::Oscillators; }
            else if (isAny (id, { "delayMix", "reverbMix", "shimmerMix", "chorusMix", "reverseMix" }))
                { m.category = Category::Fx; m.group = MutGroup::Fx; }
            else if (id.endsWith ("Gain") || id == "fmAmount" || id == "ringMix" || startsAny (id, { "fmSlot", "ringSlot" }))
                { m.category = Category::Mixer; m.group = MutGroup::Oscillators; }
            else if (startsAny (id, { "osc4", "wt2" }))
                { m.category = Category::Wavetable; m.group = pitchy ? MutGroup::Pitch : MutGroup::Wavetable; }
            else if (startsAny (id, { "osc1", "osc2", "osc3", "sub", "complex", "supersaw" }))
                { m.category = Category::Osc; m.group = pitchy ? MutGroup::Pitch : MutGroup::Oscillators; }
            else if (id.startsWith ("filter") || id == "fEnvAmt")
                { m.category = Category::Filter; m.group = MutGroup::Filter; }
            else if (startsAny (id, { "fEnv", "amp", "mEnv" }))
                { m.category = Category::Env; m.group = MutGroup::Envelopes; }
            else if (startsAny (id, { "delay", "tape", "flutter", "reverb", "shimmer", "chorus", "reverse" }))
                { m.category = Category::Fx; m.group = MutGroup::Fx; }
            else if (isAny (id, { "ccANum", "ccBNum" }))
                { m.category = Category::Performance; m.group = MutGroup::None; }
            else if (id.startsWith ("mod") && id.endsWith ("Amt"))
                { m.category = Category::Mod; m.group = MutGroup::Modulation; }
            else if (id.startsWith ("macro") || isAny (id, { "sceneX", "sceneY", "sceneMorph" }))
                { m.category = Category::Mod; m.group = MutGroup::None; }
            else if (id == "randRate")
                { m.category = Category::Lfo; m.group = MutGroup::Modulation; }
            else if (startsAny (id, { "lfoAssign", "envAssign" }))
                { m.category = Category::Mod; m.group = MutGroup::Modulation; }
            else if (id.startsWith ("lfo"))
                { m.category = Category::Lfo; m.group = MutGroup::Modulation; }
            else if (id.startsWith ("seq"))
                { m.category = Category::Seq; m.group = MutGroup::Rhythm; }

            m.module = moduleFor (id, m.category);
            m.unit = unitFor (id);
            m.path = juce::String (categoryName (m.category)).toLowerCase() + "." + id;

            // ---- modulation capability: continuous sound parameters yes; administrative ones no
            const bool admin = isAny (id, { "polyphony", "keyboardOctave", "bendRange", "seqLength", "seqEuclidPulses",
                                            "seqEuclidRotate", "seqTempo", "osc4Root", "wt2Root", "ccANum", "ccBNum" });
            m.modulatable = (r.kind == KFloat || r.kind == KAmount) && ! admin;
            float adScale;
            m.audioRate = m.modulatable && audioDestFor (i, adScale) >= 0;   // what the voice can drive per sample

            // ---- smoothing: envelope times are read at note-on; pitch follows portamento
            if (m.category == Category::Env && m.unit == "s") m.smoothingMs = 0.0f;
            else if (m.group == MutGroup::Pitch) m.smoothingMs = 0.0f;
            else if (m.category == Category::Filter || m.category == Category::Mixer) m.smoothingMs = 10.0f;
            else m.smoothingMs = 20.0f;

            out.push_back (m);
        }
        return out;
    }
}

const std::vector<ParamMeta>& registry()
{
    static const std::vector<ParamMeta> table = build();
    return table;
}

const ParamMeta& meta (int i) { return registry()[(size_t) juce::jlimit (0, (int) P_COUNT - 1, i)]; }

int indexForId (const juce::String& id)
{
    for (auto& m : registry()) if (m.id == id) return m.index;
    return -1;
}

std::vector<int> modulationDestinations()
{
    std::vector<int> out;
    for (auto& m : registry()) if (m.modulatable) out.push_back (m.index);
    return out;
}

std::vector<int> parametersInGroup (MutGroup g)
{
    std::vector<int> out;
    for (auto& m : registry()) if (m.group == g) out.push_back (m.index);
    return out;
}

const char* categoryName (Category c)
{
    switch (c)
    {
        case Category::Performance: return "Performance";
        case Category::Osc:         return "Osc";
        case Category::Wavetable:   return "Wavetable";
        case Category::Mixer:       return "Mixer";
        case Category::Filter:      return "Filter";
        case Category::Env:         return "Env";
        case Category::Lfo:         return "LFO";
        case Category::Fx:          return "FX";
        case Category::Mod:         return "Mod";
        case Category::Seq:         return "Sequencer";
        case Category::Analog:      return "Analog";
    }
    return "";
}

const char* mutGroupName (MutGroup g)
{
    switch (g)
    {
        case MutGroup::None:        return "None";
        case MutGroup::Pitch:       return "Pitch";
        case MutGroup::Oscillators: return "Oscillators";
        case MutGroup::Wavetable:   return "Wavetable";
        case MutGroup::Envelopes:   return "Envelopes";
        case MutGroup::Filter:      return "Filter";
        case MutGroup::Modulation:  return "Modulation";
        case MutGroup::Fx:          return "FX";
        case MutGroup::Rhythm:      return "Rhythm";
    }
    return "";
}

const char* scaleName (Scale s)
{
    switch (s)
    {
        case Scale::Linear:  return "Linear";
        case Scale::Log:     return "Logarithmic";
        case Scale::Integer: return "Integer";
        case Scale::Choice:  return "Choice";
        case Scale::Toggle:  return "Toggle";
    }
    return "";
}

static juce::NormalisableRange<float> rangeFor (const ParamMeta& m)
{
    juce::NormalisableRange<float> r (m.min, m.max, m.scale == Scale::Choice || m.scale == Scale::Toggle ? 1.0f : m.step);
    if (m.skewCentre > 0) r.setSkewForCentre (m.skewCentre);
    return r;
}

float toNormalised (int i, float plain)
{
    const auto& m = meta (i);
    if (m.max <= m.min) return 0.0f;
    static const std::vector<juce::NormalisableRange<float>> ranges = []
    {
        std::vector<juce::NormalisableRange<float>> v;
        for (auto& p : registry()) v.push_back (rangeFor (p));
        return v;
    }();
    auto r = ranges[(size_t) m.index];
    r.interval = 0;   // no snapping for modulation maths
    return r.convertTo0to1 (juce::jlimit (m.min, m.max, plain));
}

float fromNormalised (int i, float norm)
{
    const auto& m = meta (i);
    auto r = rangeFor (m);
    r.interval = 0;
    return r.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, norm));
}

} // namespace tg
