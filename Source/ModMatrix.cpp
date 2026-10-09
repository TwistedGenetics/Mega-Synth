#include "ModMatrix.h"
#include "Registry.h"

namespace tg
{

//==============================================================================
juce::var RouteStore::toVar() const
{
    juce::Array<juce::var> arr;
    for (int i = 0; i < kNumRoutes; ++i)
    {
        const RouteConfig c = get (i);
        if (c.src == MS_None && c.dst < 0) continue;
        auto* o = new juce::DynamicObject();
        o->setProperty ("slot", i);
        o->setProperty ("on", c.on);
        o->setProperty ("source", juce::String (kModSrcKeys[c.src]));
        o->setProperty ("dest", c.dst >= 0 ? juce::String (kParamIds[c.dst]) : juce::String());
        o->setProperty ("curve", c.curve);
        o->setProperty ("unipolar", c.unipolar);
        o->setProperty ("via", juce::String (kModSrcKeys[c.via]));
        o->setProperty ("viaDepth", c.viaDepth);
        o->setProperty ("smoothMs", c.smoothMs);
        arr.add (juce::var (o));
    }
    return arr;
}

void RouteStore::fromVar (const juce::var& v)
{
    RouteConfig fresh[kNumRoutes];
    if (v.isArray())
    {
        for (int k = 0; k < v.size(); ++k)
        {
            const juce::var o = v[k];
            const int slot = (int) o.getProperty ("slot", k);
            if (slot < 0 || slot >= kNumRoutes) continue;
            RouteConfig c;
            c.on = (bool) o.getProperty ("on", true);
            c.src = juce::jmax (0, modSourceForKey (o["source"].toString()));
            c.dst = indexForId (o["dest"].toString());
            c.curve = juce::jlimit (0, (int) MC_COUNT - 1, (int) o.getProperty ("curve", 0));
            c.unipolar = (bool) o.getProperty ("unipolar", false);
            c.via = juce::jmax (0, modSourceForKey (o["via"].toString()));
            c.viaDepth = (float) (double) o.getProperty ("viaDepth", 1.0);
            c.smoothMs = (float) (double) o.getProperty ("smoothMs", 0.0);
            if (c.dst >= 0 && ! meta (c.dst).modulatable) c.dst = -1;
            fresh[slot] = c;
        }
    }
    for (int i = 0; i < kNumRoutes; ++i) set (i, fresh[i]);
}

int modSourceForKey (const juce::String& key)
{
    for (int i = 0; i < MS_COUNT; ++i) if (key == kModSrcKeys[i]) return i;
    return -1;
}

//==============================================================================
int audioDestFor (int p, float& scale)
{
    scale = 1.0f;
    switch (p)
    {
        // pitch: semitone dials span 24 st (= 2 octaves of normalised travel), detune 100 cents
        case P_osc1Semi:     scale = 2.0f; return AD_P1;
        case P_osc2Semi:     scale = 2.0f; return AD_P2;
        case P_osc3Semi:     scale = 2.0f; return AD_P3;
        case P_subSemi:      scale = 2.0f; return AD_PSub;
        case P_osc4Semi:     scale = 2.0f; return AD_PWt1;
        case P_wt2Semi:      scale = 2.0f; return AD_PWt2;
        case P_complexSemi:  scale = 2.0f; return AD_PCx;
        case P_supersawSemi: scale = 2.0f; return AD_PSs;
        case P_osc1Detune:     scale = 1.0f / 12.0f; return AD_P1;
        case P_osc2Detune:     scale = 1.0f / 12.0f; return AD_P2;
        case P_osc3Detune:     scale = 1.0f / 12.0f; return AD_P3;
        case P_osc4Detune:     scale = 1.0f / 12.0f; return AD_PWt1;
        case P_wt2Detune:      scale = 1.0f / 12.0f; return AD_PWt2;
        case P_complexDetune:  scale = 1.0f / 12.0f; return AD_PCx;
        case P_supersawDetune: scale = 1.0f / 12.0f; return AD_PSs;
        // levels
        case P_osc1Gain:     return AD_L1;
        case P_osc2Gain:     return AD_L2;
        case P_osc3Gain:     return AD_L3;
        case P_subGain:      return AD_LSub;
        case P_osc4Gain:     return AD_LWt1;
        case P_wt2Gain:      return AD_LWt2;
        case P_complexGain:  return AD_LCx;
        case P_supersawGain: return AD_LSs;
        // filter: full cutoff travel is log2 (12000 / 40) = 8.23 octaves
        case P_filterCutoff: scale = 8.23f; return AD_Cut;
        case P_filterRes:    scale = 24.9f; return AD_Res;
        // FM / ring / shape, in their own units
        case P_complexFm:    scale = 1500.0f; return AD_CxFm;
        case P_fmAmount:     scale = 1500.0f; return AD_LegFm;
        case P_fmSlot1Amt:   scale = 1200.0f; return AD_Fm1;
        case P_fmSlot2Amt:   scale = 1200.0f; return AD_Fm2;
        case P_fmSlot3Amt:   scale = 1200.0f; return AD_Fm3;
        case P_fmSlot4Amt:   scale = 1200.0f; return AD_Fm4;
        case P_ringMix:      return AD_Ring;
        case P_complexShape: scale = 24.0f; return AD_CxShape;
        default: return -1;
    }
}

//==============================================================================
const NormTable& normTable()
{
    static const NormTable table = []
    {
        NormTable t;
        for (int i = 0; i < P_COUNT; ++i)
        {
            const auto& m = meta (i);
            t.lo[i] = m.min;
            t.span[i] = m.max - m.min;
            if (m.skewCentre > 0 && m.max > m.min)
            {
                t.skewed[i] = true;
                t.skew[i] = (float) (std::log (0.5) / std::log ((m.skewCentre - m.min) / (double) (m.max - m.min)));
                t.invSkew[i] = 1.0f / t.skew[i];
            }
            else { t.skew[i] = t.invSkew[i] = 1.0f; }
            t.global[i] = m.category == Category::Fx || i == P_masterVolume || i == P_warmth;
            t.envTime[i] = m.category == Category::Env && m.unit == "s";
            t.modulatable[i] = m.modulatable;
            t.audioRate[i] = m.audioRate;
        }
        return t;
    }();
    return table;
}

//==============================================================================
void RouteSet::build (const RouteStore& store)
{
    const auto& nt = normTable();
    n = 0; anyAudio = anyGlobal = anyFollow = anyEnvTime = false;
    for (int i = 0; i < kNumRoutes; ++i)
    {
        const RouteConfig c = store.get (i);
        if (! c.active() || ! nt.modulatable[c.dst]) continue;
        const bool audioSrc = kModSrcKind[c.src] == K_AUDIO;
        if (audioSrc && ! nt.audioRate[c.dst]) continue;    // an audio-rate source needs an audio-rate destination
        ResolvedRoute& r = this->r[n++];
        r.slot = i; r.src = c.src; r.dst = c.dst; r.curve = c.curve; r.via = kModSrcKind[c.via] == K_AUDIO ? MS_None : c.via;
        r.unipolar = c.unipolar; r.audio = audioSrc;
        r.toAmount = c.dst >= P_mod1Amt && c.dst < P_mod1Amt + kNumRoutes;
        r.viaDepth = c.viaDepth; r.smoothMs = c.smoothMs;
        anyAudio |= audioSrc;
        anyGlobal |= nt.global[c.dst];
        anyEnvTime |= nt.envTime[c.dst];
        anyFollow |= kModSrcKind[c.src] == K_FOLLOW || kModSrcKind[r.via] == K_FOLLOW;
    }
}

static constexpr double kStepHz = 12.0;   // "Stepped" curve: the value is held and updated 12 times a second

float shapeRouteValue (const ResolvedRoute& r, const float* srcV, RouteState& st, float dt)
{
    float x = srcV[r.src];
    if (r.unipolar && kModSrcBipolar[r.src]) x = 0.5f * (x + 1.0f);

    float ms = r.smoothMs;
    if (r.curve == MC_Smooth && ms < 60.0f) ms = 60.0f;
    if (ms > 0.0f)
    {
        float& s = st.sm[r.slot];
        if (st.first) s = x;
        else s += (x - s) * (1.0f - std::exp (-dt / (ms * 0.001f)));
        x = s;
    }
    if (r.curve == MC_Stepped)
    {
        if (st.first || st.holdPh < 0.0) st.hold[r.slot] = x;
        x = st.hold[r.slot];
    }
    x = applyCurve (r.curve, x);

    if (r.via != MS_None)
    {
        float v = srcV[r.via];
        if (kModSrcBipolar[r.via]) v = 0.5f * (v + 1.0f);
        x *= 1.0f - r.viaDepth + r.viaDepth * v;
    }
    return x;
}

void applyRoutes (const RouteSet& rs, const float* baseV, float* outV, const float* srcV, RouteState& st, float dt,
                  int filter, float* liveOffsetOut)
{
    const auto& nt = normTable();

    // Stepped curve clock: a negative phase marks "update the held values now"
    st.holdPh += dt * kStepHz;
    const bool tick = st.holdPh >= 1.0;
    if (tick) { st.holdPh -= std::floor (st.holdPh); st.holdPh = -1.0 + st.holdPh; }

    int dsts[kNumRoutes]; float offs[kNumRoutes]; int nd = 0;
    auto accumulate = [&] (int dst, float off)
    {
        for (int k = 0; k < nd; ++k) if (dsts[k] == dst) { offs[k] += off; return; }
        dsts[nd] = dst; offs[nd] = off; ++nd;
    };
    auto commit = [&] (int from)
    {
        for (int k = from; k < nd; ++k)
        {
            const int d = dsts[k];
            const float b = normFast (nt, d, baseV[d]);
            const float m = juce::jlimit (0.0f, 1.0f, b + offs[k]);
            outV[d] = plainFast (nt, d, m);
            if (liveOffsetOut != nullptr) liveOffsetOut[d] = m - b;
        }
    };
    auto wanted = [&] (int dst)
    {
        if (filter == RF_Global) return nt.global[dst];
        if (filter == RF_EnvTime) return nt.envTime[dst];
        return true;
    };

    // pass 1: routes that modulate another route's depth
    for (int i = 0; i < rs.n; ++i)
    {
        const ResolvedRoute& r = rs.r[i];
        if (r.audio || ! r.toAmount) continue;
        const float x = shapeRouteValue (r, srcV, st, dt);
        accumulate (r.dst, baseV[P_mod1Amt + r.slot] * x);
    }
    const int firstNormal = nd;
    commit (0);

    // pass 2: everything else, with depths that may just have been modulated
    for (int i = 0; i < rs.n; ++i)
    {
        const ResolvedRoute& r = rs.r[i];
        if (r.audio || r.toAmount || ! wanted (r.dst)) continue;
        const float x = shapeRouteValue (r, srcV, st, dt);
        accumulate (r.dst, outV[P_mod1Amt + r.slot] * x);
    }
    commit (firstNormal);

    if (tick) st.holdPh += 1.0;
    st.first = false;
}

} // namespace tg
