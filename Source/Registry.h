#pragma once
// Central parameter registry.
//
// Every parameter in TG_PARAMS (Params.h) gets one ParamMeta record here: module,
// category, scaling, unit, bipolar flag, smoothing time, whether it can be modulated
// (and at audio rate), and which mutation group it belongs to. The modulation
// destination list, UI captions/tooltips, preset storage and the mutation engine all
// read this table instead of keeping their own lists.

#include <JuceHeader.h>
#include <vector>
#include "Params.h"

namespace tg
{

enum class Category { Performance, Osc, Wavetable, Mixer, Filter, Env, Lfo, Fx, Mod, Seq, Analog, Mutation };
enum class Scale    { Linear, Log, Integer, Choice, Toggle };
enum class MutGroup { None, Pitch, Oscillators, Wavetable, Envelopes, Filter, Modulation, Fx, Rhythm };

struct ParamMeta
{
    int index = 0;
    juce::String id;          // stable host / patch id (the browser synth's control id)
    juce::String path;        // dotted id for search and display, e.g. "filter.filterCutoff"
    juce::String name;
    juce::String module;      // panel the control lives on, e.g. "Oscillator 1"
    Category category = Category::Osc;
    MutGroup group = MutGroup::None;
    Scale scale = Scale::Linear;
    juce::String unit;
    float min = 0, max = 1, def = 0, step = 0, skewCentre = 0;
    bool bipolar = false;
    bool modulatable = false;
    bool audioRate = false;   // may be driven per sample by an audio-rate source
    float smoothingMs = 20.0f;
    bool serialized = true;
    const ChoiceList* choices = nullptr;
};

const std::vector<ParamMeta>& registry();
const ParamMeta& meta (int paramIndex);
int indexForId (const juce::String& id);                     // -1 when unknown
std::vector<int> modulationDestinations();                    // every modulatable parameter, in table order
std::vector<int> parametersInGroup (MutGroup);
const char* categoryName (Category);
const char* mutGroupName (MutGroup);
const char* scaleName (Scale);

// Normalised position (0..1) of a plain value, honouring the parameter's scaling,
// and back. Used by modulation depths and mutation so "+10%" means the same
// perceptual distance everywhere.
float toNormalised (int paramIndex, float plain);
float fromNormalised (int paramIndex, float norm);

} // namespace tg
