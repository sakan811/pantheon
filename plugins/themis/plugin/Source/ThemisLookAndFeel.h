#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace colours
{
inline const juce::Colour background { 0xff12141a };
inline const juce::Colour panel { 0xff1b1e26 };
inline const juce::Colour panelEdge { 0xff2a2f3a };
inline const juce::Colour text { 0xffe5e7eb };
inline const juce::Colour dim { 0xff8b93a1 };
inline const juce::Colour accent { 0xff2dd4bf };
inline const juce::Colour good { 0xff4ade80 };
inline const juce::Colour warn { 0xfff59e0b };
inline const juce::Colour bad { 0xffef4444 };
} // namespace colours

class ThemisLookAndFeel : public juce::LookAndFeel_V4
{
public:
    ThemisLookAndFeel()
    {
        setColour (juce::ResizableWindow::backgroundColourId, colours::background);
        setColour (juce::Slider::textBoxTextColourId, colours::text);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Label::textColourId, colours::text);
        setColour (juce::ComboBox::backgroundColourId, colours::panelEdge);
        setColour (juce::ComboBox::textColourId, colours::text);
        setColour (juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
        setColour (juce::ComboBox::arrowColourId, colours::dim);
        setColour (juce::PopupMenu::backgroundColourId, colours::panel);
        setColour (juce::PopupMenu::textColourId, colours::text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::accent.darker (0.4f));
        setColour (juce::TextButton::buttonColourId, colours::panelEdge);
        setColour (juce::TextButton::buttonOnColourId, colours::accent.darker (0.25f));
        setColour (juce::TextButton::textColourOffId, colours::text);
        setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool highlighted, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        auto c = b.getToggleState() ? findColour (juce::TextButton::buttonOnColourId) : findColour (juce::TextButton::buttonColourId);
        if (highlighted)
            c = c.brighter (0.12f);
        if (down)
            c = c.brighter (0.2f);
        g.setColour (c);
        g.fillRoundedRectangle (r, 4.0f);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override
    {
        return juce::Font (juce::FontOptions (juce::jmin (13.0f, static_cast<float> (buttonHeight) * 0.6f)));
    }
};
