// Headless smoke test for the JUCE layer: runs the real processor + editor without a host or a display.
//   ThemisSmoke [snapshot.png]
#include "../plugin/Source/PluginEditor.h"
#include "../plugin/Source/PluginProcessor.h"

#include <cstdio>
#include <random>

static int failures = 0, checks = 0;
#define CHECK(cond, ...)                                       \
    do                                                         \
    {                                                          \
        ++checks;                                              \
        if (!(cond))                                           \
        {                                                      \
            ++failures;                                        \
            std::printf ("  FAIL line %d: %s  ", __LINE__, #cond); \
            std::printf ("" __VA_ARGS__);                      \
            std::printf ("\n");                                \
        }                                                      \
    } while (0)

static std::vector<float> pinkNoise (size_t n, unsigned seed, float amp)
{
    std::mt19937 rng (seed);
    std::uniform_real_distribution<float> d (-1.0f, 1.0f);
    std::vector<float> v (n);
    double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    for (auto& x : v)
    {
        const double white = d (rng);
        b0 = 0.99886 * b0 + white * 0.0555179;
        b1 = 0.99332 * b1 + white * 0.0750759;
        b2 = 0.96900 * b2 + white * 0.1538520;
        b3 = 0.86650 * b3 + white * 0.3104856;
        b4 = 0.55000 * b4 + white * 0.5329522;
        b5 = -0.7616 * b5 - white * 0.0168980;
        x = static_cast<float> ((b0 + b1 + b2 + b3 + b4 + b5 + b6 + white * 0.5362) * 0.11 * amp);
        b6 = white * 0.115926;
    }
    return v;
}

static void runAudio (ThemisProcessor& proc, double seconds, double fs, int block, float amp, unsigned seed)
{
    const size_t total = static_cast<size_t> (seconds * fs);
    const auto l = pinkNoise (total, seed, amp), r = pinkNoise (total, seed + 1, amp);
    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;
    for (size_t pos = 0; pos < total; pos += static_cast<size_t> (block))
    {
        const int n = static_cast<int> (std::min<size_t> (static_cast<size_t> (block), total - pos));
        buf.setSize (2, n, false, false, true);
        buf.copyFrom (0, 0, l.data() + pos, n);
        buf.copyFrom (1, 0, r.data() + pos, n);
        proc.processBlock (buf, midi);
    }
}

static void pump (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const double fs = 48000.0;

    ThemisProcessor proc;
    proc.setPlayConfigDetails (2, 2, fs, 512);
    proc.prepareToPlay (fs, 512);

    std::printf ("[1] audio passes through untouched, zero latency, curve builds up\n");
    CHECK (proc.getLatencySamples() == 0, "latency=%d", proc.getLatencySamples());
    {
        const size_t n = 512;
        const auto l = pinkNoise (n, 1, 0.5f), r = pinkNoise (n, 2, 0.5f);
        juce::AudioBuffer<float> buf (2, static_cast<int> (n));
        juce::MidiBuffer midi;
        buf.copyFrom (0, 0, l.data(), static_cast<int> (n));
        buf.copyFrom (1, 0, r.data(), static_cast<int> (n));
        proc.processBlock (buf, midi);
        bool identical = true;
        for (size_t i = 0; i < n; ++i)
            identical = identical && buf.getSample (0, static_cast<int> (i)) == l[i] && buf.getSample (1, static_cast<int> (i)) == r[i];
        CHECK (identical, "processBlock must not alter the audio");
    }
    {
        themis::BandArray m {};
        runAudio (proc, 14.0, fs, 512, 0.5f, 1);
        CHECK (proc.readSpectrum (m), "analyser should have data after 14 s");
        const auto one = static_cast<size_t> (std::lround (themis::BandGrid::indexForHz (1000)));
        const auto eight = static_cast<size_t> (std::lround (themis::BandGrid::indexForHz (8000)));
        std::printf ("    pink noise: %.2f dB @1k, %.2f dB @8k (relative to anchor mean)\n", m[one], m[eight]);
        CHECK (std::abs (m[one]) < 1.5f && std::abs (m[eight]) < 1.5f);
        proc.resetAnalyzer();
        runAudio (proc, 0.1, fs, 512, 0.5f, 1);
        CHECK (!proc.readSpectrum (m), "reset should clear the curve");
    }

    std::printf ("[2] averaging parameter reaches the analyser setting\n");
    {
        auto* p = proc.apvts.getParameter (params::avgTime);
        p->setValueNotifyingHost (p->convertTo0to1 (3.0f));
        CHECK (proc.paramReader.averagingSeconds() == 0.0f);
        p->setValueNotifyingHost (p->convertTo0to1 (0.0f));
        CHECK (proc.paramReader.averagingSeconds() == 10.0f);
    }

    std::printf ("[4] state round trip incl. custom curve\n");
    {
        themis::TargetCurve c = themis::TargetCurve::builtIns()[2];
        c.name = "RoundTrip";
        proc.setCustomCurve (c, true);
        proc.apvts.getParameter (params::tolerance)->setValueNotifyingHost (proc.apvts.getParameter (params::tolerance)->convertTo0to1 (1.5f));
        juce::MemoryBlock blob;
        proc.getStateInformation (blob);

        ThemisProcessor other;
        other.setPlayConfigDetails (2, 2, fs, 512);
        other.prepareToPlay (fs, 512);
        other.setStateInformation (blob.getData(), static_cast<int> (blob.getSize()));
        CHECK (std::abs (other.paramReader.value (params::tolerance) - 1.5f) < 0.01f);
        CHECK (other.hasCustomCurve());
        CHECK (other.activeTarget().name == "RoundTrip");
        CHECK (std::abs (other.activeTarget().medianDb[10] - c.medianDb[10]) < 0.01f);
    }

    std::printf ("[5] learn a target from an audio file\n");
    {
        const auto wav = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("themis_smoke_pink.wav");
        wav.deleteFile();
        {
            juce::WavAudioFormat fmt;
            std::unique_ptr<juce::FileOutputStream> out (wav.createOutputStream());
            std::unique_ptr<juce::AudioFormatWriter> w (fmt.createWriterFor (out.get(), 44100.0, 2, 16, {}, 0));
            if (w != nullptr)
            {
                out.release();
                const auto l = pinkNoise (44100 * 14, 5, 0.5f), r = pinkNoise (44100 * 14, 6, 0.5f);
                const float* chans[] = { l.data(), r.data() };
                w->writeFromFloatArrays (chans, 2, static_cast<int> (l.size()));
            }
        }
        CHECK (wav.existsAsFile());
        bool done = false;
        juce::String error = "not called";
        proc.learnFromFiles ({ wav }, [&] (const juce::String& e) {
            error = e;
            done = true;
        });
        for (int i = 0; i < 400 && !done; ++i)
            pump (50);
        CHECK (done, "callback never fired");
        CHECK (error.isEmpty(), "%s", error.toRawUTF8());
        const auto t = proc.activeTarget();
        std::printf ("    learned \"%s\"; median @1k = %.2f dB, @100 = %.2f dB, @8k = %.2f dB\n", t.name.c_str(),
                     t.medianDb[static_cast<size_t> (std::lround (themis::BandGrid::indexForHz (1000)))],
                     t.medianDb[static_cast<size_t> (std::lround (themis::BandGrid::indexForHz (100)))],
                     t.medianDb[static_cast<size_t> (std::lround (themis::BandGrid::indexForHz (8000)))]);
        CHECK (t.name == "themis_smoke_pink");
        CHECK (std::abs (t.medianDb[static_cast<size_t> (std::lround (themis::BandGrid::indexForHz (1000)))]) < 1.5f);
        CHECK (std::abs (t.medianDb[static_cast<size_t> (std::lround (themis::BandGrid::indexForHz (8000)))]) < 1.5f);
        wav.deleteFile();

        proc.learnFromFiles ({ juce::File ("/nonexistent/nothing.wav") }, [&] (const juce::String& e) {
            error = e;
            done = true;
        });
        done = false;
        for (int i = 0; i < 100 && !done; ++i)
            pump (50);
        CHECK (done && error.isNotEmpty(), "bad file should report an error");
    }

    std::printf ("[6] editor builds, paints and survives resizing\n");
    {
        runAudio (proc, 14.0, fs, 512, 0.5f, 3); // so the screenshot shows a measured curve
        std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
        CHECK (editor != nullptr);
        if (editor != nullptr)
        {
            editor->setSize (1000, 560);
            pump (400); // let the 30 Hz timer pull data
            auto img = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
            CHECK (img.isValid() && img.getWidth() == 1000);
            if (argc > 1)
            {
                juce::File png (argv[1]);
                png.deleteFile();
                juce::PNGImageFormat fmt;
                if (auto out = png.createOutputStream())
                    fmt.writeImageToStream (img, *out);
                std::printf ("    snapshot -> %s\n", png.getFullPathName().toRawUTF8());
            }
            editor->setSize (720, 420);
            editor->setSize (1900, 1100);
            pump (100);
        }
    }

    proc.releaseResources();
    std::printf ("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
