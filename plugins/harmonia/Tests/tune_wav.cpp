// Offline command-line front end for the Harmonia engine: tune a WAV file.
//   tune_wav in.wav out.wav [--key C] [--scale major|minor|chromatic|...] [--speed ms]
//            [--natural 0..1] [--strength 0..1] [--formant semitones] [--range low|mid|high|auto]
// Prints a pitch report; the output is delay-compensated so it lines up with the input.

#include "../Source/dsp/AutoTuneEngine.h"
#include "WavIO.h"

#include <cstdlib>
#include <cstring>
#include <string>

using namespace harmonia;

static std::string lowerCase (std::string s)
{
    for (auto& c : s)
        c = (char) std::tolower ((unsigned char) c);
    return s;
}

int main (int argc, char** argv)
{
    if (argc < 3)
    {
        std::fprintf (stderr, "usage: tune_wav in.wav out.wav [--key C] [--scale major] [--speed 40] [--natural 0.7] [--strength 1] [--formant 0] [--range auto]\n");
        return 1;
    }

    EngineParams p;
    int root = 0, scale = 0;
    for (int i = 3; i + 1 < argc; i += 2)
    {
        const std::string k = argv[i], v = argv[i + 1];
        if (k == "--key")
        {
            for (int n = 0; n < 12; ++n)
                if (v == kNoteNames[n])
                    root = n;
        }
        else if (k == "--scale")
        {
            for (int n = 0; n < kNumScales; ++n)
                if (lowerCase (kScales[n].name) == lowerCase (v))
                    scale = n;
        }
        else if (k == "--speed")
            p.corrector.retuneMs = (float) std::atof (v.c_str());
        else if (k == "--natural")
            p.corrector.natural = (float) std::atof (v.c_str());
        else if (k == "--strength")
            p.corrector.strength = (float) std::atof (v.c_str());
        else if (k == "--formant")
            p.formantSemitones = (float) std::atof (v.c_str());
        else if (k == "--range")
        {
            for (int n = 0; n < kNumVoiceRanges; ++n)
                if (lowerCase (kVoiceRanges[n].name).rfind (lowerCase (v), 0) == 0) // "low" matches "Low (bass / baritone)"
                    p.setVoiceRange (n);
        }
    }
    p.corrector.noteMask = scaleMask (root, scale);

    wavio::Wav in;
    if (! wavio::read (argv[1], in))
    {
        std::fprintf (stderr, "cannot read %s\n", argv[1]);
        return 1;
    }

    AutoTuneEngine eng;
    eng.prepare (in.sampleRate, std::min (in.channels, 2));
    eng.setParams (p);

    const int nch = std::min (in.channels, 2);
    const size_t frames = in.data[0].size();
    const int lat = eng.latencySamples();
    wavio::Wav out;
    out.sampleRate = in.sampleRate;
    out.channels = nch;
    out.data.assign ((size_t) nch, std::vector<float> (frames));

    std::vector<std::vector<float>> work ((size_t) nch);
    for (int c = 0; c < nch; ++c)
    {
        work[(size_t) c] = in.data[(size_t) c];
        work[(size_t) c].resize (frames + (size_t) lat, 0.0f); // flush the look-ahead
    }
    const size_t total = frames + (size_t) lat;
    for (size_t pos = 0; pos < total; pos += 512)
    {
        const int n = (int) std::min<size_t> (512, total - pos);
        float* ch[2] = { work[0].data() + pos, nch > 1 ? work[1].data() + pos : nullptr };
        eng.process (ch, nch, n);
    }
    for (int c = 0; c < nch; ++c)
        std::copy (work[(size_t) c].begin() + lat, work[(size_t) c].begin() + lat + (long) frames, out.data[(size_t) c].begin());

    if (! wavio::write (argv[2], out))
    {
        std::fprintf (stderr, "cannot write %s\n", argv[2]);
        return 1;
    }

    std::printf ("processed %zu frames @ %d Hz, latency %d samples, starved %d\n", frames, in.sampleRate, lat, eng.starvedEvents());
    return 0;
}
