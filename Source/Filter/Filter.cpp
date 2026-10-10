#include "Filter.h"

namespace tg
{

//==============================================================================
// FilterChain: the 17 filter "flavours" of the browser synth, built from the same
// Web Audio biquad stages, waveshapers and feedback paths.
enum FilterMode { FM_LP, FM_SALLENKEY, FM_LADDER, FM_STEINER, FM_SVF, FM_OTA, FM_POLIVOKS, FM_SWCAP, FM_ACID,
                  FM_MS20, FM_OBERHEIM, FM_SEM, FM_TB303, FM_MOOGFAT, FM_ARPODYSSEY, FM_CS15, FM_SH2 };

void FilterChain::configure (int newMode)
{
    mode = newMode;
    fbKind = FbNone; preK = 0; postK = 0; fbGain = 0; ladder = false; limitOut = false; tb303 = false;
    auto set = [this] (std::initializer_list<Biquad::Type> t)
    {
        numStages = 0;
        for (auto x : t) types[numStages++] = x;
    };
    using B = Biquad;
    switch (mode)
    {
        case FM_LP:          set ({ B::LP }); break;
        case FM_SALLENKEY:   set ({ B::LP, B::LP }); break;
        case FM_LADDER:      set ({}); ladder = true; break;
        case FM_STEINER:     set ({ B::HP, B::BP, B::LP }); break;
        case FM_SVF:         set ({ B::BP, B::LP }); break;
        case FM_OTA:         set ({ B::LP, B::LP }); preK = 4.0f; break;
        case FM_POLIVOKS:    set ({ B::HP, B::LP }); postK = 7.0f; break;
        case FM_SWCAP:       set ({ B::LP, B::NOTCH, B::LP }); break;
        case FM_MS20:        set ({ B::HP, B::LP }); break;
        case FM_OBERHEIM:    set ({ B::HP, B::LP }); break;
        case FM_SEM:         set ({ B::BP, B::LP }); break;
        case FM_TB303:       set ({}); ladder = true; tb303 = true; limitOut = true; break;
        case FM_MOOGFAT:     set ({}); ladder = true; break;
        case FM_ARPODYSSEY:  set ({ B::HP, B::LP }); break;
        case FM_CS15:        set ({ B::HP, B::BP, B::LP }); break;
        case FM_SH2:         set ({ B::LP, B::LP }); break;
        case FM_ACID:
        default:             set ({ B::LP, B::LP }); preK = 6.0f; limitOut = true; break;
    }
    reset();
}

void FilterChain::reset()
{
    for (auto& s : st) s.reset();
    fbState[0] = fbState[1] = 0;
    for (auto& ch : ls) for (auto& v : ch) v = 0.0f;
    for (auto& b : par) b.reset();
    dcX[0] = dcX[1] = dcY[0] = dcY[1] = 0.0f;
    tbX[0] = tbX[1] = tbY[0] = tbY[1] = 0.0f;
}

void FilterChain::copyChannel (int from, int to)
{
    for (auto& s : st) s.copyState (from, to);
    fbState[to] = fbState[from];
    for (int k = 0; k < 4; ++k) ls[to][k] = ls[from][k];
    for (auto& b : par) b.copyState (from, to);
    dcX[to] = dcX[from]; dcY[to] = dcY[from];
    tbX[to] = tbX[from]; tbY[to] = tbY[from];
}

void FilterChain::setLadder (float c, float spread, float k, double sr)
{
    for (int i = 0; i < 4; ++i)
    {
        const double fc = clampv ((double) std::max (20.0f, c * (1.0f - i * spread)), 10.0, sr * 0.45);
        const double g = std::tan (kPi * fc / sr);
        lg[i] = (float) (g / (1.0 + g));
    }
    lk = k;
}

void FilterChain::update (float c, float res, double sr)
{
    auto mx = [] (float a, float b) { return std::max (a, b); };
    auto mn = [] (float a, float b) { return std::min (a, b); };
    dcR = (float) (1.0 - 2.0 * kPi * 8.0 / sr);
    auto S = [&] (int k, float f, float q)
    {
        if (types[k] == Biquad::HP) f = std::max (20.0f, f * (1.0f - 0.85f * bassKeep));
        st[k].set (types[k], f, q, sr);
        if (types[k] == Biquad::BP) par[k].set (Biquad::LP, f, -3.01, sr);   // Butterworth low-pass at the same frequency
    };
    switch (mode)
    {
        case FM_LP:
            S (0, c, res); break;
        case FM_SALLENKEY:
            S (0, c, mx (0.2f, res * 0.18f));
            S (1, mx (20, c * 0.92f), mx (0.2f, res * 0.18f)); break;
        case FM_LADDER:
            setLadder (c, 0.0f, 4.0f * mn (0.98f, res / 30.0f), sr); break;
        case FM_STEINER:
            S (0, mx (20, c * 0.55f), mx (0.2f, res * 0.12f));
            S (1, c, mx (0.2f, res * 0.35f));
            S (2, mn (18000, c * 1.15f), mx (0.2f, res * 0.18f)); break;
        case FM_SVF:
            S (0, c, mx (0.2f, res * 0.28f));
            S (1, mn (18000, c * 1.08f), mx (0.2f, res * 0.14f)); break;
        case FM_OTA:
            S (0, c, mx (0.2f, res * 0.16f));
            S (1, mx (20, c * 0.88f), mx (0.2f, res * 0.22f)); break;
        case FM_POLIVOKS:
            S (0, mx (20, c * 0.65f), mx (0.2f, res * 0.2f));
            S (1, c, mx (0.2f, res * 0.55f)); break;
        case FM_SWCAP:
            S (0, c, mx (0.2f, res * 0.12f));
            S (1, mx (20, c * 1.35f), mx (0.2f, res * 0.35f));
            S (2, mx (20, c * 0.82f), mx (0.2f, res * 0.12f)); break;
        case FM_MS20:
            S (0, mx (20, c * 0.62f), mx (0.25f, res * 0.18f));
            S (1, c, mx (0.3f, res * 0.72f)); break;
        case FM_OBERHEIM:
            S (0, mx (20, c * 0.28f), mx (0.2f, res * 0.08f));
            S (1, mn (18000, c * 1.08f), mx (0.2f, res * 0.2f)); break;
        case FM_SEM:
            S (0, c, mx (0.2f, res * 0.18f));
            S (1, mn (18000, c * 1.12f), mx (0.2f, res * 0.14f)); break;
        case FM_TB303:
            tbA = (float) (1.0 / (1.0 + 2.0 * kPi * 150.0 / sr));
            setLadder (c, 0.0f, 4.0f * mn (0.98f, res / 24.0f), sr); break;
        case FM_MOOGFAT:
            setLadder (c, 0.03f, 4.0f * mn (0.985f, res / 22.0f), sr); break;
        case FM_ARPODYSSEY:
            S (0, mx (20, c * 0.42f), mx (0.2f, res * 0.12f));
            S (1, mn (18000, c * 1.04f), mx (0.2f, res * 0.24f)); break;
        case FM_CS15:
            S (0, mx (20, c * 0.5f), mx (0.2f, res * 0.1f));
            S (1, c, mx (0.2f, res * 0.42f));
            S (2, mn (18000, c * 1.1f), mx (0.2f, res * 0.16f)); break;
        case FM_SH2:
            S (0, c, mx (0.2f, res * 0.16f));
            S (1, mx (20, c * 0.9f), mx (0.2f, res * 0.36f)); break;
        case FM_ACID:
        default:
        {
            // resonance fades below ~500 Hz so a closed filter doesn't boom at its cutoff
            const float rs = clampv ((c - 60.0f) / 440.0f, 0.2f, 1.0f);
            S (0, c, res * 0.8f * rs);
            S (1, mx (20, c * 0.95f), res * 1.2f * rs);
            fbGain = mn (0.92f, res / 28.0f); break;
        }
    }
}


//==============================================================================
const ModelVoice& modelVoice (int model)
{
    //                                   preK postK resScale satT  limit
    static const ModelVoice table[17] = { { 0, 0, 1.00f, 6.0f, false },    // Standard LP
                                          { 0, 0, 0.90f, 5.0f, false },    // Sallen-Key
                                          { 0, 0, 1.00f, 5.0f, false },    // Ladder
                                          { 0, 0, 1.10f, 3.0f, false },    // Steiner-Parker
                                          { 0, 0, 1.00f, 6.0f, false },    // State Variable
                                          { 4, 0, 0.95f, 4.0f, false },    // Discrete OTA
                                          { 0, 7, 1.15f, 2.4f, false },    // Polivoks
                                          { 0, 0, 0.90f, 4.0f, false },    // Switched Capacitor
                                          { 6, 0, 1.10f, 3.0f, true  },    // Acid Filter Approx
                                          { 0, 0, 1.15f, 2.4f, false },    // MS-20
                                          { 0, 0, 0.90f, 6.0f, false },    // Oberheim
                                          { 0, 0, 0.85f, 6.0f, false },    // SEM
                                          { 0, 0, 1.00f, 3.6f, true  },    // TB-303
                                          { 0, 0, 1.00f, 5.0f, false },    // Minimoog - Fat Cat
                                          { 0, 0, 1.00f, 4.0f, false },    // Arp Odyssey
                                          { 0, 0, 0.95f, 5.0f, false },    // CS-15
                                          { 0, 0, 0.95f, 5.0f, false } };  // SH-2
    return table[clampv (model, 0, 16)];
}

//==============================================================================
void MultiCore::configure (int filterType, int effectiveSlope, int model)
{
    type = clampv (filterType, 0, (int) FT_TYPE_COUNT - 1);
    slope = clampv (effectiveSlope, 0, 3);
    mv = modelVoice (model);
    reset();
}

void MultiCore::reset()
{
    a.reset(); b.reset(); p.reset();
    for (auto& ch : ls) for (auto& v : ch) v = 0.0f;
    lfb[0] = lfb[1] = 0.0f;
    dcX[0] = dcX[1] = dcY[0] = dcY[1] = 0.0f;
}

void MultiCore::copyChannel (int from, int to)
{
    a.copy (from, to); b.copy (from, to); p.copy (from, to);
    for (int k = 0; k < 4; ++k) ls[to][k] = ls[from][k];
    lfb[to] = lfb[from];
    dcX[to] = dcX[from]; dcY[to] = dcY[from];
}

void MultiCore::update (float c, float res, double sr)
{
    dcR = (float) (1.0 - 2.0 * kPi * 8.0 / sr);
    const float g = (float) std::tan (kPi * (double) c / sr);
    // Resonance knob (0.1 .. 25) -> 0..1, then the model's strength. Q runs 0.55 .. ~50 per
    // octave-equal step of the knob, so the useful range is spread evenly.
    r = clampv ((res - 0.1f) / 24.9f * mv.resScale, 0.0f, 1.0f);
    const float q = 0.55f * std::pow (90.0f, r);
    const float k = 1.0f / q;
    bump = 0.0f;
    switch (type)
    {
        case FT_LP:
        case FT_HP:
            if (slope == 3 && type == FT_LP)
            {
                lg = g / (1.0f + g);
                lk = 4.2f * std::pow (r, 0.75f);   // reaches self-oscillation near the top of the knob
            }
            else if (slope == 3)
            {
                a.set (g, 1.0f / 0.5412f);                       // Butterworth pair, resonance on the second stage
                b.set (g, std::min (k, 1.0f / 1.3066f));
            }
            else
            {
                a.set (g, k);
                p.set (g);
                if (slope == 0) bump = 2.0f * r;
            }
            break;
        case FT_BP:
            a.set (g, slope == 3 ? std::min (k, 1.0f / 0.7071f) : k);
            b.set (g, k);
            bpGainA = slope == 3 ? a.k : std::sqrt (a.k);   // 24 dB: unity first stage, then the resonant one
            bpGainB = std::sqrt (b.k);
            break;
        case FT_NOTCH:
            a.set (g, k); b.set (g, k);
            break;
        case FT_PEAK:
        {
            const float db = 2.0f + 16.0f * r;
            const float A = std::pow (10.0f, db / 40.0f);
            const float qb = 0.7f + 4.0f * r;
            a.set (g, 1.0f / (qb * A));
            peakGain = A * A - 1.0f;
            break;
        }
        case FT_AP:
        default:
            a.set (g, k); b.set (g, k); p.set (g);
            break;
    }
}

float MultiCore::process (float x, int ch, float drive)
{
    float y = 0.0f, lp, bp;
    const float sat = mv.satT;
    if (type == FT_LP && slope == 3)
    {
        // Zero-delay feedback: solve the loop for this sample's output (linear estimate), then put
        // the loop input through the drive stage, so tuning and resonance stay right up to 20 kHz.
        float* s = ls[ch];
        const float G = lg, H = 1.0f - lg;
        const float G2 = G * G, G3 = G2 * G, G4 = G2 * G2;
        const float S = G3 * H * s[0] + G2 * H * s[1] + G * H * s[2] + H * s[3];
        const float y4 = (G4 * x + S) / (1.0f + lk * G4);
        float v = filterInputShape (x - lk * y4, drive, warm);
        if (mv.preK > 0.0f) v = modelShape (v, mv.preK, mv.character);
        for (int k = 0; k < 4; ++k)
        {
            const float u = (v - s[k]) * lg;
            v = u + s[k];
            s[k] = v + u;
        }
        lfb[ch] = v;
        y = v * (1.0f + lk * 0.35f);   // make up some of the bass lost to resonance
    }
    else
    {
        float v = filterInputShape (x, drive, warm);
        if (mv.preK > 0.0f) v = modelShape (v, mv.preK, mv.character);
        switch (type)
        {
            case FT_LP:
            case FT_HP:
            {
                const bool hp = type == FT_HP;
                if (slope == 0)
                {
                    const float l1 = p.lp (v, ch);
                    y = hp ? v - l1 : l1;
                    if (bump > 0.0f) { a.tick (v, ch, lp, bp, sat); y += bump * a.k * bp; }
                }
                else if (slope == 3)   // high pass, 24 dB
                {
                    a.tick (v, ch, lp, bp, 0.0f);
                    const float h1 = v - a.k * bp - lp;
                    b.tick (h1, ch, lp, bp, sat);
                    y = h1 - b.k * bp - lp;
                }
                else
                {
                    a.tick (v, ch, lp, bp, sat);
                    y = hp ? v - a.k * bp - lp : lp;
                    if (slope == 2) { const float l1 = p.lp (y, ch); y = hp ? y - l1 : l1; }
                }
                break;
            }
            case FT_BP:
                a.tick (v, ch, lp, bp, sat);
                y = bp * bpGainA;
                if (slope == 3) { b.tick (y, ch, lp, bp, sat); y = bp * bpGainB; }
                break;
            case FT_NOTCH:
                a.tick (v, ch, lp, bp, sat);
                y = v - a.k * bp;
                if (slope == 3) { const float y1 = y; b.tick (y1, ch, lp, bp, sat); y = y1 - b.k * bp; }
                break;
            case FT_PEAK:
                a.tick (v, ch, lp, bp, sat);
                y = v + a.k * peakGain * bp;
                break;
            case FT_AP:
            default:
                if (slope == 0) { const float l1 = p.lp (v, ch); y = 2.0f * l1 - v; }
                else
                {
                    a.tick (v, ch, lp, bp, 0.0f);
                    y = v - 2.0f * a.k * bp;
                    if (slope == 2) { const float l1 = p.lp (y, ch); y = 2.0f * l1 - y; }
                    else if (slope == 3) { const float y1 = y; b.tick (y1, ch, lp, bp, 0.0f); y = y1 - 2.0f * b.k * bp; }
                }
                break;
        }
    }
    if (! (std::abs (y) < 64.0f)) { reset(); y = 0.0f; }
    if (mv.postK > 0.0f) y = modelShape (y, mv.postK, mv.character);
    y = softKnee (y, mv.limitOut ? 1.5f : 4.0f);   // safety only: normal levels pass untouched
    const float d = y - dcX[ch] + dcR * dcY[ch];
    dcX[ch] = y; dcY[ch] = d;
    return d;
}

//==============================================================================
void FilterUnit::setup (Core& c, int model, int type, int slope)
{
    c.model = model; c.type = type; c.slope = slope;
    const int eff = effectiveFilterSlope (type, slope);
    c.classic = type == FT_LP && eff == nativeFilterSlope (model);
    if (c.classic) c.chain.configure (model);
    else c.multi.configure (type, eff, model);
    c.chain.warm = warm; c.chain.bassKeep = keep;
    c.multi.warm = warm;
    c.chain.character = c.multi.mv.character = driveCharacter (drive);
    if (preview) { c.chain.preK = c.chain.postK = 0.0f; c.multi.mv.preK = c.multi.mv.postK = 0.0f; }
}

void FilterUnit::configure (int model, int type, int slope, bool immediate)
{
    if (immediate)
    {
        setup (cur, model, type, slope);
        fade = 0;
        return;
    }
    if (model == cur.model && type == cur.type && slope == cur.slope) return;
    // Slopes the type doesn't use (Peak) or that map to the same engine change nothing audible
    const int eff = effectiveFilterSlope (type, slope);
    if (cur.model >= 0 && model == cur.model && type == cur.type && eff == effectiveFilterSlope (cur.type, cur.slope))
    {
        cur.slope = slope;
        return;
    }
    old = cur;
    setup (cur, model, type, slope);
    fade = kFadeLen;
    primePending = true;   // done in update(), once the new filter has its coefficients
}

void FilterUnit::update (float c, float res, double sr)
{
    c = clampv (c, 20.0f, (float) std::min (20000.0, sr * 0.45));
    if (cur.classic) cur.chain.update (c, res, sr); else cur.multi.update (c, res, sr);
    if (primePending)
    {
        primePending = false;
        for (int i = 0; i < kHist; ++i)
        {
            const int k = (hpos + i) & (kHist - 1);
            run (cur, hist[0][k], 0);
            if (lastStereo) run (cur, hist[1][k], 1);
        }
    }
    if (fade > 0) { if (old.classic) old.chain.update (c, res, sr); else old.multi.update (c, res, sr); }
}

void FilterUnit::setAnalog (float warmth, float bassKeep)
{
    warm = warmth; keep = bassKeep;
    cur.chain.warm = old.chain.warm = warm;
    cur.chain.bassKeep = old.chain.bassKeep = keep;
    cur.multi.warm = old.multi.warm = warm;
}

void FilterUnit::reset()
{
    for (auto& h : hist) std::fill (std::begin (h), std::end (h), 0.0f);
    primePending = false;
    cur.chain.reset(); cur.multi.reset();
    old.chain.reset(); old.multi.reset();
    fade = 0;
}

void FilterUnit::copyChannel (int from, int to)
{
    for (Core* c : { &cur, &old })
    {
        c->chain.copyChannel (from, to);
        c->multi.copyChannel (from, to);
    }
}

} // namespace tg
