// Channels.h — the channel processing modes shared by most modules: in
// STEREO one parameter set runs on both sides; in MID/SIDE or LEFT/RIGHT
// set A runs on mid (or left) and set B on side (or right).
#pragma once

#include "Dsp.h"
#include "Module.h"

namespace burnish
{
struct ChannelModes
{
    static constexpr int kMaxSets = 2;

    // Parameter set used by physical channel c (0 or 1) in this mode.
    static int setFor (int mode, int c) { return mode == Stereo ? 0 : c; }

    // Converts a stereo pair into the two processing channels for the mode.
    // Safe to call with a aliasing l and b aliasing r.
    static void split (int mode, float l, float r, float& a, float& b)
    {
        if (mode == MidSide) { const float m = 0.5f * (l + r), s = 0.5f * (l - r); a = m; b = s; }
        else { a = l; b = r; }
    }
    static void join (int mode, float a, float b, float& l, float& r)
    {
        if (mode == MidSide) { const float x = a + b, y = a - b; l = x; r = y; }
        else { l = a; r = b; }
    }
};
} // namespace burnish
