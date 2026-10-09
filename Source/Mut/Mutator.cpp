#include "Mutator.h"
#include "../Registry.h"

namespace tg
{

namespace
{
    uint32_t fnv (const juce::String& s)
    {
        uint32_t h = 2166136261u;
        for (auto c : s) { h ^= (uint32_t) c; h *= 16777619u; }
        return h;
    }

    // splitmix32-style hash to a float in [0, 1)
    inline float hash01 (uint32_t x)
    {
        x += 0x9E3779B9u; x ^= x >> 16; x *= 0x85EBCA6Bu; x ^= x >> 13; x *= 0xC2B2AE35u; x ^= x >> 16;
        return (x & 0xFFFFFF) / 16777216.0f;
    }

    std::vector<MutInfo> buildInfo()
    {
        std::vector<MutInfo> out ((size_t) P_COUNT);
        for (int i = 0; i < P_COUNT; ++i)
        {
            const auto& m = meta (i);
            MutInfo inf;
            inf.hash = fnv (m.id);
            const bool waveChoice = m.scale == Scale::Choice && (m.id.endsWith ("Wave") || m.id == "complexWaveA" || m.id == "complexWaveB")
                                    && ! m.id.startsWith ("lfo");
            const bool mutable_ = (m.modulatable && m.scale != Scale::Choice && m.scale != Scale::Toggle) || waveChoice;
            const bool excluded = m.id.startsWith ("mut") || m.id.startsWith ("macro") || m.id == "sceneX" || m.id == "sceneY"
                                  || m.id == "masterVolume" || m.group == MutGroup::Rhythm || m.group == MutGroup::None;
            if (! mutable_ || excluded) { out[(size_t) i] = inf; continue; }

            // lock group
            if (m.module == "Feedback Matrix") inf.lock = ML_Feedback;
            else if (m.module == "Granular" || m.module == "Spectral") inf.lock = ML_Bus;
            else if (m.module == "Resonator") inf.lock = ML_Reso;
            else if (m.module == "Wave Mutation" || m.module == "Audio-Rate Transform") inf.lock = ML_Wave;
            else if (m.module == "DNA Splice") inf.lock = ML_Dna;
            else if (m.module == "Cell Instability" || m.module == "Mod Matrix") inf.lock = ML_Mod;
            else switch (m.group)
            {
                case MutGroup::Pitch:       inf.lock = ML_Pitch; break;
                case MutGroup::Oscillators: inf.lock = m.category == Category::Mixer ? ML_Levels : ML_Osc; break;
                case MutGroup::Wavetable:   inf.lock = ML_Wavetable; break;
                case MutGroup::Envelopes:   inf.lock = ML_Env; break;
                case MutGroup::Filter:      inf.lock = ML_Filter; break;
                case MutGroup::Modulation:  inf.lock = ML_Mod; break;
                case MutGroup::Fx:          inf.lock = ML_Fx; break;
                default:                    inf.lock = ML_None; break;
            }

            // shape and depth
            if (waveChoice) inf.shape = MutInfo::Choice;
            else if (m.id.endsWith ("Semi") || m.id.endsWith ("Oct")) { inf.shape = MutInfo::Semitone; inf.depth = 0.3f; }
            else if (m.id.containsIgnoreCase ("Detune") || m.id == "supersawDrift") inf.depth = 0.15f;
            else if (m.id.containsIgnoreCase ("feedback") || m.id.startsWith ("fb") || m.id == "resFeedback") inf.shape = MutInfo::Stable;
            else if (m.id == "complexRatio" || m.id == "arRatio") inf.shape = MutInfo::Ratio;
            else if (m.category == Category::Env && m.id.endsWithChar ('A')) inf.shape = MutInfo::Attack;
            if (m.category == Category::Mixer) inf.depth = 0.25f;
            if (m.id.startsWith ("mod") && m.id.endsWith ("Amt")) inf.depth = 0.25f;
            out[(size_t) i] = inf;
        }
        return out;
    }
}

const MutInfo& mutInfo (int p)
{
    static const std::vector<MutInfo> table = buildInfo();
    return table[(size_t) juce::jlimit (0, (int) P_COUNT - 1, p)];
}

int mutLockOf (int p) { return mutInfo (p).lock; }

void MutationTable::build (uint32_t newSeed)
{
    seed = newSeed;
    float gene[ML_COUNT];
    for (int g = 0; g < ML_COUNT; ++g) gene[g] = hash01 (newSeed * 7919u + (uint32_t) g * 104729u) * 2.0f - 1.0f;
    for (int i = 0; i < P_COUNT; ++i)
    {
        const auto& inf = mutInfo (i);
        const float own = hash01 (newSeed ^ inf.hash) * 2.0f - 1.0f;
        // correlated: the group's gene plus the parameter's own variation
        dir[i] = inf.lock >= 0 ? juce::jlimit (-1.0f, 1.0f, 0.6f * gene[inf.lock] + 0.6f * own) : 0.0f;
        pick[i] = hash01 ((newSeed + 0x51ED27u) ^ (inf.hash * 31u));
    }
}

static float snapRatio (float r)
{
    static const float ratios[] = { 0.125f, 0.25f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 2.5f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 10.0f, 12.0f, 16.0f };
    float best = ratios[0];
    for (float x : ratios) if (std::abs (std::log2 (x / r)) < std::abs (std::log2 (best / r))) best = x;
    return best;
}

void applyMutation (const MutationTable& t, float amount, uint32_t lockMask, float* v, bool globalOnly)
{
    if (amount <= 1.0e-4f) return;
    amount = juce::jlimit (0.0f, 1.0f, amount);
    const auto& nt = normTable();
    for (int i = 0; i < P_COUNT; ++i)
    {
        const auto& inf = mutInfo (i);
        if (inf.lock < 0 || (lockMask >> inf.lock) & 1u) continue;
        if (globalOnly && ! nt.global[i]) continue;
        const float base = v[i];
        if (inf.shape == MutInfo::Choice)
        {
            // a waveform switches only when the amount passes this parameter's own threshold
            if (amount > 0.35f + 0.6f * t.pick[i])
            {
                const int n = (int) std::lround (nt.span[i]) + 1;
                const int shift = 1 + (int) std::floor (std::abs (t.dir[i]) * (n - 1) * 0.999f);
                v[i] = (float) (((int) std::lround (base) + shift) % std::max (1, n));
            }
            continue;
        }
        const float b = normFast (nt, i, base);
        if (b <= 1.0e-6f && nt.lo[i] >= 0.0f) continue;   // switched off (level, mix, depth at zero) stays off
        float off = amount * inf.depth * t.dir[i];
        switch (inf.shape)
        {
            case MutInfo::Attack:   if (b < 0.15f) off *= amount * amount; break;           // short attacks stay short until extreme amounts
            case MutInfo::Stable:   off = amount * inf.depth * (0.5f * t.dir[i] - 0.5f * std::abs (t.dir[i])); break;   // feedback favours going down
            default: break;
        }
        float out = plainFast (nt, i, b + off);
        if (inf.shape == MutInfo::Semitone) out = base + std::round (out - base);   // whole semitones / octaves (keeps any modulation underneath)
        else if (inf.shape == MutInfo::Ratio) out += (snapRatio (out) - out) * (1.0f - 0.5f * amount);   // pulled towards harmonic ratios
        v[i] = out;
    }
}

} // namespace tg
