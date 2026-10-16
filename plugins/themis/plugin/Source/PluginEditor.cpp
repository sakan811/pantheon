#include "PluginEditor.h"

ThemisEditor::ThemisEditor (ThemisProcessor& p)
    : AudioProcessorEditor (&p),
      proc (p),
      view (p),
      targetBox (p.apvts, params::target, params::targetNames()),
      avgBox (p.apvts, params::avgTime, params::avgTimeNames),
      toleranceSlider (p.apvts, params::tolerance, "Tolerance")
{
    setLookAndFeel (&laf);

    addAndMakeVisible (view);
    for (auto* c : std::initializer_list<juce::Component*> { &targetBox, &avgBox, &learnButton, &saveButton, &loadButton, &resetButton,
                                                              &statusLabel, &toleranceSlider })
        addAndMakeVisible (c);

    avgBox.setTooltip ("Averaging window for the long-term curve.");
    resetButton.setTooltip ("Clear the measured curve and start averaging again.");

    statusLabel.setJustificationType (juce::Justification::centredLeft);
    statusLabel.setFont (juce::Font (juce::FontOptions (12.5f)));

    learnButton.onClick = [this] { chooseReferenceTracks(); };
    saveButton.onClick = [this] { saveCurve(); };
    loadButton.onClick = [this] { loadCurve(); };
    resetButton.onClick = [this] { proc.resetAnalyzer(); };

    setStatus ("Tip: use 'Learn from tracks...' to build a target from your own reference masters.", false);

    setResizable (true, true);
    setResizeLimits (720, 420, 1900, 1100);
    setSize (1000, 560);
    startTimerHz (30);
}

ThemisEditor::~ThemisEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void ThemisEditor::timerCallback()
{
    view.update();
    if (statusTicks > 0 && --statusTicks == 0)
        statusLabel.setText ({}, juce::dontSendNotification);
}

void ThemisEditor::setStatus (const juce::String& text, bool isError)
{
    statusLabel.setColour (juce::Label::textColourId, isError ? colours::warn : colours::dim);
    statusLabel.setText (text, juce::dontSendNotification);
    statusTicks = 30 * (isError ? 12 : 20);
}

// ---- actions ------------------------------------------------------------------------------------
void ThemisEditor::chooseReferenceTracks()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose one or more reference tracks (final masters work best)",
                                                   juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                   "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.m4a;*.ogg");
    const auto dialogFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                       | juce::FileBrowserComponent::canSelectMultipleItems;
    chooser->launchAsync (dialogFlags, [safe = juce::Component::SafePointer<ThemisEditor> (this)] (const juce::FileChooser& fc) {
        if (safe == nullptr)
            return;
        const auto results = fc.getResults();
        if (results.isEmpty())
            return;
        safe->setStatus ("Analysing " + juce::String (results.size()) + " track(s)...", false);
        safe->proc.learnFromFiles (results, [safe] (const juce::String& error) {
            if (safe == nullptr)
                return;
            if (error.isEmpty())
                safe->setStatus ("Learned a new target curve and selected it (\"Custom\").", false);
            else
                safe->setStatus (error, true);
        });
    });
}

void ThemisEditor::saveCurve()
{
    chooser = std::make_unique<juce::FileChooser> ("Save the active target curve",
                                                   juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                                                       .getChildFile (juce::String (proc.activeTarget().name) + ".themiscurve"),
                                                   "*.themiscurve");
    const auto dialogFlags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                       | juce::FileBrowserComponent::warnAboutOverwriting;
    chooser->launchAsync (dialogFlags, [safe = juce::Component::SafePointer<ThemisEditor> (this)] (const juce::FileChooser& fc) {
        if (safe == nullptr)
            return;
        auto file = fc.getResult();
        if (file == juce::File())
            return;
        file = file.withFileExtension ("themiscurve");
        if (file.replaceWithText (juce::String (safe->proc.activeTarget().toJson())))
            safe->setStatus ("Saved " + file.getFileName(), false);
        else
            safe->setStatus ("Could not write " + file.getFullPathName(), true);
    });
}

void ThemisEditor::loadCurve()
{
    chooser = std::make_unique<juce::FileChooser> ("Load a target curve",
                                                   juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.themiscurve");
    const auto dialogFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    chooser->launchAsync (dialogFlags, [safe = juce::Component::SafePointer<ThemisEditor> (this)] (const juce::FileChooser& fc) {
        if (safe == nullptr)
            return;
        const auto file = fc.getResult();
        if (file == juce::File())
            return;
        themis::TargetCurve curve;
        if (themis::TargetCurve::fromJson (file.loadFileAsString().toStdString(), curve))
        {
            safe->proc.setCustomCurve (curve, true);
            safe->setStatus ("Loaded \"" + juce::String (curve.name) + "\".", false);
        }
        else
            safe->setStatus (file.getFileName() + " is not a valid Themis curve.", true);
    });
}

// ---- layout -------------------------------------------------------------------------------------
void ThemisEditor::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);

    g.setColour (colours::accent);
    g.setFont (juce::Font (juce::FontOptions (22.0f, juce::Font::bold)));
    g.drawText ("THEMIS", 14, 8, 120, 28, juce::Justification::centredLeft);
}

void ThemisEditor::resized()
{
    auto r = getLocalBounds().reduced (10, 8);

    auto head = r.removeFromTop (64);
    auto row1 = head.removeFromTop (30);
    row1.removeFromLeft (118);
    targetBox.setBounds (row1.removeFromLeft (190).reduced (0, 2));
    row1.removeFromLeft (6);
    learnButton.setBounds (row1.removeFromLeft (150).reduced (0, 2));
    row1.removeFromLeft (4);
    saveButton.setBounds (row1.removeFromLeft (84).reduced (0, 2));
    row1.removeFromLeft (4);
    loadButton.setBounds (row1.removeFromLeft (84).reduced (0, 2));

    auto row2 = head.reduced (0, 2);
    row2.removeFromLeft (118);
    avgBox.setBounds (row2.removeFromLeft (100));
    row2.removeFromLeft (6);
    resetButton.setBounds (row2.removeFromLeft (70));
    row2.removeFromLeft (12);
    toleranceSlider.setBounds (row2.removeFromLeft (190));
    row2.removeFromLeft (12);
    statusLabel.setBounds (row2);

    view.setBounds (r);
}
