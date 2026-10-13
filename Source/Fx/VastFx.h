#pragma once
// v0.1.74: Vastness - huge reverbs, cascading echoes and swelling pads from one network of long, modulated delays fed
// back into each other (an original design, in the spirit of the "huge delay / reverb" effects). Eight lines; Warp blends
// how they feed each other, from a ring of echoes passed along (cascades) to all into all (a dense wash). Modes change the
// lines' lengths and the character. Adds its sound to what it hears; the slot's Return sets how much.
#include <JuceHeader.h>
#include "../DSP.h"
#include "../Params.h"
#include "RackFx.h"

namespace tg
{

class VastReverb
{
public:
    void prepare (double sr);
    void reset();
    void process (float* L, float* R, int n, const float* v, const FxContext&, const float* duck);
    static constexpr int kLines = 8;
private:
    double sr = 48000.0;
    DelayLine line[kLines];
    DelayLine diff[4];               // input diffusion (all-passes)
    double diffLen[4] {};
    float lp[kLines] {}, hp[kLines] {};
    double lenS[kLines] {};          // each line's length now (samples), smoothed
    double lfoPh[kLines] {};
    double modPrev[kLines] {};
    float freezeS = 0.0f, warpS = -1.0f, fbS = -1.0f, densS = -1.0f, widthS = -1.0f;
    float bloomEnv = 0.0f, bloomGain = 0.0f, inEnv = 0.0f;
    int curMode = -1;
    // Shimmer Swell: an octave up in the loop (two crossfaded read heads over a short buffer)
    DelayLine shim;
    double shimPh = 0.0;
    float wowPh = 0.0f;
};

} // namespace tg
