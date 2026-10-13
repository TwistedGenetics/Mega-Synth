#include "ModFx.h"

namespace tg
{

double syncRate (const ChoiceList& list, int idx, double tempo, double fallback);   // Engine.cpp

namespace
{
    inline float sine01 (double p) { return 0.5f + 0.5f * (float) std::sin (2.0 * kPi * p); }
    inline float tri01 (double p) { const double f = p - std::floor (p); return (float) (f < 0.5 ? 2.0 * f : 2.0 - 2.0 * f); }
    inline double rateOf (const float* v, int rateP, int syncP, const FxContext& ctx, float lo, float hi)
    {
        return syncRate (kListModSync, (int) std::lround (v[syncP]), ctx.tempo, clampv (v[rateP], lo, hi));
    }
    inline float softLimit (float x, float ceiling) { return ceiling * std::tanh (x / ceiling); }
    inline void advance (double& ph, double inc) { ph += inc; ph -= std::floor (ph); }
}

//==============================================================================
// Ensemble: three taps, each swept by the slow and the fast LFO (120 degrees apart)
void EnsembleChorus::prepare (double s) { sr = s; dl.prepare ((int) (0.03 * sr)); reset(); }
void EnsembleChorus::reset() { dl.reset(); phS = phF = 0.0; lpL = lpR = 0.0f; depthS = speedS = -1.0f; }

void EnsembleChorus::process (float* L, float* R, int n, const float* v, const FxContext&)
{
    const float depth = clampv (v[P_cpEnsDepth], 0.0f, 1.0f), speed = clampv (v[P_cpEnsSpeed], 0.0f, 1.0f);
    const bool first = depthS < 0.0f;
    if (first) { depthS = depth; speedS = speed; }
    const float tc = 1.0f - (float) std::exp (-2.0 * kPi * clampv (v[P_cpEnsTone], 2000.0f, 12000.0f) / sr);
    const double incS = 0.63 / sr, incF = 5.9 / sr;
    const double centre = 0.0075 * sr;
    auto tapsAt = [&] (double* d)
    {
        const double slowA = (1.0f - 0.75f * speedS) * depthS * 0.0028 * sr;   // (up to 2.8 ms of slow sweep)
        const double fastA = (0.25f + 0.75f * speedS) * depthS * 0.00045 * sr; // (and 0.45 ms of fast)
        for (int k = 0; k < 3; ++k) { const double o = k / 3.0; d[k] = centre + slowA * std::sin (2.0 * kPi * (phS + o)) + fastA * std::sin (2.0 * kPi * (phF + o)); }
    };
    if (first) tapsAt (dPrev);
    for (int i0 = 0; i0 < n; i0 += kModSub)
    {
        const int m = std::min (kModSub, n - i0);
        const float cs = smoothCoef ((double) m / sr, 0.03);
        depthS += (depth - depthS) * cs; speedS += (speed - speedS) * cs;
        advance (phS, incS * m); advance (phF, incF * m);
        double dNext[3]; tapsAt (dNext);
        for (int j = 0; j < m; ++j)
        {
            const int i = i0 + j;
            const double u = (double) (j + 1) / m;
            dl.push (0.5f * (L[i] + R[i]));
            float t[3];
            for (int k = 0; k < 3; ++k) t[k] = dl.read (dPrev[k] + (dNext[k] - dPrev[k]) * u);
            const float wl = (t[0] + 0.6f * t[1] + 0.25f * t[2]) * 0.62f, wr = (t[2] + 0.6f * t[1] + 0.25f * t[0]) * 0.62f;
            lpL += (wl - lpL) * tc; lpR += (wr - lpR) * tc;
            L[i] = lpL; R[i] = lpR;
        }
        for (int k = 0; k < 3; ++k) dPrev[k] = dNext[k];
    }
}

//==============================================================================
// BBD Chorus: one triangle sweep; the right side's sweep turns over as Stereo goes up
void BbdChorus::prepare (double s) { sr = s; for (auto& d : dl) d.prepare ((int) (0.03 * sr)); hp.set (Biquad::HP, 70.0, -3.0103, sr); reset(); }
void BbdChorus::reset() { for (auto& d : dl) d.reset(); lp.reset(); hp.reset(); ph = 0.0; depthS = stereoS = rateS = -1.0f; lastBright = ! lastBright; }

void BbdChorus::process (float* L, float* R, int n, const float* v, const FxContext& ctx)
{
    const bool bright = v[P_cpBbdBright] > 0.5f;
    if (bright != lastBright || depthS < 0.0f) { lastBright = bright; lp.set (Biquad::LP, bright ? 9500.0 : 5200.0, -3.0103, sr); }
    const float depth = clampv (v[P_cpBbdDepth], 0.0f, 1.0f), stereo = clampv (v[P_cpBbdStereo], 0.0f, 1.0f);
    const float rate = (float) rateOf (v, P_cpBbdRate, P_cpBbdSync, ctx, 0.1f, 8.0f);
    if (depthS < 0.0f) { depthS = depth; stereoS = stereo; rateS = rate; }
    const float cs = smoothCoef (1.0 / sr, 0.03);
    const double centre = 0.0068 * sr;
    for (int i = 0; i < n; ++i)
    {
        depthS += (depth - depthS) * cs; stereoS += (stereo - stereoS) * cs; rateS += (rate - rateS) * cs;
        const float t = 2.0f * tri01 (ph) - 1.0f;
        advance (ph, rateS / sr);
        const double a = depthS * 0.0034 * sr;
        dl[0].push (L[i]); dl[1].push (R[i]);
        const float wl = dl[0].read (centre + a * t), wr = dl[1].read (centre + a * t * (1.0f - 2.0f * stereoS));
        // (the BBD's own filtering, and a touch of its soft clipping)
        L[i] = (float) hp.process (lp.process (softLimit (wl * 1.1f, 1.6f), 0), 0);
        R[i] = (float) hp.process (lp.process (softLimit (wr * 1.1f, 1.6f), 1), 1);
    }
}

//==============================================================================
// Multi-Chorus: up to 8 voices, each its own sweep (a little faster each), spread across the field
void MultiChorus::prepare (double s) { sr = s; dl.prepare ((int) (0.05 * sr)); reset(); }
void MultiChorus::reset()
{
    dl.reset(); fbS = 0.0f; depthS = rateS = spreadS = -1.0f; lastWet = 0.0f;
    for (int k = 0; k < kMax; ++k) { ph[k] = (double) k / kMax; voiceG[k] = -1.0f; }
}

void MultiChorus::process (float* L, float* R, int n, const float* v, const FxContext& ctx)
{
    const int nv = kCpVoicesN[clampv ((int) std::lround (v[P_cpMulVoices]), 0, 4)];
    const float depth = clampv (v[P_cpMulDepth], 0.0f, 1.0f), spread = clampv (v[P_cpMulSpread], 0.0f, 1.0f);
    const float fb = clampv (v[P_cpMulFb], -0.9f, 0.9f);
    const float rate = (float) rateOf (v, P_cpMulRate, P_cpMulSync, ctx, 0.05f, 5.0f);
    const bool first = depthS < 0.0f;
    if (first) { depthS = depth; rateS = rate; spreadS = spread; fbS = fb; }
    for (int k = 0; k < kMax; ++k) if (voiceG[k] < 0.0f) voiceG[k] = k < nv ? 1.0f : 0.0f;
    auto delayOf = [&] (int k) { return (0.009 + 0.0013 * k) * sr + depthS * 0.004 * sr * std::sin (2.0 * kPi * ph[k]); };
    if (first) for (int k = 0; k < kMax; ++k) dPrev[k] = delayOf (k);
    for (int i0 = 0; i0 < n; i0 += kModSub)
    {
        const int m = std::min (kModSub, n - i0);
        const float cs = smoothCoef ((double) m / sr, 0.03);
        depthS += (depth - depthS) * cs; rateS += (rate - rateS) * cs; spreadS += (spread - spreadS) * cs; fbS += (fb - fbS) * cs;
        double dNext[kMax]; float g0[kMax], g1[kMax], cl[kMax], cr[kMax];
        int active = 0;
        for (int k = 0; k < kMax; ++k)
        {
            g0[k] = voiceG[k];
            voiceG[k] += ((k < nv ? 1.0f : 0.0f) - voiceG[k]) * cs;
            if (voiceG[k] < 1.0e-4f && k >= nv) voiceG[k] = 0.0f;
            g1[k] = voiceG[k];
            advance (ph[k], rateS * (1.0 + 0.11 * k) / sr * m);
            dNext[k] = delayOf (k);
            if (g0[k] > 0.0f || g1[k] > 0.0f) active = k + 1;
            const float pan = nv > 1 ? spreadS * (2.0f * (float) std::min (k, nv - 1) / (float) (nv - 1) - 1.0f) * (k % 2 == 0 ? 1.0f : -1.0f) : 0.0f;
            const float a = (pan + 1.0f) * 0.25f * (float) kPi;
            cl[k] = std::cos (a); cr[k] = std::sin (a);
        }
        float gsum = 0.0f; for (int k = 0; k < kMax; ++k) gsum += g1[k] * g1[k];
        const float norm = 1.2f / std::sqrt (std::max (1.0f, gsum)), monoN = 1.0f / std::max (1.0f, gsum);
        for (int j = 0; j < m; ++j)
        {
            const int i = i0 + j;
            const double u = (double) (j + 1) / m;
            dl.push (0.5f * (L[i] + R[i]) + softLimit (fbS * lastWet, 1.0f));
            float wl = 0.0f, wr = 0.0f, mono = 0.0f;
            for (int k = 0; k < active; ++k)
            {
                const float g = g0[k] + (g1[k] - g0[k]) * (float) u;
                if (g <= 0.0f) continue;
                const float y = dl.read (dPrev[k] + (dNext[k] - dPrev[k]) * u) * g;
                wl += y * cl[k]; wr += y * cr[k]; mono += y;
            }
            lastWet = mono * monoN;
            L[i] = wl * norm; R[i] = wr * norm;
        }
        for (int k = 0; k < kMax; ++k) dPrev[k] = dNext[k];
    }
}

//==============================================================================
// Script Phase: 4 equal stages, a triangle sweep through an exponential (JFET-like) curve
void ScriptPhase::prepare (double s) { sr = s; reset(); }
void ScriptPhase::reset() { for (auto& c : ap) for (auto& a : c) a.reset(); fb[0] = fb[1] = 0.0f; fbS = 0.0f; rateS = -1.0f; ph = 0.0; }

void ScriptPhase::process (float* L, float* R, int n, const float* v, const FxContext& ctx)
{
    const float rate = (float) rateOf (v, P_cpScrSpeed, P_cpScrSync, ctx, 0.05f, 10.0f);
    const float fbT = v[P_cpScrFb] > 0.5f ? 0.45f : 0.0f;
    auto coefNow = [&] { const float u = tri01 (ph); return Ap1::coef (160.0 * std::pow (12.0, (double) (u * u * 0.6f + u * 0.4f)), sr); };   // (about 160 Hz to 1.9 kHz)
    if (rateS < 0.0f) { rateS = rate; fbS = fbT; aPrev = coefNow(); }
    float* io[2] = { L, R };
    for (int i0 = 0; i0 < n; i0 += kModSub)
    {
        const int m = std::min (kModSub, n - i0);
        const float cs = smoothCoef ((double) m / sr, 0.03);
        rateS += (rate - rateS) * cs;
        const float fb0 = fbS; fbS += (fbT - fbS) * cs;
        advance (ph, rateS / sr * m);
        const float aNext = coefNow();
        for (int j = 0; j < m; ++j)
        {
            const float u = (float) (j + 1) / m, a = aPrev + (aNext - aPrev) * u, f = fb0 + (fbS - fb0) * u;
            for (int c = 0; c < 2; ++c)
            {
                const float x = io[c][i0 + j];
                float y = x + f * fb[c];
                for (auto& st : ap[c]) y = st.tick (y, a);
                fb[c] = y;
                io[c][i0 + j] = 0.5f * (x + y);
            }
        }
        aPrev = aNext;
    }
}

//==============================================================================
// Colour Phase: 4 stages, a sine sweep over a wider range; Colour turns the feedback up
void ColourPhase::prepare (double s) { sr = s; reset(); }
void ColourPhase::reset() { for (auto& c : ap) for (auto& a : c) a.reset(); fb[0] = fb[1] = 0.0f; fbS = 0.0f; rateS = -1.0f; ph = 0.0; }

void ColourPhase::process (float* L, float* R, int n, const float* v, const FxContext& ctx)
{
    const float rate = (float) rateOf (v, P_cpColRate, P_cpColSync, ctx, 0.05f, 10.0f);
    const float fbT = v[P_cpColColour] > 0.5f ? 0.72f : 0.12f;
    auto coefNow = [&] { return Ap1::coef (110.0 * std::pow (32.0, (double) sine01 (ph)), sr); };   // (110 Hz to 3.5 kHz)
    if (rateS < 0.0f) { rateS = rate; fbS = fbT; aPrev = coefNow(); }
    float* io[2] = { L, R };
    for (int i0 = 0; i0 < n; i0 += kModSub)
    {
        const int m = std::min (kModSub, n - i0);
        const float cs = smoothCoef ((double) m / sr, 0.03);
        rateS += (rate - rateS) * cs;
        const float fb0 = fbS; fbS += (fbT - fbS) * cs;
        advance (ph, rateS / sr * m);
        const float aNext = coefNow();
        for (int j = 0; j < m; ++j)
        {
            const float u = (float) (j + 1) / m, a = aPrev + (aNext - aPrev) * u, f = fb0 + (fbS - fb0) * u;
            for (int c = 0; c < 2; ++c)
            {
                const float x = io[c][i0 + j];
                float y = x - f * fb[c];   // (negative feedback: the Small Stone's colour peaks sit between its notches)
                for (auto& st : ap[c]) y = st.tick (y, a);
                fb[c] = y;
                io[c][i0 + j] = 0.5f * (x + y);
            }
        }
        aPrev = aNext;
    }
}

//==============================================================================
// Dual Phaser: phaser A and phaser B, 6 stages each; Parallel: A on the left, B on the right; Series: A then B
void DualPhaser::prepare (double s) { sr = s; reset(); }
void DualPhaser::reset()
{
    for (auto& p : ap) for (auto& c : p) for (auto& a : c) a.reset();
    for (auto& p : fb) p[0] = p[1] = 0.0f;
    ph[0] = 0.0; ph[1] = 0.25; modeS = -1.0f; depthS = -1.0f; rateS[0] = rateS[1] = -1.0f;
}

void DualPhaser::process (float* L, float* R, int n, const float* v, const FxContext& ctx)
{
    const int mode = clampv ((int) std::lround (v[P_cpDualMode]), 0, 1);
    const bool link = v[P_cpDualLink] > 0.5f;
    const float rA = (float) rateOf (v, P_cpDualRateA, P_cpDualSync, ctx, 0.05f, 10.0f);
    const float rB = link ? rA : clampv (v[P_cpDualRateB], 0.05f, 10.0f);
    const float depth = clampv (v[P_cpDualDepth], 0.0f, 1.0f), fbAmt = clampv (v[P_cpDualFb], 0.0f, 0.95f);
    auto coefNow = [&] (int p) { return Ap1::coef (180.0 * std::pow (2.0, (double) (depthS * 4.5f * sine01 (ph[p]))), sr); };
    // a change of routing: the phasers restart from silence and fade back in (no click)
    if (modeS < 0.0f || (int) modeS != mode)
    {
        const bool first = modeS < 0.0f;
        for (auto& p : ap) for (auto& c : p) for (auto& a : c) a.reset();
        for (auto& p : fb) p[0] = p[1] = 0.0f;
        modeS = (float) mode;
        fadeIn = first ? 1.0f : 0.0f;
    }
    if (depthS < 0.0f) { depthS = depth; rateS[0] = rA; rateS[1] = rB; aPrev[0] = coefNow (0); aPrev[1] = coefNow (1); }
    if (link) { const double want = ph[0] + 0.25; ph[1] += (want - std::floor (want) - ph[1]) * 0.05; ph[1] -= std::floor (ph[1]); }   // (B follows A, a quarter turn behind)
    for (int i0 = 0; i0 < n; i0 += kModSub)
    {
        const int m = std::min (kModSub, n - i0);
        const float cs = smoothCoef ((double) m / sr, 0.03), cf = smoothCoef (1.0 / sr, 0.01);
        depthS += (depth - depthS) * cs; rateS[0] += (rA - rateS[0]) * cs; rateS[1] += (rB - rateS[1]) * cs;
        float aNext[2];
        for (int p = 0; p < 2; ++p) { advance (ph[p], rateS[p] / sr * m); aNext[p] = coefNow (p); }
        for (int j = 0; j < m; ++j)
        {
            const int i = i0 + j;
            const float u = (float) (j + 1) / m;
            const float a[2] = { aPrev[0] + (aNext[0] - aPrev[0]) * u, aPrev[1] + (aNext[1] - aPrev[1]) * u };
            fadeIn += (1.0f - fadeIn) * cf;
            const float xl = L[i], xr = R[i];
            auto run = [&] (int p, int c, float x)
            {
                float y = x + fbAmt * fb[p][c];
                for (auto& st : ap[p][c]) y = st.tick (y, a[p]);
                fb[p][c] = y;
                return y;
            };
            float yl, yr;
            if (mode == 0) { yl = run (0, 0, xl); yr = run (1, 1, xr); }
            else { yl = run (1, 0, 0.5f * (xl + run (0, 0, xl))); yr = run (1, 1, 0.5f * (xr + run (0, 1, xr))); }
            L[i] = 0.5f * (xl * (2.0f - fadeIn) + yl * fadeIn);
            R[i] = 0.5f * (xr * (2.0f - fadeIn) + yr * fadeIn);
        }
        aPrev[0] = aNext[0]; aPrev[1] = aNext[1];
    }
}

//==============================================================================
// Barber-pole: three 8-stage chains, a third of a sweep apart; each sweeps 7 octaves and fades in at the start of its
// sweep and out at the end, so together they seem to rise (or fall) without end
void BarberPhaser::prepare (double s) { sr = s; reset(); }
void BarberPhaser::reset() { for (auto& c : ap) for (auto& ch : c) for (auto& a : ch) a.reset(); for (auto& c : fb) for (auto& f : c) f = 0.0f; ph = 0.0; rateS = -1.0f; dirS = -2.0f; }

void BarberPhaser::process (float* L, float* R, int n, const float* v, const FxContext& ctx)
{
    const float rate = (float) rateOf (v, P_cpBarSpeed, P_cpBarSync, ctx, 0.02f, 2.0f);
    const float dir = clampv ((int) std::lround (v[P_cpBarDir]), 0, 1) == 0 ? 1.0f : -1.0f;
    const float fbAmt = clampv (v[P_cpBarFb], 0.0f, 0.9f);
    auto at = [&] (float* a, float* w)
    {
        for (int c = 0; c < 2; ++c)
            for (int k = 0; k < kChains; ++k)
            {
                double u = ph + (double) k / kChains + (c == 1 ? 1.0 / (2.0 * kChains) : 0.0);
                u -= std::floor (u);
                w[c * kChains + k] = (float) (std::sin (kPi * u) * std::sin (kPi * u)) * (2.0f / kChains);
                a[c * kChains + k] = Ap1::coef (60.0 * std::pow (2.0, 7.0 * u), sr);
            }
    };
    if (rateS < 0.0f) { rateS = rate; dirS = dir; at (&aPrev[0][0], &wPrev[0][0]); }
    float* io[2] = { L, R };
    for (int i0 = 0; i0 < n; i0 += kModSub)
    {
        const int m = std::min (kModSub, n - i0);
        const float cs = smoothCoef ((double) m / sr, 0.05);
        rateS += (rate - rateS) * cs; dirS += (dir - dirS) * cs;   // (a change of direction slows, stops and turns round)
        // a chain that wraps from the top of its sweep to the bottom does so at zero level: jump its coefficient, don't ramp it
        const double ph0 = ph;
        advance (ph, dirS * rateS / sr * m);
        float aNext[2][kChains], wNext[2][kChains];
        at (&aNext[0][0], &wNext[0][0]);
        (void) ph0;
        for (int c = 0; c < 2; ++c)
            for (int k = 0; k < kChains; ++k)
                if (std::abs (aNext[c][k] - aPrev[c][k]) > 0.5f) aPrev[c][k] = aNext[c][k];
        for (int j = 0; j < m; ++j)
        {
            const float u = (float) (j + 1) / m;
            for (int c = 0; c < 2; ++c)
            {
                const float x = io[c][i0 + j];
                float y = 0.0f;
                for (int k = 0; k < kChains; ++k)
                {
                    const float a = aPrev[c][k] + (aNext[c][k] - aPrev[c][k]) * u, w = wPrev[c][k] + (wNext[c][k] - wPrev[c][k]) * u;
                    float z = x + fbAmt * fb[c][k];
                    for (auto& st : ap[c][k]) z = st.tick (z, a);
                    fb[c][k] = z;
                    y += w * z;
                }
                io[c][i0 + j] = 0.5f * (x + y);
            }
        }
        for (int c = 0; c < 2; ++c) for (int k = 0; k < kChains; ++k) { aPrev[c][k] = aNext[c][k]; wPrev[c][k] = wNext[c][k]; }
    }
}

//==============================================================================
// Vibe: a lamp lit by the LFO (quick to brighten, slow to fade, like a bulb and photocell) sweeps 4 unequal stages
void VibeFx::prepare (double s) { sr = s; reset(); }
void VibeFx::reset() { for (auto& c : ap) for (auto& a : c) a.reset(); lamp = 0.0f; ph = 0.0; intS = modeS = rateS = -1.0f; }

void VibeFx::process (float* L, float* R, int n, const float* v, const FxContext& ctx)
{
    static const double base[4] = { 95.0, 420.0, 1500.0, 4200.0 };
    const float rate = (float) rateOf (v, P_cpVibeSpeed, P_cpVibeSync, ctx, 0.1f, 10.0f);
    const float inten = clampv (v[P_cpVibeInt], 0.0f, 1.0f);
    const float mode = clampv ((int) std::lround (v[P_cpVibeMode]), 0, 1) == 1 ? 1.0f : 0.0f;
    auto coefs = [&] (float* a)
    {
        const double cell = std::pow (std::max (0.0f, lamp), 0.7f);   // (the photocell's curve)
        for (int k = 0; k < 4; ++k) a[k] = Ap1::coef (base[k] * std::pow (2.0, (double) intS * 3.2 * (cell - 0.35)), sr);
    };
    if (intS < 0.0f) { intS = inten; modeS = mode; rateS = rate; coefs (aPrev); }
    float* io[2] = { L, R };
    for (int i0 = 0; i0 < n; i0 += kModSub)
    {
        const int m = std::min (kModSub, n - i0);
        const float cs = smoothCoef ((double) m / sr, 0.03);
        const float up = smoothCoef ((double) m / sr, 0.012), down = smoothCoef ((double) m / sr, 0.05);
        intS += (inten - intS) * cs; rateS += (rate - rateS) * cs;
        const float m0 = modeS; modeS += (mode - modeS) * cs;
        advance (ph, rateS / sr * m);
        const float drive = std::pow (sine01 (ph), 1.6f);
        lamp += (drive - lamp) * (drive > lamp ? up : down);
        float aNext[4]; coefs (aNext);
        for (int j = 0; j < m; ++j)
        {
            const float u = (float) (j + 1) / m, md = m0 + (modeS - m0) * u;
            float a[4]; for (int k = 0; k < 4; ++k) a[k] = aPrev[k] + (aNext[k] - aPrev[k]) * u;
            for (int c = 0; c < 2; ++c)
            {
                const float x = io[c][i0 + j];
                float y = x;
                for (int k = 0; k < 4; ++k) y = ap[c][k].tick (y, a[k]);
                io[c][i0 + j] = x * 0.5f * (1.0f - md) + y * (0.5f + 0.5f * md);   // (Chorus: dry + shifted; Vibrato: shifted only)
            }
        }
        for (int k = 0; k < 4; ++k) aPrev[k] = aNext[k];
    }
}

} // namespace tg
