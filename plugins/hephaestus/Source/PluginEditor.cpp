#include "PluginEditor.h"

using namespace hep;
using namespace hep::ui;

namespace
{
constexpr int editorWidth = 1040, editorHeight = 582;
constexpr int cellHeight = 98, captionHeight = 14;
constexpr int chipWidth = 34, chipHeight = 26, chipGap = 6;

// Everything that varies between the fixed panels, in one place.
struct PanelSpec
{
    const char* title;
    const char* subtitle;
    int columns;
    int firstColumn; // the first knob's column (a column before it is taken by a combo box)
    int knobTop;     // y offset of the first knob row inside the panel
};

const PanelSpec panelSpecs[] =
{
    { "BAND",      "",                                 6, 0, 110 },
    { "CHARACTER", "shared by all saturate bands",     2, 1, 26 },
    { "MASTER",    "",                                 3, 0, 26 },
};

// Knobs of the character and master panels: which panel, its parameter, caption and tooltip. They
// fill a panel left to right in table order.
struct KnobSpec
{
    int panel; // index into panelSpecs
    const char* paramId;
    const char* caption;
    const char* tooltip;
};

const KnobSpec knobSpecs[] =
{
    { 1, id::even, "EVEN", "0% = symmetric curve, odd harmonics only (3rd, 5th...). Higher = asymmetric, adds 2nd, 4th... (tube-like warmth)." },

    { 2, id::inGain,  "INPUT",  "Input gain. Drives the saturate bands harder without changing the dry reference." },
    { 2, id::mix,     "MIX",    "Parallel mix of processed and dry (dry is latency-compensated)." },
    { 2, id::outGain, "OUTPUT", "Output gain." },
};

// The knobs of the selected band, by mode. They share six slots that are re-attached to the band's
// parameters whenever the selection or the mode changes.
struct BandKnobSpec
{
    BandMode mode;
    juce::String BandIds::* field;
    const char* caption;
    const char* tooltip;
};

const BandKnobSpec bandKnobSpecs[] =
{
    { BandMode::saturate, &BandIds::lo,    "LOW",   "Lower edge of the band. Also set by dragging its edge in the display." },
    { BandMode::saturate, &BandIds::hi,    "HIGH",  "Upper edge of the band. Also set by dragging its edge in the display." },
    { BandMode::saturate, &BandIds::drive, "DRIVE", "How hard the band is driven into the curve. More drive = more harmonics. Small signals stay untouched." },

    { BandMode::fill, &BandIds::lo,      "LOW",     "Lowest fundamental to work on. Also set by dragging the band's edge in the display." },
    { BandMode::fill, &BandIds::hi,      "HIGH",    "Highest fundamental to work on (fill bands stop at 2 kHz)." },
    { BandMode::fill, &BandIds::amount,  "AMOUNT",  "How much harmonic energy the band should have above it, relative to its own energy. Off at the minimum." },
    { BandMode::fill, &BandIds::tone,    "TONE",    "Which harmonics carry the energy. Dark = mostly the 2nd. Middle = the usual roll-off. Bright = led by the 3rd, with 4th and 5th present." },
    { BandMode::fill, &BandIds::adapt,   "ADAPT",   "0% = always add AMOUNT. 100% = only add what is missing: the energy just above the band is compared with the band's own, and only the shortfall to AMOUNT is generated. Watch the FILL % on the band." },
    { BandMode::fill, &BandIds::rebuild, "REBUILD", "Also rebuilds a fundamental that is missing inside the band while its harmonics are present (difference tone, 3f - 2f = f). Off at the minimum. Experimental on dense material." },
};

const char* modeName (BandMode mode) { return mode == BandMode::fill ? "HARMONIC FILL" : "SATURATE"; }
}

HephaestusEditor::HephaestusEditor (HephaestusProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      spectrum (p.engine, p.apvts, p.currentSampleRate)
{
    setLookAndFeel (&look);
    addAndMakeVisible (spectrum);

    for (auto& spec : knobSpecs)
        addKnob (spec.paramId, spec.caption, spec.tooltip);

    for (auto& slot : bandKnobs)
    {
        slot = makeKnob ({}, {});
        addChildComponent (slot->slider);
        addChildComponent (slot->caption);
    }

    addChoiceBox (curveBox, curveAttachment, id::curve,
                  "Waveshaper curve for the saturate bands.\nTanh: smooth, tube / transformer-like.\nAtan: more open, tape-like.\n"
                  "Soft: algebraic knee, gritty, strongest upper odd harmonics.");

    // Band panel buttons.
    saturateModeButton.setButtonText ("SATURATE");
    fillModeButton.setButtonText ("HARMONIC FILL");
    removeButton.setButtonText ("REMOVE");
    addSaturateButton.setButtonText ("+ SATURATE BAND");
    addFillButton.setButtonText ("+ HARMONIC FILL BAND");
    saturateModeButton.setColour (juce::TextButton::buttonOnColourId, colour::forMode (BandMode::saturate));
    fillModeButton.setColour (juce::TextButton::buttonOnColourId, colour::forMode (BandMode::fill));
    addFillButton.setColour (juce::TextButton::buttonOnColourId, colour::forMode (BandMode::fill));
    saturateModeButton.setTooltip ("Add harmonics by driving this band into a waveshaper (uses Curve and Even).");
    fillModeButton.setTooltip ("Add harmonics above this band, only as much as is missing (see ADAPT).");
    removeButton.setTooltip ("Free this band slot.");
    saturateModeButton.onClick = [this] { setBandMode (proc.apvts, spectrum.getSelectedBand(), BandMode::saturate); };
    fillModeButton.onClick = [this] { setBandMode (proc.apvts, spectrum.getSelectedBand(), BandMode::fill); };
    removeButton.onClick = [this] { removeBand (proc.apvts, spectrum.getSelectedBand()); };
    addSaturateButton.onClick = [this] { addBand (proc.apvts, BandMode::saturate, 800.0, spectrum.getSelectedBand()); };
    addFillButton.onClick = [this] { addBand (proc.apvts, BandMode::fill, 80.0, spectrum.getSelectedBand()); };
    for (auto* b : { &saturateModeButton, &fillModeButton, &removeButton, &addSaturateButton, &addFillButton })
        addChildComponent (b);

    // Header.
    for (int i = 0; i < (int) presets().size(); ++i)
        presetBox.addItem (presets()[(size_t) i].name, i + 1);
    presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification);
    presetBox.onChange = [this] { proc.setCurrentProgram (presetBox.getSelectedId() - 1); };
    presetBox.setTooltip ("Presets. Global settings (gain, mix, stereo mode, oversampling) are kept.");
    addAndMakeVisible (presetBox);

    addChoiceBox (stereoBox, stereoAttachment, id::stereo,
                  "Mid/Side saturates the centre and sides together; harmonic fill bands then work on the mid channel only, keeping bass centred.");
    addChoiceBox (osBox, osAttachment, id::oversampling,
                  "Oversampling for the saturate bands (linear phase). Reduces aliasing; adds latency, which is reported to the host.");
    addToggle (deltaButton, deltaAttachment, id::delta, "DELTA",
               "Listen to only what the plugin adds (wet minus dry). Use it to hear exactly what the harmonics are doing.");
    addToggle (matchButton, matchAttachment, id::match, "GAIN MATCH",
               "Matches the processed loudness to the dry signal so 'louder' doesn't sound 'better'.");

    // The first band that exists is the one to show.
    for (int i = 0; i < maxBands; ++i)
        if (isBandOn (proc.apvts, i))
        {
            spectrum.setSelectedBand (i);
            break;
        }
    refreshBandPanel();

    // Size last: resized() lays out the controls, which must all exist by then.
    setSize (editorWidth, editorHeight);
    startTimerHz (30);
}

HephaestusEditor::~HephaestusEditor()
{
    setLookAndFeel (nullptr);
}

std::unique_ptr<HephaestusEditor::Knob> HephaestusEditor::makeKnob (const juce::String& caption, const juce::String& tooltip)
{
    auto knob = std::make_unique<Knob>();
    knob->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 78, 18);
    knob->slider.setTooltip (tooltip);
    knob->slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    knob->slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    knob->slider.setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
    knob->caption.setText (caption, juce::dontSendNotification);
    knob->caption.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    knob->caption.setColour (juce::Label::textColourId, colour::dim);
    knob->caption.setJustificationType (juce::Justification::centred);
    return knob;
}

void HephaestusEditor::addKnob (const char* paramId, const char* caption, const char* tooltip)
{
    auto knob = makeKnob (caption, tooltip);
    knob->attachment = std::make_unique<SliderAttachment> (proc.apvts, paramId, knob->slider);
    addAndMakeVisible (knob->slider);
    addAndMakeVisible (knob->caption);
    knobs.push_back (std::move (knob));
}

void HephaestusEditor::addToggle (juce::TextButton& b, std::unique_ptr<ButtonAttachment>& a, const char* paramId,
                                  const juce::String& text, const juce::String& tooltip)
{
    b.setButtonText (text);
    b.setClickingTogglesState (true);
    b.setTooltip (tooltip);
    a = std::make_unique<ButtonAttachment> (proc.apvts, paramId, b);
    addAndMakeVisible (b);
}

// The items come from the parameter itself, so the choices are defined once (in Parameters.h).
void HephaestusEditor::addChoiceBox (juce::ComboBox& box, std::unique_ptr<ComboAttachment>& attachment,
                                     const char* paramId, const juce::String& tooltip)
{
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (paramId)))
        box.addItemList (choice->choices, 1);

    box.setTooltip (tooltip);
    attachment = std::make_unique<ComboAttachment> (proc.apvts, paramId, box);
    addAndMakeVisible (box);
}

// What changes the band panel: which band, whether it exists, and its mode.
int HephaestusEditor::bandSignature() const
{
    int signature = spectrum.getSelectedBand();
    for (int i = 0; i < maxBands; ++i)
        signature = signature * 3 + (isBandOn (proc.apvts, i) ? 1 + (int) bandMode (proc.apvts, i) : 0);
    return signature;
}

// Points the six knob slots and the buttons at the selected band.
void HephaestusEditor::refreshBandPanel()
{
    shownBandSignature = bandSignature();
    const int band = spectrum.getSelectedBand();
    const bool on = isBandOn (proc.apvts, band);
    const auto mode = bandMode (proc.apvts, band);

    bandKnobCount = 0;
    for (auto& spec : bandKnobSpecs)
    {
        if (! on || spec.mode != mode)
            continue;

        auto& knob = *bandKnobs[(size_t) bandKnobCount++];
        knob.attachment.reset();
        knob.caption.setText (spec.caption, juce::dontSendNotification);
        knob.slider.setTooltip (spec.tooltip);
        knob.attachment = std::make_unique<SliderAttachment> (proc.apvts, bandIds (band).*spec.field, knob.slider);
    }

    for (int k = 0; k < maxBands; ++k)
    {
        auto& knob = *bandKnobs[(size_t) k];
        if (k >= bandKnobCount)
            knob.attachment.reset();
        knob.slider.setVisible (k < bandKnobCount);
        knob.caption.setVisible (k < bandKnobCount);
    }

    saturateModeButton.setToggleState (mode == BandMode::saturate, juce::dontSendNotification);
    fillModeButton.setToggleState (mode == BandMode::fill, juce::dontSendNotification);
    for (auto* b : { &saturateModeButton, &fillModeButton, &removeButton })
        b->setVisible (on);
    for (auto* b : { &addSaturateButton, &addFillButton })
        b->setVisible (! on);
}

juce::String HephaestusEditor::bandHint() const
{
    const int band = spectrum.getSelectedBand();
    if (! isBandOn (proc.apvts, band))
        return "Slot " + juce::String (band + 1) + " is free. Add a band here, or double-click the display where you want one.";

    if (bandMode (proc.apvts, band) == BandMode::saturate)
        return "Drives the sound inside the band into the curve, so harmonics appear above it (the shaded zone, 2nd to 5th harmonic). "
               "Anything outside the band is left exactly as it is.";

    return "Adds harmonics above the band (the shaded zone, 2nd to 5th harmonic), but only as many as are missing: the energy just above the band is "
           "measured against the band's own, and only the shortfall to AMOUNT is generated. FILL % on the band shows how much is being added.";
}

void HephaestusEditor::layoutBandPanel()
{
    const auto panel = panelBounds[(size_t) Panel::band];

    for (int i = 0; i < maxBands; ++i)
        chipBounds[(size_t) i] = { panel.getX() + 14 + i * (chipWidth + chipGap), panel.getY() + 34, chipWidth, chipHeight };

    auto buttons = juce::Rectangle<int> (panel.getX(), panel.getY() + 34, panel.getWidth() - 14, chipHeight);
    removeButton.setBounds (buttons.removeFromRight (76));
    buttons.removeFromRight (8);
    fillModeButton.setBounds (buttons.removeFromRight (124));
    buttons.removeFromRight (6);
    saturateModeButton.setBounds (buttons.removeFromRight (90));

    // A free slot offers the two ways to fill it.
    auto offer = juce::Rectangle<int> (panel.getX() + 14, panel.getY() + 118, panel.getWidth() - 28, 44);
    addSaturateButton.setBounds (offer.removeFromLeft (offer.getWidth() / 2 - 6));
    offer.removeFromLeft (12);
    addFillButton.setBounds (offer);

    const auto& spec = panelSpecs[(size_t) Panel::band];
    const auto area = panel.reduced (8, 0).withTrimmedTop (spec.knobTop);
    const int cellW = area.getWidth() / spec.columns;
    for (int k = 0; k < bandKnobCount; ++k)
    {
        auto cell = juce::Rectangle<int> (area.getX() + k * cellW, area.getY(), cellW, cellHeight);
        bandKnobs[(size_t) k]->caption.setBounds (cell.removeFromTop (captionHeight));
        bandKnobs[(size_t) k]->slider.setBounds (cell);
    }
}

void HephaestusEditor::layoutPanelKnobs (Panel which)
{
    const auto& spec = panelSpecs[(size_t) which];
    const auto area = panelBounds[(size_t) which].reduced (8, 0).withTrimmedTop (spec.knobTop);
    const int cellW = area.getWidth() / spec.columns;

    int index = spec.firstColumn;
    for (size_t k = 0; k < knobs.size(); ++k)
    {
        if (knobSpecs[k].panel != (int) which)
            continue;

        auto cell = juce::Rectangle<int> (area.getX() + (index % spec.columns) * cellW,
                                          area.getY() + (index / spec.columns) * cellHeight, cellW, cellHeight);
        knobs[k]->caption.setBounds (cell.removeFromTop (captionHeight));
        knobs[k]->slider.setBounds (cell);
        ++index;
    }
}

void HephaestusEditor::resized()
{
    auto bounds = getLocalBounds().reduced (16, 0);

    // Header.
    auto header = bounds.removeFromTop (56);
    {
        auto h = header.withTrimmedLeft (300).withTrimmedTop (22).withTrimmedBottom (8);
        for (auto [component, width, gap] : { std::tuple<juce::Component*, int, int> { &presetBox, 170, 12 },
                                              { &stereoBox, 120, 12 }, { &osBox, 76, 16 },
                                              { &deltaButton, 64, 8 }, { &matchButton, 96, 0 } })
        {
            component->setBounds (h.removeFromLeft (width));
            h.removeFromLeft (gap);
        }
        meterArea = header.removeFromRight (96).withTrimmedTop (14).withTrimmedBottom (14);
    }

    bounds.removeFromTop (6);
    spectrum.setBounds (bounds.removeFromTop (224));
    bounds.removeFromTop (10);

    // Panels: the selected band | character over master.
    auto row = bounds.removeFromTop (268);
    panelBounds[(size_t) Panel::band] = row.removeFromLeft (690);
    row.removeFromLeft (12);
    panelBounds[(size_t) Panel::character] = row.removeFromTop (130);
    row.removeFromTop (8);
    panelBounds[(size_t) Panel::master] = row.removeFromTop (130);

    // Character panel: the curve sits in the first column, ahead of the EVEN knob.
    {
        const auto& spec = panelSpecs[(size_t) Panel::character];
        const auto area = panelBounds[(size_t) Panel::character].reduced (8, 0).withTrimmedTop (spec.knobTop);
        curveBox.setBounds (area.getX() + 10, area.getY() + 26, area.getWidth() / spec.columns - 20, 28);
    }

    layoutBandPanel();
    layoutPanelKnobs (Panel::character);
    layoutPanelKnobs (Panel::master);
}

void HephaestusEditor::selectBand (int band)
{
    spectrum.setSelectedBand (band);
    refreshBandPanel();
    layoutBandPanel();
    repaint();
}

void HephaestusEditor::mouseDown (const juce::MouseEvent& e)
{
    for (int i = 0; i < maxBands; ++i)
        if (chipBounds[(size_t) i].contains (e.getPosition()))
            selectBand (i);
}

void HephaestusEditor::paint (juce::Graphics& g)
{
    using namespace colour;

    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1a1410), 0.0f, 0.0f, background, 0.0f, (float) getHeight(), false));
    g.fillAll();

    // Title block.
    g.setColour (ember);
    g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
    g.drawText ("HEPHAESTUS", 20, 8, 280, 32, juce::Justification::left);
    g.setColour (dim);
    g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    g.drawText ("HARMONIC FORGE  -  MASTERING SATURATOR", 22, 38, 280, 14, juce::Justification::left);

    // Small captions above the header combos.
    g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
    for (auto [box, caption] : { std::pair<juce::Component*, const char*> { &presetBox, "PRESET" },
                              { &stereoBox, "STEREO MODE" }, { &osBox, "OVERSAMPLING" } })
        g.drawText (caption, box->getX() + 2, 8, 100, 12, juce::Justification::left);

    // Panels.
    const int band = spectrum.getSelectedBand();
    const bool bandOn = isBandOn (proc.apvts, band);
    const auto mode = bandMode (proc.apvts, band);

    for (size_t i = 0; i < panelBounds.size(); ++i)
    {
        const auto r = panelBounds[i];

        g.setColour (colour::panel);
        g.fillRoundedRectangle (r.toFloat(), 8.0f);
        g.setColour (panelEdge);
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 8.0f, 1.0f);

        juce::String title = panelSpecs[i].title;
        if (i == (size_t) Panel::band)
            title << " " << (band + 1) << (bandOn ? juce::String ("  -  ") + modeName (mode) : "  -  FREE");

        g.setColour (i == (size_t) Panel::band && bandOn ? forMode (mode) : copper);
        g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
        g.drawText (title, r.getX() + 14, r.getY() + 9, 300, 14, juce::Justification::left);
        g.setColour (dim);
        g.setFont (juce::FontOptions (10.0f));
        g.drawText (panelSpecs[i].subtitle, r.getX() + 14, r.getY() + 9, r.getWidth() - 28, 14, juce::Justification::right);
    }

    g.setColour (dim);
    g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    g.drawText ("CURVE", curveBox.getX(), curveBox.getY() - 16, curveBox.getWidth(), 14, juce::Justification::centred);

    // Band chips: one per slot. Lit in the band's colour when it exists.
    for (int i = 0; i < maxBands; ++i)
    {
        const auto r = chipBounds[(size_t) i].toFloat();
        const bool on = isBandOn (proc.apvts, i);
        const auto c = forMode (bandMode (proc.apvts, i));

        g.setColour (on ? c.withAlpha (i == band ? 0.95f : 0.5f) : colour::panel.brighter (0.05f));
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (i == band ? text : (on ? c.withAlpha (0.7f) : panelEdge));
        g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, i == band ? 1.5f : 1.0f);
        g.setColour (on ? juce::Colour (0xff1a0d05) : dim);
        g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
        g.drawText (juce::String (i + 1), r, juce::Justification::centred, false);
    }

    // What the selected band does.
    {
        const auto bandPanel = panelBounds[(size_t) Panel::band];
        g.setColour (dim);
        g.setFont (juce::FontOptions (11.5f));
        g.drawFittedText (bandHint(), bandPanel.getX() + 14, bandPanel.getY() + 68, bandPanel.getWidth() - 28, 36,
                          juce::Justification::topLeft, 2);
    }

    // Meters.
    const auto drawMeter = [&] (juce::Rectangle<int> r, float level, const juce::String& name)
    {
        g.setColour (juce::Colour (0xff120f0d));
        g.fillRoundedRectangle (r.toFloat(), 3.0f);

        const float db = juce::Decibels::gainToDecibels (level, -60.0f);
        const float fill = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
        g.setColour (db > -0.5f ? juce::Colours::red : (db > -6.0f ? glow : ember));
        g.fillRoundedRectangle (r.toFloat().withWidth ((float) r.getWidth() * fill), 3.0f);
        g.setColour (dim);
        g.setFont (juce::FontOptions (8.5f, juce::Font::bold));
        g.drawText (name, r.getX() - 22, r.getY() - 2, 20, r.getHeight() + 4, juce::Justification::centredRight);
    };
    auto m = meterArea;
    drawMeter (m.removeFromTop (m.getHeight() / 2 - 2), inLevel, "IN");
    m.removeFromTop (4);
    drawMeter (m, outLevel, "OUT");
}

void HephaestusEditor::timerCallback()
{
    inLevel = proc.engine.inPeak.load (std::memory_order_relaxed);
    outLevel = proc.engine.outPeak.load (std::memory_order_relaxed);
    repaint (meterArea.expanded (24, 4));

    if (bandSignature() != shownBandSignature)
    {
        refreshBandPanel();
        layoutBandPanel();
        repaint (panelBounds[(size_t) Panel::band]);
    }

    if (const int prog = proc.getCurrentProgram(); prog != lastProgram)
    {
        lastProgram = prog;
        presetBox.setSelectedId (prog + 1, juce::dontSendNotification);
    }
}
