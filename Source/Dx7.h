#pragma once
// v0.1.74: reading DX7 voice files (sysex) for the Core slots. A 32-voice bank (4104 bytes, the packed VMEM format)
// or a single voice (163 bytes, VCED). No DX7 data is included with the plug-in: you load your own files.
#include <JuceHeader.h>
#include <array>
#include <vector>

namespace tg
{
namespace dx7
{
    struct Op
    {
        int rate[4] {}, level[4] {};
        int bp = 39, ld = 0, rd = 0, lc = 0, rc = 0, rateScale = 0, ams = 0, kvs = 0, outLevel = 0;
        int mode = 0, coarse = 1, fine = 0, detune = 7;   // mode 0 ratio, 1 fixed
    };
    struct Voice
    {
        Op op[6];   // op[0] is operator 1 (the files store 6 first)
        int pitchRate[4] {}, pitchLevel[4] { 50, 50, 50, 50 };
        int algorithm = 0, feedback = 0, oscSync = 1;
        int lfoSpeed = 35, lfoDelay = 0, lfoPmd = 0, lfoAmd = 0, lfoSync = 1, lfoWave = 0, pms = 3, transpose = 24;
        juce::String name;
    };

    // The 32 algorithms: modulations as "from to" pairs (operator numbers), the carriers, and which operator has the
    // feedback. (Algorithms 4 and 6 loop feedback through two operators on the original; here it's on the top one.)
    struct Algo { const char* edges; const char* carriers; int fbOp; };
    inline const Algo kAlgos[32] = {
        { "21 65 54 43", "13", 6 },          // 1
        { "21 65 54 43", "13", 2 },          // 2
        { "32 21 65 54", "14", 6 },          // 3
        { "32 21 65 54", "14", 6 },          // 4 (feedback 4>6 on the original)
        { "21 43 65", "135", 6 },            // 5
        { "21 43 65", "135", 6 },            // 6 (feedback 5>6 on the original)
        { "21 43 53 65", "13", 6 },          // 7
        { "21 43 53 65", "13", 4 },          // 8
        { "21 43 53 65", "13", 2 },          // 9
        { "32 21 54 64", "14", 3 },          // 10
        { "32 21 54 64", "14", 6 },          // 11
        { "21 43 53 63", "13", 2 },          // 12
        { "21 43 53 63", "13", 6 },          // 13
        { "21 43 54 64", "13", 6 },          // 14
        { "21 43 54 64", "13", 2 },          // 15
        { "21 31 43 51 65", "1", 6 },        // 16
        { "21 31 43 51 65", "1", 2 },        // 17
        { "21 31 41 54 65", "1", 3 },        // 18
        { "32 21 64 65", "145", 6 },         // 19
        { "31 32 54 64", "124", 3 },         // 20
        { "31 32 64 65", "1245", 3 },        // 21
        { "21 63 64 65", "1345", 6 },        // 22
        { "32 64 65", "1245", 6 },           // 23
        { "63 64 65", "12345", 6 },          // 24
        { "64 65", "12345", 6 },             // 25
        { "32 54 64", "124", 6 },            // 26
        { "32 54 64", "124", 3 },            // 27
        { "21 43 54", "136", 5 },            // 28
        { "43 65", "1235", 6 },              // 29
        { "43 54", "1236", 5 },              // 30
        { "65", "12345", 6 },                // 31
        { "", "123456", 6 },                 // 32
    };

    inline juce::String cleanName (const uint8_t* p)
    {
        juce::String n;
        for (int i = 0; i < 10; ++i) { const char c = (char) (p[i] & 0x7f); n << juce::String::charToString ((juce::juce_wchar) (c >= 32 && c < 127 ? c : ' ')); }
        return n.trim();
    }
    inline int c99 (int v) { return juce::jlimit (0, 99, v); }

    // one voice from 128 packed bytes (a bank's voice)
    inline Voice fromPacked (const uint8_t* b)
    {
        Voice v;
        for (int f = 0; f < 6; ++f)
        {
            const uint8_t* o = b + f * 17;
            Op& op = v.op[5 - f];   // (stored 6 first)
            for (int i = 0; i < 4; ++i) { op.rate[i] = c99 (o[i]); op.level[i] = c99 (o[4 + i]); }
            op.bp = c99 (o[8]); op.ld = c99 (o[9]); op.rd = c99 (o[10]);
            op.lc = o[11] & 3; op.rc = (o[11] >> 2) & 3;
            op.rateScale = o[12] & 7; op.detune = juce::jlimit (0, 14, (o[12] >> 3) & 15);
            op.ams = o[13] & 3; op.kvs = (o[13] >> 2) & 7;
            op.outLevel = c99 (o[14]);
            op.mode = o[15] & 1; op.coarse = (o[15] >> 1) & 31;
            op.fine = c99 (o[16]);
        }
        for (int i = 0; i < 4; ++i) { v.pitchRate[i] = c99 (b[102 + i]); v.pitchLevel[i] = c99 (b[106 + i]); }
        v.algorithm = b[110] & 31;
        v.feedback = b[111] & 7; v.oscSync = (b[111] >> 3) & 1;
        v.lfoSpeed = c99 (b[112]); v.lfoDelay = c99 (b[113]); v.lfoPmd = c99 (b[114]); v.lfoAmd = c99 (b[115]);
        v.lfoSync = b[116] & 1; v.lfoWave = (b[116] >> 1) & 7; v.pms = (b[116] >> 4) & 7;
        v.transpose = juce::jlimit (0, 48, (int) b[117]);
        v.name = cleanName (b + 118);
        return v;
    }
    // one voice from 155 unpacked bytes (a single-voice dump)
    inline Voice fromUnpacked (const uint8_t* b)
    {
        Voice v;
        for (int f = 0; f < 6; ++f)
        {
            const uint8_t* o = b + f * 21;
            Op& op = v.op[5 - f];
            for (int i = 0; i < 4; ++i) { op.rate[i] = c99 (o[i]); op.level[i] = c99 (o[4 + i]); }
            op.bp = c99 (o[8]); op.ld = c99 (o[9]); op.rd = c99 (o[10]); op.lc = o[11] & 3; op.rc = o[12] & 3;
            op.rateScale = o[13] & 7; op.ams = o[14] & 3; op.kvs = o[15] & 7; op.outLevel = c99 (o[16]);
            op.mode = o[17] & 1; op.coarse = o[18] & 31; op.fine = c99 (o[19]); op.detune = juce::jlimit (0, 14, (int) o[20]);
        }
        for (int i = 0; i < 4; ++i) { v.pitchRate[i] = c99 (b[126 + i]); v.pitchLevel[i] = c99 (b[130 + i]); }
        v.algorithm = b[134] & 31; v.feedback = b[135] & 7; v.oscSync = b[136] & 1;
        v.lfoSpeed = c99 (b[137]); v.lfoDelay = c99 (b[138]); v.lfoPmd = c99 (b[139]); v.lfoAmd = c99 (b[140]);
        v.lfoSync = b[141] & 1; v.lfoWave = b[142] & 7; v.pms = b[143] & 7; v.transpose = juce::jlimit (0, 48, (int) b[144]);
        v.name = cleanName (b + 145);
        return v;
    }

    // a sysex file's voices; error says why when there are none
    inline std::vector<Voice> parse (const juce::MemoryBlock& mb, juce::String& error)
    {
        std::vector<Voice> out;
        const auto* d = static_cast<const uint8_t*> (mb.getData());
        const size_t n = mb.getSize();
        if (n < 8 || d[0] != 0xF0 || d[1] != 0x43) { error = "Not a DX7 sysex file (it doesn't start F0 43)"; return out; }
        const int format = d[3];
        auto checksum = [] (const uint8_t* p, size_t len) { int s = 0; for (size_t i = 0; i < len; ++i) s += p[i]; return (uint8_t) ((128 - (s & 127)) & 127); };
        if (format == 9)
        {
            if (n < 4104) { error = "A 32-voice bank should be 4104 bytes; this is " + juce::String ((int) n); return out; }
            const uint8_t* data = d + 6;
            if (checksum (data, 4096) != (d[4102] & 127)) error = "(the bank's checksum doesn't match: loaded anyway)";
            for (int i = 0; i < 32; ++i) out.push_back (fromPacked (data + i * 128));
            return out;
        }
        if (format == 0)
        {
            if (n < 163) { error = "A single voice should be 163 bytes; this is " + juce::String ((int) n); return out; }
            if (checksum (d + 6, 155) != (d[161] & 127)) error = "(the voice's checksum doesn't match: loaded anyway)";
            out.push_back (fromUnpacked (d + 6));
            return out;
        }
        error = "A DX7 file, but not a voice or a 32-voice bank (format " + juce::String (format) + ")";
        return out;
    }

    // ---- converting to the Core's settings (approximations, listed by convert)
    // an envelope rate (0-99) as the time to sweep the whole range, in seconds (about 38 s at 0 to 5 ms at 99)
    inline double rateSeconds (int r) { return 38.0 * std::pow (2.0, -(double) juce::jlimit (0, 99, r) / 7.7); }
    // an output or envelope level (0-99) as a gain (0.75 dB a step; 0 is silence)
    inline double levelGain (int l) { return l <= 0 ? 0.0 : std::pow (10.0, (double) (juce::jlimit (0, 99, l) - 99) * 0.75 / 20.0); }
    inline double ratioOf (const Op& o) { const double c = o.coarse == 0 ? 0.5 : (double) o.coarse; return c * (1.0 + o.fine / 100.0); }
    inline double fixedHzOf (const Op& o) { return std::pow (10.0, (double) (o.coarse & 3) + o.fine / 100.0); }
} // namespace dx7
} // namespace tg
