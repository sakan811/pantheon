#include "PluginEditor.h"

using namespace hep;
using namespace hep::ui;

namespace
{
constexpr int editorWidth = 1040, editorHeight = 540;
constexpr int cellHeight = 98, captionHeight = 14;

enum class PanelId { saturation, bass, restore, master };

// Everything that varies between panels, in one place.
struct PanelSpec
{
    const char* title;
    const char* subtitle;
    int columns;
    int knobTop; // y offset of the first knob row inside the panel
};

const PanelSpec panelSpecs[] =
{
    { "SATURATION",          "3-band  -  ADAA  -  oversampled",                      3, 64 },
    { "HARMONIC BASS",       "missing fundamental  -  H2..H5 in dB re. fundamental", 4, 34 },
    { "FUNDAMENTAL RESTORE", "difference tone",                                      3, 26 },
    { "MASTER",              "",                                                     3, 26 },
};

// Every knob: which panel it sits in (knobs fill a panel left to right, top to bottom, in table
// order), its parameter, caption and tooltip.
struct KnobSpec
{
    PanelId panel;
    const char* paramId;
    const char* caption;
    const char* tooltip;
};

const KnobSpec knobSpecs[] =
{
    { PanelId::saturation, id::driveLow,  "LOW DRIVE",   "Drive into the saturator below the low crossover. More drive = more harmonics." },
    { PanelId::saturation, id::driveMid,  "MID DRIVE",   "Drive into the saturator between the crossovers." },
    { PanelId::saturation, id::driveHigh, "HIGH DRIVE",  "Drive into the saturator above the high crossover. Acts as an exciter: harmonics land above the band." },
    { PanelId::saturation, id::even,      "EVEN",        "0% = symmetric curve, odd harmonics only (3rd, 5th...). Higher = asymmetric, adds 2nd, 4th... (tube-like warmth)." },
    { PanelId::saturation, id::xoverLow,  "LOW X-OVER",  "Crossover between low and mid bands (Linkwitz-Riley 24 dB/oct)." },
    { PanelId::saturation, id::xoverHigh, "HIGH X-OVER", "Crossover between mid and high bands (Linkwitz-Riley 24 dB/oct)." },

    { PanelId::bass, id::extAmount, "AMOUNT",     "HARMONIC BASS: level of generated harmonics (the missing-fundamental effect), relative to the bass band. Off at the minimum." },
    { PanelId::bass, id::fundLo,    "RANGE LOW",  "Lowest fundamental to work on. The orange band in the display shows the range." },
    { PanelId::bass, id::fundHi,    "RANGE HIGH", "Highest fundamental to work on." },
    { PanelId::bass, id::ceiling,   "CEILING",    "Low-pass on the generated harmonics." },
    { PanelId::bass, id::h2, "H2", "2nd harmonic level relative to the fundamental. Even harmonics are the most consonant." },
    { PanelId::bass, id::h3, "H3", "3rd harmonic level relative to the fundamental. Together with H2 this carries most of the missing-fundamental cue." },
    { PanelId::bass, id::h4, "H4", "4th harmonic level relative to the fundamental. Adds definition." },
    { PanelId::bass, id::h5, "H5", "5th harmonic level relative to the fundamental. Adds bite; keep low." },

    { PanelId::restore, id::resAmount, "AMOUNT",    "FUNDAMENTAL RESTORE: regenerates a fundamental that is missing while its harmonics are present, from their difference tone (3f - 2f = f)." },
    { PanelId::restore, id::resFill,   "FILL GAPS", "How strongly an existing fundamental ducks the restored one. 100% = only fills what is missing; 0% = always adds." },

    { PanelId::master, id::inGain,  "INPUT",  "Input gain. Drives the saturator harder without changing the dry reference." },
    { PanelId::master, id::mix,     "MIX",    "Parallel mix of processed and dry (dry is latency-compensated)." },
    { PanelId::master, id::outGain, "OUTPUT", "Output gain." },
};
}

HephaestusEditor::HephaestusEditor (HephaestusProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      spectrum (p.engine, p.apvts, p.currentSampleRate)
{
    setLookAndFeel (&look);
    addAndMakeVisible (spectrum);

    for (auto& spec : knobSpecs)
        addKnob (spec.paramId, spec.caption, spec.tooltip);

    addToggle (satButton, satAttachment, id::satOn, "ON", "Enable the three-band saturator.");
    addChoiceBox (curveBox, curveAttachment, id::curve,
                  "Waveshaper curve.\nTanh: smooth, tube / transformer-like.\nAtan: more open, tape-like.\n"
                  "Soft: algebraic knee, gritty, strongest upper odd harmonics.");

    // Header.
    for (int i = 0; i < (int) presets().size(); ++i)
        presetBox.addItem (presets()[(size_t) i].name, i + 1);
    presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification);
    presetBox.onChange = [this] { proc.setCurrentProgram (presetBox.getSelectedId() - 1); };
    presetBox.setTooltip ("Presets. Global settings (gain, mix, stereo mode, oversampling) are kept.");
    addAndMakeVisible (presetBox);

    addChoiceBox (stereoBox, stereoAttachment, id::stereo,
                  "Mid/Side saturates the centre and sides together; the bass engine then works on the mid channel only, keeping bass centred.");
    addChoiceBox (osBox, osAttachment, id::oversampling,
                  "Oversampling for the saturator (linear phase). Reduces aliasing; adds latency, which is reported to the host.");
    addToggle (deltaButton, deltaAttachment, id::delta, "DELTA",
               "Listen to only what the plugin adds (wet minus dry). Use it to hear exactly what the harmonics are doing.");
    addToggle (matchButton, matchAttachment, id::match, "GAIN MATCH",
               "Matches the processed loudness to the dry signal so 'louder' doesn't sound 'better'.");

    // Size last: resized() lays out the controls, which must all exist by then.
    setSize (editorWidth, editorHeight);
    startTimerHz (30);
}

HephaestusEditor::~HephaestusEditor()
{
    setLookAndFeel (nullptr);
}

void HephaestusEditor::addKnob (const char* paramId, const char* caption, const char* tooltip)
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

void HephaestusEditor::layoutPanelKnobs (Panel which)
{
    const auto panelId = (PanelId) which;
    const auto& spec = panelSpecs[(size_t) which];
    const auto area = panelBounds[(size_t) which].reduced (8, 0).withTrimmedTop (spec.knobTop);
    const int cellW = area.getWidth() / spec.columns;

    int index = 0;
    for (size_t k = 0; k < knobs.size(); ++k)
    {
        if (knobSpecs[k].panel != panelId)
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
    spectrum.setBounds (bounds.removeFromTop (190));
    bounds.removeFromTop (10);

    // Panels: saturation | bass | (restore over master).
    auto row = bounds.removeFromTop (266);
    panelBounds[(size_t) Panel::saturation] = row.removeFromLeft (310);
    row.removeFromLeft (12);
    panelBounds[(size_t) Panel::bass] = row.removeFromLeft (400);
    row.removeFromLeft (12);
    panelBounds[(size_t) Panel::restore] = row.removeFromTop (126);
    row.removeFromTop (14);
    panelBounds[(size_t) Panel::master] = row.removeFromTop (126);

    // Saturation panel: switch and curve above the knobs.
    auto top = panelBounds[(size_t) Panel::saturation].reduced (8, 0).withTrimmedTop (30).removeFromTop (28);
    satButton.setBounds (top.removeFromLeft (56));
    top.removeFromLeft (8);
    curveBox.setBounds (top.removeFromLeft (90));

    for (auto which : { Panel::saturation, Panel::bass, Panel::restore, Panel::master })
        layoutPanelKnobs (which);
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
    for (size_t i = 0; i < panelBounds.size(); ++i)
    {
        const auto r = panelBounds[i];

        g.setColour (colour::panel);
        g.fillRoundedRectangle (r.toFloat(), 8.0f);
        g.setColour (panelEdge);
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 8.0f, 1.0f);

        g.setColour (copper);
        g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
        g.drawText (panelSpecs[i].title, r.getX() + 14, r.getY() + 9, 200, 14, juce::Justification::left);
        g.setColour (dim);
        g.setFont (juce::FontOptions (10.0f));
        g.drawText (panelSpecs[i].subtitle, r.getX() + 14, r.getY() + 9, r.getWidth() - 28, 14, juce::Justification::right);
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

    if (const int prog = proc.getCurrentProgram(); prog != lastProgram)
    {
        lastProgram = prog;
        presetBox.setSelectedId (prog + 1, juce::dontSendNotification);
    }
}
