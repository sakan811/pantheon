// Developer tool (not part of the shipped plug-in):
//   HarmoniaDevTool selftest              drive the real HarmoniaProcessor with a synthetic vocal
//   HarmoniaDevTool snapshot out.png      render the editor (after feeding it a phrase) to a PNG

#include "../Source/PluginEditor.h"
#include "../Source/PluginProcessor.h"
#include "TestSignals.h"

#include <cstdio>

static void setParam (HarmoniaProcessor& p, const juce::String& id, float plain)
{
    auto* param = p.apvts.getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 (plain));
}

static std::vector<float> run (HarmoniaProcessor& p, const std::vector<float>& in, int block = 512)
{
    std::vector<float> out (in.size());
    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;
    for (size_t pos = 0; pos < in.size(); pos += (size_t) block)
    {
        const int n = (int) std::min<size_t> ((size_t) block, in.size() - pos);
        buf.setSize (2, n, false, false, true);
        for (int i = 0; i < n; ++i)
        {
            buf.setSample (0, i, in[pos + (size_t) i]);
            buf.setSample (1, i, in[pos + (size_t) i]);
        }
        p.processBlock (buf, midi);
        for (int i = 0; i < n; ++i)
            out[pos + (size_t) i] = buf.getSample (0, i);
    }
    return out;
}

static int selftest()
{
    constexpr double fs = 48000.0;
    int failures = 0;
    auto check = [&] (bool ok, const char* what, double v) {
        std::printf ("  [%s] %-60s %8.3f\n", ok ? "PASS" : "FAIL", what, v);
        failures += ok ? 0 : 1;
    };

    HarmoniaProcessor proc;
    proc.setPlayConfigDetails (2, 2, fs, 512);
    proc.prepareToPlay (fs, 512);

    // A major, sing C#4 (61) 30 cents flat, then E4 (64) 25 cents sharp... but E is in A major, C# too.
    // Use A major (key = 9, scale = Major) and sing D#4 (63), which is NOT in the scale: it must land on D4 or E4.
    setParam (proc, ids::key, 9);
    setParam (proc, ids::scale, (float) harmonia::kMajorScale);
    setParam (proc, ids::speed, 0.0f);
    setParam (proc, ids::natural, 0.0f);

    std::printf ("Processor + parameters\n");
    check (proc.currentNoteMask() == harmonia::scaleMask (9, harmonia::kMajorScale), "A major mask from parameters", proc.currentNoteMask());

    auto voice = [&] (double midi) {
        return testsig::makeVoice (fs, 2.5, [midi] (double) { return testsig::midiToHz (midi); });
    };
    auto measure = [&] (const std::vector<float>& x, double expected) {
        auto ps = testsig::measurePitch (x, fs);
        std::vector<double> v;
        const double lat = proc.getLatencySamples() / fs;
        for (auto& s : ps)
            if (s.t > 1.2 + lat && s.t < 2.3)
                v.push_back ((s.midi - expected) * 100.0);
        return testsig::stats (v);
    };

    {
        auto out = run (proc, voice (69.0 - 0.33)); // A4, 33 cents flat, in key
        auto s = measure (out, 69.0);
        check (std::fabs (s.mean) < 2.0 && s.stddev < 2.0, "A4 33c flat -> A4 (in key)", s.mean);
    }
    {
        proc.prepareToPlay (fs, 512);
        auto out = run (proc, voice (63.45)); // D#4 + 45c: nearest in-key note is E4 (64)
        auto s = measure (out, 64.0);
        check (std::fabs (s.mean) < 2.0 && s.stddev < 2.0, "out-of-key D#4 +45c -> E4", s.mean);
    }
    {
        // Custom scale: only allow C and G.
        proc.prepareToPlay (fs, 512);
        setParam (proc, ids::scale, (float) harmonia::kCustomScale);
        for (int i = 0; i < 12; ++i)
            setParam (proc, ids::note (i), (i == 0 || i == 7) ? 1.0f : 0.0f);
        check (proc.currentNoteMask() == ((1u << 0) | (1u << 7)), "custom mask = C + G", proc.currentNoteMask());
        auto out = run (proc, voice (65.2)); // F4 + 20c -> between C and G; nearest allowed is G4 (67)? F4=65: C5(72) 7 away, G4(67) 2 away
        auto s = measure (out, 67.0);
        check (std::fabs (s.mean) < 2.0 && s.stddev < 2.0, "custom scale {C,G}: F4 -> G4", s.mean);
    }
    {
        // Bypass: output equals the delayed input.
        proc.prepareToPlay (fs, 512);
        setParam (proc, ids::bypass, 1.0f);
        auto in = voice (63.45);
        auto out = run (proc, in);
        const int lat = proc.getLatencySamples();
        double maxDiff = 0;
        for (size_t i = (size_t) (fs * 1.0); i < in.size(); ++i)
            maxDiff = std::max (maxDiff, (double) std::fabs (in[i - (size_t) lat] - out[i]));
        check (maxDiff < 1e-4, "bypass = input delayed by reported latency", maxDiff);
        setParam (proc, ids::bypass, 0.0f);
    }
    {
        // Mono-sized blocks / tiny blocks must not crash.
        proc.prepareToPlay (fs, 64);
        auto out = run (proc, voice (60.0), 7);
        bool finite = true;
        for (float s : out)
            finite = finite && std::isfinite (s);
        check (finite, "tiny odd block sizes stay finite", finite ? 1 : 0);
    }
    {
        // State round trip.
        setParam (proc, ids::speed, 123.0f);
        juce::MemoryBlock mb;
        proc.getStateInformation (mb);
        setParam (proc, ids::speed, 5.0f);
        proc.setStateInformation (mb.getData(), (int) mb.getSize());
        check (std::fabs (proc.apvts.getRawParameterValue (ids::speed)->load() - 123.0f) < 0.5f, "state save/restore", proc.apvts.getRawParameterValue (ids::speed)->load());
    }
    {
        // Key detection: sing a C major arpeggio-ish melody and ask for the key.
        proc.prepareToPlay (fs, 512);
        setParam (proc, ids::bypass, 0.0f);
        const double notes[] = { 60, 64, 67, 64, 62, 65, 69, 65, 60, 67, 64, 60 };
        auto in = testsig::makeVoice (fs, 8.0, [&] (double t) {
            const int k = (int) (t / 0.6);
            return k >= 12 ? 0.0 : testsig::midiToHz (notes[k] + 0.12);
        });
        proc.startKeyLearn();
        auto chunk = run (proc, std::vector<float> (in.begin(), in.begin() + 1024)); // let the command be picked up
        (void) chunk;
        run (proc, std::vector<float> (in.begin() + 1024, in.end()));
        proc.finishKeyLearn();
        run (proc, std::vector<float> (4096, 0.0f));
        HarmoniaProcessor::KeyResult r;
        const bool got = proc.takeKeyResult (r);
        check (got && r.root == 0 && ! r.minor, "key detection finds C major", got ? r.root + (r.minor ? 100 : 0) : -1);
    }

    std::printf ("\n%s\n", failures == 0 ? "PROCESSOR TESTS PASSED" : "PROCESSOR TESTS FAILED");
    return failures == 0 ? 0 : 1;
}

static int snapshot (const char* path, double feedSeconds)
{
    constexpr double fs = 48000.0;
    HarmoniaProcessor proc;
    proc.setPlayConfigDetails (2, 2, fs, 512);
    proc.prepareToPlay (fs, 512);

    setParam (proc, ids::key, 0);
    setParam (proc, ids::scale, (float) harmonia::kMajorScale);
    setParam (proc, ids::speed, 40.0f);
    setParam (proc, ids::natural, 70.0f);

    // a few seconds of an off-key, vibrato-laden phrase
    const double notes[] = { 60.0 - 0.35, 62.0 + 0.30, 64.0 - 0.20, 65.0 + 0.40, 67.0 - 0.30, 64.0 + 0.2 };
    auto in = testsig::makeVoice (fs, 4.2, [&] (double t) {
        const int k = (int) (t / 0.68);
        if (k >= 6 || std::fmod (t, 0.68) > 0.62)
            return 0.0;
        return testsig::midiToHz (notes[k] + 0.22 * std::sin (2.0 * testsig::kPi * 5.6 * t));
    });
    in.resize ((size_t) (feedSeconds * fs));
    run (proc, in);

    auto* editor = new HarmoniaEditor (proc);
    editor->setSize (1760, 1120);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (400);
    auto img = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
    juce::File f (path);
    f.deleteFile();
    juce::FileOutputStream out (f);
    juce::PNGImageFormat().writeImageToStream (img, out);
    delete editor;
    std::printf ("wrote %s (%d x %d)\n", path, img.getWidth(), img.getHeight());
    return 0;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc >= 2 && std::string (argv[1]) == "selftest")
        return selftest();
    if (argc >= 3 && std::string (argv[1]) == "snapshot")
        return snapshot (argv[2], argc >= 4 ? std::atof (argv[3]) : 4.2);
    std::printf ("usage: HarmoniaDevTool selftest | snapshot out.png\n");
    return 1;
}
