#include "PluginEditor.h"

ThemisEditor::ThemisEditor (ThemisProcessor& p)
    : AudioProcessorEditor (&p),
      proc (p),
      view (p),
      scopeView (p),
      targetBox (p.apvts, params::target, params::targetNames()),
      avgBox (p.apvts, params::avgTime, params::avgTimeNames()),
      deviceBox (p.apvts, params::device, params::deviceNames()),
      toleranceSlider (p.apvts, params::tolerance, "Tolerance")
{
    setLookAndFeel (&laf);

    addAndMakeVisible (view);
    addAndMakeVisible (scopeView);
    for (auto* c : std::initializer_list<juce::Component*> { &targetBox, &avgBox, &deviceBox, &deviceLabel, &learnButton, &saveButton, &loadButton, &resetButton,
                                                              &statusLabel, &toleranceSlider })
        addAndMakeVisible (c);

    avgBox.setTooltip ("Averaging window for the long-term curve.");
    deviceBox.setTooltip ("Preview how the balance would translate to a typical playback device (approximate response; "
                          "analysis only, the audio is never changed). \"Off\" hides the preview.");
    deviceLabel.setText ("Translate to", juce::dontSendNotification);
    deviceLabel.setColour (juce::Label::textColourId, colours::dim);
    deviceLabel.setFont (ui::font (11.5f));
    deviceLabel.setJustificationType (juce::Justification::centredRight);
    resetButton.setTooltip ("Clear the measured curve and start averaging again.");

    statusLabel.setJustificationType (juce::Justification::centredLeft);
    statusLabel.setFont (ui::font (12.5f));

    learnButton.onClick = [this] { chooseReferenceTracks(); };
    saveButton.onClick = [this] { saveCurve(); };
    loadButton.onClick = [this] { loadCurve(); };
    resetButton.onClick = [this] { proc.resetAnalyzer(); };

    setStatus ("Tip: use 'Learn from tracks...' to build a target from your own reference masters.", false);

    setResizable (true, true);
    setResizeLimits (900, 440, 1900, 1100);
    setSize (1240, 580);
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
    scopeView.update();
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
// Shows a file chooser and calls `onChosen` with it if the editor still exists when the user is done.
void ThemisEditor::launchChooser (const juce::String& title, const juce::File& start, const juce::String& patterns, int flags,
                                  std::function<void (ThemisEditor&, const juce::FileChooser&)> onChosen)
{
    chooser = std::make_unique<juce::FileChooser> (title, start, patterns);
    chooser->launchAsync (flags, [safe = juce::Component::SafePointer<ThemisEditor> (this), handler = std::move (onChosen)] (const juce::FileChooser& fc) {
        if (safe != nullptr)
            handler (*safe, fc);
    });
}

void ThemisEditor::chooseReferenceTracks()
{
    launchChooser ("Choose one or more reference tracks (final masters work best)",
                   juce::File::getSpecialLocation (juce::File::userMusicDirectory), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.m4a;*.ogg",
                   juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems,
                   [] (ThemisEditor& self, const juce::FileChooser& fc) {
                       const auto results = fc.getResults();
                       if (results.isEmpty())
                           return;
                       self.setStatus ("Analysing " + juce::String (results.size()) + " track(s)...", false);
                       self.proc.learnFromFiles (results, [safe = juce::Component::SafePointer<ThemisEditor> (&self)] (const juce::String& error) {
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
    launchChooser ("Save the active target curve",
                   juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                       .getChildFile (juce::File::createLegalFileName (juce::String (proc.activeTarget().name)) + kCurveExtension),
                   "*" + juce::String (kCurveExtension),
                   juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                   [] (ThemisEditor& self, const juce::FileChooser& fc) {
                       auto file = fc.getResult();
                       if (file == juce::File())
                           return;
                       if (!file.hasFileExtension (kCurveExtension)) // append: withFileExtension would cut "Mix 1.5" down to "Mix 1"
                           file = file.getSiblingFile (file.getFileName() + kCurveExtension);
                       if (file.replaceWithText (juce::String (self.proc.activeTarget().toJson())))
                           self.setStatus ("Saved " + file.getFileName(), false);
                       else
                           self.setStatus ("Could not write " + file.getFullPathName(), true);
                   });
}

void ThemisEditor::loadCurve()
{
    launchChooser ("Load a target curve", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*" + juce::String (kCurveExtension),
                   juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                   [] (ThemisEditor& self, const juce::FileChooser& fc) {
                       const auto file = fc.getResult();
                       if (file == juce::File())
                           return;
                       themis::TargetCurve curve;
                       if (themis::TargetCurve::fromJson (file.loadFileAsString().toStdString(), curve))
                       {
                           self.proc.setCustomCurve (curve, true);
                           self.setStatus ("Loaded \"" + juce::String (curve.name) + "\".", false);
                       }
                       else
                           self.setStatus (file.getFileName() + " is not a valid Themis curve.", true);
                   });
}

// ---- layout -------------------------------------------------------------------------------------
void ThemisEditor::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);

    g.setColour (colours::accent);
    g.setFont (ui::font (22.0f, juce::Font::bold));
    g.drawText ("THEMIS", 14, 8, kTitleWidth, 28, juce::Justification::centredLeft);
}

void ThemisEditor::resized()
{
    auto r = getLocalBounds().reduced (10, 8);

    auto head = r.removeFromTop (64);
    auto row1 = head.removeFromTop (30);
    row1.removeFromLeft (kTitleWidth);
    targetBox.setBounds (row1.removeFromLeft (190).reduced (0, 2));
    row1.removeFromLeft (6);
    learnButton.setBounds (row1.removeFromLeft (150).reduced (0, 2));
    row1.removeFromLeft (4);
    saveButton.setBounds (row1.removeFromLeft (84).reduced (0, 2));
    row1.removeFromLeft (4);
    loadButton.setBounds (row1.removeFromLeft (84).reduced (0, 2));
    row1.removeFromLeft (10);
    deviceLabel.setBounds (row1.removeFromLeft (76));
    row1.removeFromLeft (4);
    deviceBox.setBounds (row1.removeFromLeft (150).reduced (0, 2));

    auto row2 = head.reduced (0, 2);
    row2.removeFromLeft (kTitleWidth);
    avgBox.setBounds (row2.removeFromLeft (100));
    row2.removeFromLeft (6);
    resetButton.setBounds (row2.removeFromLeft (70));
    row2.removeFromLeft (12);
    toleranceSlider.setBounds (row2.removeFromLeft (190));
    row2.removeFromLeft (12);
    statusLabel.setBounds (row2);

    scopeView.setBounds (r.removeFromRight (270));
    r.removeFromRight (8);
    view.setBounds (r);
}
