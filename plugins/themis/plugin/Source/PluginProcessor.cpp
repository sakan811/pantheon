#include "PluginProcessor.h"

#include "PluginEditor.h"

ThemisProcessor::ThemisProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "THEMIS", params::createLayout()),
      paramReader (apvts)
{
}

ThemisProcessor::~ThemisProcessor()
{
    stopAnalysis();
    alive->store (false);
    if (pool != nullptr)
        pool->removeAllJobs (true, 10000);
}

void ThemisProcessor::stopAnalysis()
{
    if (analysisThread != nullptr)
        analysisThread->stopThread (2000);
    analysisThread.reset();
}

void ThemisProcessor::prepareToPlay (double sr, int)
{
    stopAnalysis(); // prepare() must not overlap with pump()
    analyzer.prepare (sr);
    lowCrest.prepare (sr);
    scope.prepare (sr);
    prepared = true;
    analysisThread = std::make_unique<AnalysisThread> (analyzer);
    analysisThread->startThread();
}

void ThemisProcessor::releaseResources()
{
    prepared = false;
    stopAnalysis();
}

bool ThemisProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
           && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void ThemisProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    if (!prepared || buffer.getNumChannels() < 2 || buffer.getNumSamples() == 0)
        return;

    // Measure only - the audio itself is left exactly as it came in. The FFT work happens on the analysis thread.
    const float avgSeconds = paramReader.averagingSeconds();
    analyzer.setAveragingSeconds (avgSeconds);
    analyzer.push (buffer.getReadPointer (0), buffer.getReadPointer (1), buffer.getNumSamples());
    lowCrest.setAveragingSeconds (avgSeconds);
    lowCrest.process (buffer.getReadPointer (0), buffer.getReadPointer (1), buffer.getNumSamples());
    scope.push (buffer.getReadPointer (0), buffer.getReadPointer (1), buffer.getNumSamples());
}

juce::AudioProcessorEditor* ThemisProcessor::createEditor()
{
    return new ThemisEditor (*this);
}

// ---- state ------------------------------------------------------------------------------------
void ThemisProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    {
        const juce::ScopedLock sl (curveLock);
        if (customCurveValid)
            state.setProperty ("customCurve", juce::String (customCurve.toJson()), nullptr);
    }
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void ThemisProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || !xml->hasTagName (apvts.state.getType()))
        return;
    const auto tree = juce::ValueTree::fromXml (*xml);
    apvts.replaceState (tree);

    // The loaded state replaces the custom curve; one without a (valid) curve must not keep the previous one.
    themis::TargetCurve c;
    const bool valid = themis::TargetCurve::fromJson (tree.getProperty ("customCurve").toString().toStdString(), c);
    const juce::ScopedLock sl (curveLock);
    customCurveValid = valid;
    if (valid)
        customCurve = c;
}

// ---- analysis / targets -----------------------------------------------------------------------
bool ThemisProcessor::readSpectrum (themis::BandArray& out) const
{
    if (!analyzer.hasData())
        return false;
    analyzer.readNormalisedDb (out);
    return true;
}

bool ThemisProcessor::readLowCrest (float& outDb) const
{
    if (!lowCrest.hasData())
        return false;
    outDb = lowCrest.crestDb();
    return true;
}

themis::TargetCurve ThemisProcessor::activeTarget() const
{
    const auto& built = themis::TargetCurve::builtIns();
    const int idx = static_cast<int> (paramReader.value (params::target));
    if (idx >= 0 && idx < static_cast<int> (built.size()))
        return built[static_cast<size_t> (idx)];
    const juce::ScopedLock sl (curveLock);
    return customCurveValid ? customCurve : built.front();
}

bool ThemisProcessor::hasCustomCurve() const
{
    const juce::ScopedLock sl (curveLock);
    return customCurveValid;
}

void ThemisProcessor::setParameterValue (const juce::String& id, float plainValue)
{
    if (auto* p = apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (plainValue));
        p->endChangeGesture();
    }
}

void ThemisProcessor::setCustomCurve (const themis::TargetCurve& c, bool selectIt)
{
    {
        const juce::ScopedLock sl (curveLock);
        customCurve = c;
        customCurveValid = true;
    }
    if (selectIt)
        setParameterValue (params::target, static_cast<float> (themis::TargetCurve::builtIns().size()));
}

void ThemisProcessor::learnFromFiles (const juce::Array<juce::File>& files, std::function<void (const juce::String&)> onDone)
{
    if (learning.exchange (true))
    {
        onDone ("Already analysing reference tracks.");
        return;
    }

    if (pool == nullptr)
        pool = std::make_unique<juce::ThreadPool> (1);
    pool->addJob ([this, files, onDone, aliveFlag = alive] {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();

        std::vector<themis::BandArray> tracks;
        juce::StringArray failed;
        for (const auto& file : files)
        {
            if (!aliveFlag->load())
                return;
            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
            if (reader == nullptr || reader->lengthInSamples <= 0)
            {
                failed.add (file.getFileName());
                continue;
            }
            themis::TonalAnalyzer trackAnalyzer;
            trackAnalyzer.prepare (reader->sampleRate);
            trackAnalyzer.setAveragingSeconds (0.0f); // whole-track average

            constexpr int chunkSize = 1 << 15;
            juce::AudioBuffer<float> buf (2, chunkSize);
            const bool mono = reader->numChannels < 2;
            for (juce::int64 pos = 0; pos < reader->lengthInSamples && aliveFlag->load(); pos += chunkSize)
            {
                const int n = static_cast<int> (std::min<juce::int64> (chunkSize, reader->lengthInSamples - pos));
                reader->read (&buf, 0, n, pos, true, !mono);
                trackAnalyzer.process (buf.getReadPointer (0), buf.getReadPointer (mono ? 0 : 1), n);
            }
            themis::BandArray arr {};
            if (trackAnalyzer.hasData())
            {
                trackAnalyzer.readNormalisedDb (arr);
                tracks.push_back (arr);
            }
            else
                failed.add (file.getFileName() + " (too short or silent)");
        }

        juce::String error;
        themis::TargetCurve curve;
        if (tracks.empty())
            error = "Could not analyse any of the files" + (failed.isEmpty() ? juce::String() : ": " + failed.joinIntoString (", "));
        else
        {
            const auto name = files.size() == 1 ? files[0].getFileNameWithoutExtension()
                                                : "Custom (" + juce::String (static_cast<int> (tracks.size())) + " tracks)";
            curve = themis::TargetCurve::fromTracks (name.toStdString(), tracks);
            if (!failed.isEmpty())
                error = "Skipped: " + failed.joinIntoString (", ");
        }

        juce::MessageManager::callAsync ([this, aliveFlag, curve, error, onDone, ok = !tracks.empty()] {
            if (!aliveFlag->load())
                return;
            if (ok)
                setCustomCurve (curve, true);
            learning.store (false);
            onDone (error);
        });
    });
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ThemisProcessor();
}
