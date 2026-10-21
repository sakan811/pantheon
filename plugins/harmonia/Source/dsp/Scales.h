#pragma once

#include <cmath>
#include <cstdint>
#include <initializer_list>

namespace harmonia
{
// A note mask is 12 bits, bit 0 = C, bit 1 = C#, ... bit 11 = B (absolute pitch classes).
// Scale intervals are stored relative to the root (bit 0 = root).

constexpr uint16_t intervalBits (std::initializer_list<int> semitones)
{
    uint16_t m = 0;
    for (int s : semitones)
        m = (uint16_t) (m | (1u << s));
    return m;
}

inline constexpr uint16_t kAllNotes = 0x0FFF;

struct ScaleInfo
{
    const char* name;
    uint16_t intervals;
};

inline constexpr ScaleInfo kScales[] = {
    { "Chromatic",      kAllNotes }, // must stay at kChromaticScale
    { "Major",          intervalBits ({ 0, 2, 4, 5, 7, 9, 11 }) },
    { "Minor",          intervalBits ({ 0, 2, 3, 5, 7, 8, 10 }) },
    { "Harmonic Minor", intervalBits ({ 0, 2, 3, 5, 7, 8, 11 }) },
    { "Melodic Minor",  intervalBits ({ 0, 2, 3, 5, 7, 9, 11 }) },
    { "Dorian",         intervalBits ({ 0, 2, 3, 5, 7, 9, 10 }) },
    { "Phrygian",       intervalBits ({ 0, 1, 3, 5, 7, 8, 10 }) },
    { "Lydian",         intervalBits ({ 0, 2, 4, 6, 7, 9, 11 }) },
    { "Mixolydian",     intervalBits ({ 0, 2, 4, 5, 7, 9, 10 }) },
    { "Major Pentatonic", intervalBits ({ 0, 2, 4, 7, 9 }) },
    { "Minor Pentatonic", intervalBits ({ 0, 3, 5, 7, 10 }) },
    { "Blues",          intervalBits ({ 0, 3, 5, 6, 7, 10 }) },
    { "Custom",         kAllNotes },
};

inline constexpr int kNumScales = (int) (sizeof (kScales) / sizeof (kScales[0]));
inline constexpr int kChromaticScale = 0;
inline constexpr int kMajorScale = 1;
inline constexpr int kMinorScale = 2;
inline constexpr int kCustomScale = kNumScales - 1;

// Chromatic and Custom scales are not tied to a key; the others are relative to the root note.
constexpr bool scaleUsesRoot (int scaleIndex)
{
    return scaleIndex != kChromaticScale && scaleIndex != kCustomScale;
}

// Pitch class (0 = C .. 11 = B) of a MIDI note number, also for negative numbers.
constexpr int pitchClass (int midiNote)
{
    return ((midiNote % 12) + 12) % 12;
}

inline constexpr const char* kNoteNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

inline uint16_t rotateToRoot (uint16_t intervals, int root)
{
    uint16_t out = 0;
    for (int i = 0; i < 12; ++i)
        if (intervals & (1u << i))
            out = (uint16_t) (out | (1u << ((i + root) % 12)));
    return out;
}

// Absolute note mask for a preset scale (Custom is handled by the caller).
inline uint16_t scaleMask (int root, int scaleIndex)
{
    return rotateToRoot (kScales[scaleIndex].intervals, root);
}

struct KeyGuess
{
    int root = 0;
    bool minor = false;
    float score = 0.0f; // Pearson correlation, -1..1
};

// Krumhansl-Schmuckler key finding on a 12-bin pitch-class histogram.
inline KeyGuess guessKey (const float* hist)
{
    static const float major[12] = { 6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f, 2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f };
    static const float minor[12] = { 6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f, 2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f };

    float meanH = 0.0f;
    for (int i = 0; i < 12; ++i)
        meanH += hist[i];
    meanH /= 12.0f;

    KeyGuess best;
    best.score = -2.0f;

    for (int mode = 0; mode < 2; ++mode)
    {
        const float* prof = mode == 0 ? major : minor;
        float meanP = 0.0f;
        for (int i = 0; i < 12; ++i)
            meanP += prof[i];
        meanP /= 12.0f;

        for (int root = 0; root < 12; ++root)
        {
            float num = 0.0f, dh = 0.0f, dp = 0.0f;
            for (int i = 0; i < 12; ++i)
            {
                const float h = hist[(i + root) % 12] - meanH;
                const float p = prof[i] - meanP;
                num += h * p;
                dh += h * h;
                dp += p * p;
            }
            const float r = (dh > 0.0f && dp > 0.0f) ? num / std::sqrt (dh * dp) : 0.0f;
            if (r > best.score)
            {
                best.score = r;
                best.root = root;
                best.minor = (mode == 1);
            }
        }
    }
    return best;
}
} // namespace harmonia
