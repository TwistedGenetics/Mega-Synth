#pragma once
// The jungle / drum & bass bank's Output trims (dB), measured so each preset's audition phrase peaks at
// about -12 dBFS (MEGASYNTH_PRESETCAL prints them). Index = position in dnbBank().
namespace tg
{
inline float dnbTrim (int i)
{
    static const float t[100] = {
        1.9f, 2.8f, 1.8f, -2.8f, -1.0f, -0.8f, 2.9f, -2.8f, -1.2f, -0.6f,
        -0.2f, 1.6f, -2.6f, 1.4f, -1.8f, -2.0f, -6.8f, -1.8f, -6.9f, 0.6f,
        1.5f, -3.0f, -2.9f, -3.6f, -2.0f, 8.4f, -3.4f, -4.1f, -2.5f, 0.8f,
        -2.0f, -5.5f, -4.7f, -0.3f, 0.3f, -2.3f, -5.4f, -5.2f, 2.0f, 1.0f,
        0.6f, 0.9f, 3.6f, 1.8f, 0.4f, -2.0f, 6.9f, -5.0f, -6.8f, -8.5f,
        -2.4f, -8.9f, -8.1f, -4.5f, -1.6f, -6.9f, 1.7f, -4.3f, -6.2f, -7.9f,
        -0.5f, -7.3f, -5.4f, 0.5f, -9.3f, -1.6f, -6.8f, -1.5f, -1.9f, -0.2f,
        2.9f, 4.7f, -6.3f, -0.8f, -5.2f, -3.6f, -3.6f, -3.5f, -3.6f, -3.6f,
        -3.6f, -3.5f, -3.0f, -3.6f, -3.2f, -1.4f, 3.7f, 4.1f, 1.5f, -4.2f,
        -0.6f, 5.0f, 1.0f, -6.0f, 4.6f, -6.6f, -1.7f, -1.0f, -2.0f, -6.1f
    };
    return i >= 0 && i < 100 ? t[i] : 0.0f;
}
} // namespace tg
