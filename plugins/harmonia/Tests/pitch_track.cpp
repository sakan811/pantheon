// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 The Pantheon authors

// Independent pitch tracker for checking results: pitch_track in.wav out.csv [minHz maxHz]
// Writes "t,midi" every 5 ms (midi = 0 when unvoiced), using the same YIN detector as the engine.

#include "../Source/dsp/PitchDetector.h"
#include "WavIO.h"

#include <cstdlib>

int main (int argc, char** argv)
{
    if (argc < 3)
    {
        std::fprintf (stderr, "usage: pitch_track in.wav out.csv [minHz maxHz]\n");
        return 1;
    }
    const float minHz = argc > 3 ? (float) std::atof (argv[3]) : 75.0f;
    const float maxHz = argc > 4 ? (float) std::atof (argv[4]) : 1000.0f;

    wavio::Wav in;
    if (! wavio::read (argv[1], in))
        return 1;

    harmonia::PitchDetector det;
    det.prepare (in.sampleRate, 75.0f);
    FILE* f = std::fopen (argv[2], "w");
    std::fprintf (f, "t,midi\n");

    const int hop = in.sampleRate / 200;
    float prev = 0.0f;
    bool was = false;
    for (size_t i = 0; i < in.data[0].size(); ++i)
    {
        float m = 0.0f;
        for (const auto& ch : in.data)
            m += ch[i];
        det.push (m / (float) in.data.size());
        if ((i + 1) % (size_t) hop == 0)
        {
            const auto r = det.detect (minHz, maxHz, prev, was);
            was = r.voiced;
            if (r.voiced)
                prev = r.period;
            std::fprintf (f, "%.4f,%.3f\n", (double) (i + 1) / in.sampleRate, r.voiced ? 69.0 + 12.0 * std::log2 (in.sampleRate / r.period / 440.0) : 0.0);
        }
    }
    std::fclose (f);
    return 0;
}
