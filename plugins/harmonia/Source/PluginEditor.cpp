// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 The Pantheon authors

#include "PluginEditor.h"

using namespace harmonia;

namespace
{
struct Preset
{
    const char* name;
    float speed, strength, natural;
};

// "Natural" is transparent; "Pop" is the audible-but-musical studio sound; "Hard" is the robotic effect.
const Preset kPresets[3] = {
    { "Natural",   55.0f, 100.0f, 85.0f },
    { "Pop",       18.0f, 100.0f, 50.0f },
    { "Hard-tune",  0.0f, 100.0f,  0.0f },
};

constexpr int kKnobX = 190, kKnobW = 111, kKnobTop = 396, kNumKnobs = 6;
} // namespace

HarmoniaEditor::HarmoniaEditor (HarmoniaProcessor& p)
    : AudioProcessorEditor (&p), proc (p), graph (p)
{
    setLookAndFeel (&laf);

    addAndMakeVisible (content);
    content.onPaint = [this] (juce::Graphics& g) { paintContent (g); };

    // ---- header ----
    setupChoice (rangeBox, rangeAtt, ids::range,
                 "Pick your voice type. It narrows the pitch search so tracking is faster and never jumps an octave.");

    bypassButton.setClickingTogglesState (true);
    bypassButton.getProperties().set ("warn", true);
    bypassButton.setTooltip ("Hear your vocal untouched (still delayed by the plug-in latency, so it stays in time).");
    content.addAndMakeVisible (bypassButton);
    bypassAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, ids::bypass, bypassButton);

    // ---- key card ----
    setupChoice (keyBox, keyAtt, ids::key, "The key (root note) of your song.");
    setupChoice (scaleBox, scaleAtt, ids::scale,
                 "The notes your vocal is allowed to land on. Chromatic = every semitone (fixes pitch only).");

    detectButton.setTooltip ("Click, sing or play a phrase of the song, then click again - Harmonia picks the key for you.");
    autoKeyButton.setClickingTogglesState (true);
    autoKeyButton.setTooltip ("Follow the key of the vocal by itself, and change it when the song changes key. "
                              "Works out the key from the singing, so give it a few seconds of vocal. Major/minor only.");
    content.addAndMakeVisible (autoKeyButton);
    autoKeyAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, ids::autoKey, autoKeyButton);
    detectButton.onClick = [this] { onDetectClicked(); };
    content.addAndMakeVisible (detectButton);

    detectedText = "Not sure of the key? Sing a phrase and let Harmonia find it.";
    statusLabel.setJustificationType (juce::Justification::topLeft);
    statusLabel.setColour (juce::Label::textColourId, theme::dim);
    statusLabel.setFont (theme::font (12.0f));
    content.addAndMakeVisible (statusLabel);

    keyboard.onToggle = [this] (int pc) { toggleNote (pc); };
    keyboard.setTooltip ("Click a key to allow or block that note. Lit keys are the notes your vocal can be tuned to.");
    content.addAndMakeVisible (keyboard);

    content.addAndMakeVisible (graph);

    // ---- style presets ----
    for (int i = 0; i < 3; ++i)
    {
        presetButtons[i].setButtonText (kPresets[i].name);
        presetButtons[i].onClick = [this, i] { applyPreset (i); };
        content.addAndMakeVisible (presetButtons[i]);
    }
    presetButtons[0].setTooltip ("Transparent correction. Keeps your vibrato and expression.");
    presetButtons[1].setTooltip ("Polished modern pop vocal.");
    presetButtons[2].setTooltip ("Instant snapping: the robotic T-Pain style effect.");

    // ---- knobs ----
    setupKnob (knobs[0], ids::speed, "SPEED",
               "How fast the voice is pulled onto the note. Instant = robotic effect. 20-60 ms is transparent, higher is looser.");
    setupKnob (knobs[1], ids::strength, "STRENGTH",
               "How much correction is applied. 100% = fully on the note. Lower keeps more of your own pitch.");
    setupKnob (knobs[2], ids::natural, "NATURAL",
               "Keeps your own vibrato and expression. 100% = all of it. 0% = flattened onto the note.");
    setupKnob (knobs[3], ids::hold, "NOTE HOLD",
               "How long the voice has to stay near a new note before Harmonia moves to it. Higher stops the correction flickering to a wrong neighbouring note. 0 = switch at once.");
    setupKnob (knobs[4], ids::formant, "FORMANT",
               "Changes the vocal character (throat size) without changing pitch. Leave at 0 for your natural voice.", true);
    setupKnob (knobs[5], ids::tuning, "TUNING (A4)",
               "Reference pitch for A4. Change it if the song is not tuned to 440 Hz.");

    setResizable (true, true);
    setResizeLimits (kDesignW * 3 / 4, kDesignH * 3 / 4, kDesignW * 2, kDesignH * 2);
    getConstrainer()->setFixedAspectRatio ((double) kDesignW / (double) kDesignH);
    setSize (kDesignW, kDesignH);

    subtitle = makeSubtitle();
    startTimerHz (20);
}

HarmoniaEditor::~HarmoniaEditor()
{
    setLookAndFeel (nullptr);
}

void HarmoniaEditor::setupChoice (juce::ComboBox& box, std::unique_ptr<ComboAttachment>& attachment, const char* id,
                                  const juce::String& tip)
{
    // The parameter owns the list of choices, so the box can never drift out of step with it.
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (id)))
        box.addItemList (choice->choices, 1);
    box.setTooltip (tip);
    content.addAndMakeVisible (box);
    attachment = std::make_unique<ComboAttachment> (proc.apvts, id, box);
}

void HarmoniaEditor::setupKnob (Knob& k, const char* id, const juce::String& title, const juce::String& tip, bool bipolar)
{
    k.title = title;
    content.addAndMakeVisible (k.slider); // join the hierarchy first so the text box picks up our look-and-feel
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 100, 22);
    k.slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    k.slider.setTooltip (tip);
    k.slider.getProperties().set ("bipolar", bipolar);
    k.slider.setPopupDisplayEnabled (false, false, nullptr);
    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, k.slider);

    if (auto* param = proc.apvts.getParameter (id))
        if (param->getLabel().isNotEmpty())
            k.slider.setTextValueSuffix (" " + param->getLabel());
}

void HarmoniaEditor::resized()
{
    content.setBounds (0, 0, kDesignW, kDesignH);
    content.setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) kDesignW));

    rangeBox.setBounds (580, 18, 190, 30);
    bypassButton.setBounds (784, 18, 76, 30);

    graph.setBounds (20, 72, 560, 296);

    keyBox.setBounds (616, 110, 82, 28);
    scaleBox.setBounds (706, 110, 138, 28);
    detectButton.setBounds (616, 148, 146, 32);
    autoKeyButton.setBounds (770, 148, 74, 32);
    statusLabel.setBounds (616, 186, 228, 34);
    keyboard.setBounds (616, 226, 228, 100);

    for (int i = 0; i < 3; ++i)
        presetButtons[i].setBounds (36, 424 + i * 40, 130, 32);

    for (int i = 0; i < kNumKnobs; ++i)
        knobs[i].slider.setBounds (kKnobX + i * kKnobW + 9, kKnobTop + 20, kKnobW - 18, 126);
}

void HarmoniaEditor::paintContent (juce::Graphics& g)
{
    g.fillAll (theme::bg);

    // header
    g.setColour (theme::text);
    g.setFont (theme::font (26.0f, true));
    g.drawText ("Harmonia", 20, 12, 200, 32, juce::Justification::centredLeft);
    g.setColour (theme::accent);
    g.fillEllipse (20.0f + 126.0f, 24.0f, 7.0f, 7.0f);
    g.setColour (theme::dim);
    g.setFont (theme::font (12.5f));
    g.drawText (subtitle, kSubtitleBounds, juce::Justification::centredLeft);
    g.setFont (theme::font (11.0f, true));
    g.setColour (theme::dim);
    g.drawText ("VOICE", 520, 18, 54, 30, juce::Justification::centredRight);

    auto card = [&] (juce::Rectangle<float> r) {
        g.setColour (theme::panel);
        g.fillRoundedRectangle (r, 10.0f);
        g.setColour (theme::panelEdge);
        g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);
    };
    auto caption = [&] (const juce::String& t, int x, int y) {
        g.setColour (theme::dim);
        g.setFont (theme::font (11.0f, true));
        g.drawText (t, x, y, 220, 16, juce::Justification::centredLeft);
    };

    card ({ 600.0f, 72.0f, 260.0f, 296.0f });
    caption ("KEY & SCALE", 616, 86);
    g.setColour (theme::dim);
    g.setFont (theme::font (11.0f));
    g.drawText (keysLocked ? "The key follows your vocal" : "Click keys to allow or block notes", 616, 332, 228, 28, juce::Justification::centredLeft);

    card ({ 20.0f, 384.0f, 840.0f, 160.0f });
    caption ("STYLE", 36, 400);
    g.setColour (theme::panelEdge);
    g.fillRect (180.0f, 400.0f, 1.0f, 128.0f);

    for (int i = 0; i < kNumKnobs; ++i)
    {
        g.setColour (theme::dim);
        g.setFont (theme::font (11.0f, true));
        g.drawText (knobs[i].title, kKnobX + i * kKnobW, kKnobTop, kKnobW, 16, juce::Justification::centred);
    }
}

// ---- parameter helpers ---------------------------------------------------------------

void HarmoniaEditor::setParam (const juce::String& id, float plainValue)
{
    if (auto* p = proc.apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (plainValue));
        p->endChangeGesture();
    }
}

void HarmoniaEditor::toggleNote (int pc)
{
    int root = 0;
    bool minor = false;
    if (proc.followedKey (root, minor)) // the key is being followed: the keys on screen are not editable
        return;
    if (proc.scaleIndex() != kCustomScale)
    {
        // Start from what is currently lit, then switch to Custom so edits stick.
        const uint16_t mask = proc.currentNoteMask();
        for (int i = 0; i < 12; ++i)
            setParam (ids::note (i), (mask & (1u << i)) != 0 ? 1.0f : 0.0f);
        setParam (ids::scale, (float) kCustomScale);
    }
    const bool now = proc.paramValue (ids::note (pc)) >= 0.5f;
    setParam (ids::note (pc), now ? 0.0f : 1.0f);
}

void HarmoniaEditor::applyPreset (int index)
{
    setParam (ids::speed, kPresets[index].speed);
    setParam (ids::strength, kPresets[index].strength);
    setParam (ids::natural, kPresets[index].natural);
}

void HarmoniaEditor::onDetectClicked()
{
    if (proc.learnState() == HarmoniaProcessor::LearnState::idle)
    {
        detectedText = "Listening starts now.";
        awaitingKeyResult = false;
        proc.startKeyLearn();
    }
    else
    {
        proc.finishKeyLearn();
        awaitingKeyResult = true;
        finishClickedAt = juce::Time::getMillisecondCounter();
    }
}

void HarmoniaEditor::timerCallback()
{
    int followRoot = 0;
    bool followMinor = false;
    const bool autoKey = proc.paramValue (ids::autoKey) >= 0.5f;
    const bool following = proc.followedKey (followRoot, followMinor);
    const bool usesRoot = scaleUsesRoot (proc.scaleIndex());

    keyboard.setState (proc.currentNoteMask(), following ? followRoot : (usesRoot ? (int) proc.paramValue (ids::key) : -1), graph.singingPitchClass());
    if (following != keysLocked)
    {
        keysLocked = following;
        content.repaint (616, 332, 228, 28);
    }
    keyBox.setEnabled (! autoKey && usesRoot);
    scaleBox.setEnabled (! autoKey);

    // key detection feedback
    HarmoniaProcessor::KeyResult result;
    if (proc.takeKeyResult (result))
    {
        setParam (ids::key, (float) result.root);
        setParam (ids::scale, (float) (result.minor ? kMinorScale : kMajorScale));
        detectedText = "Detected " + juce::String (kNoteNames[result.root]) + (result.minor ? " minor" : " major")
                       + "  (" + juce::String (juce::roundToInt (juce::jlimit (0.0f, 1.0f, result.score) * 100.0f)) + "% match)";
        awaitingKeyResult = false;
    }
    else if (awaitingKeyResult && juce::Time::getMillisecondCounter() - finishClickedAt > 700)
    {
        detectedText = "Not enough singing to tell. Sing a few notes, then try again.";
        awaitingKeyResult = false;
    }

    const bool listening = proc.learnState() == HarmoniaProcessor::LearnState::listening;
    detectButton.setToggleState (listening, juce::dontSendNotification);
    detectButton.setButtonText (listening ? "Stop & detect" : "Detect key");
    detectButton.setEnabled (! autoKey);
    if (listening)
    {
        const float seconds = proc.learnedAmount() / (float) (proc.hopRateHz() * 0.85);
        statusLabel.setText ("Listening... " + juce::String (seconds, 1) + " s of singing heard. Click again when done.", juce::dontSendNotification);
    }
    else if (autoKey)
        statusLabel.setText (following ? "Following the key: " + juce::String (kNoteNames[followRoot]) + (followMinor ? " minor" : " major")
                                       : juce::String ("Auto key: listening for the key. Play some vocal."),
                             juce::dontSendNotification);
    else
        statusLabel.setText (detectedText, juce::dontSendNotification);

    // highlight the matching style preset
    const float speed = proc.paramValue (ids::speed);
    const float strength = proc.paramValue (ids::strength);
    const float natural = proc.paramValue (ids::natural);
    for (int i = 0; i < 3; ++i)
    {
        const bool match = std::abs (speed - kPresets[i].speed) < 0.5f && std::abs (strength - kPresets[i].strength) < 0.5f
                           && std::abs (natural - kPresets[i].natural) < 0.5f;
        presetButtons[i].setToggleState (match, juce::dontSendNotification);
    }

    // The latency readout only changes when the host (re)prepares the plug-in.
    const auto text = makeSubtitle();
    if (text != subtitle)
    {
        subtitle = text;
        content.repaint (kSubtitleBounds);
    }
}

juce::String HarmoniaEditor::makeSubtitle() const
{
    juce::String text ("Natural pitch correction");
    if (proc.engineReady())
    {
        const int latencyMs = juce::roundToInt (1000.0 * proc.getLatencySamples() / juce::jmax (1.0, proc.getSampleRate()));
        text << "   |   latency " << latencyMs << " ms";
    }
    return text;
}
