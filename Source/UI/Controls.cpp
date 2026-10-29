//
//  Controls.cpp
//  Kitbox
//

#include "Controls.h"
#include "Knob.h"
#include "Palette.h"
#include "TextUtf8.h"

namespace
{
    void drawChevron (juce::Graphics& g, juce::Point<float> centre, float size, juce::Colour colour)
    {
        juce::Path chevron;
        chevron.startNewSubPath (centre.x - size * 0.5f, centre.y - size * 0.25f);
        chevron.lineTo (centre.x, centre.y + size * 0.25f);
        chevron.lineTo (centre.x + size * 0.5f, centre.y - size * 0.25f);
        g.setColour (colour);
        g.strokePath (chevron, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

//==============================================================================
ChoiceBox::ChoiceBox (const juce::String& labelText)
    : label (labelText.toUpperCase())
{
    setRepaintsOnMouseActivity (true);
}

ChoiceBox::~ChoiceBox()
{
    attachment.reset();
}

void ChoiceBox::attach (juce::AudioProcessorValueTreeState& state, const juce::String& parameterId)
{
    attachment.reset();
    parameter = state.getParameter (parameterId);

    if (parameter == nullptr)
        return;

    attachment = std::make_unique<juce::ParameterAttachment> (*parameter, [this] (float value)
    {
        current = juce::roundToInt (value);
        repaint();
    });

    attachment->sendInitialUpdate();
}

int ChoiceBox::count() const
{
    if (parameter == nullptr)
        return 0;

    const auto& range = parameter->getNormalisableRange();
    return juce::roundToInt (range.end - range.start) + 1;
}

juce::String ChoiceBox::textFor (int value) const
{
    if (textForValue)
        return textForValue (value);

    if (parameter == nullptr)
        return {};

    return parameter->getText (parameter->convertTo0to1 ((float) value), 32);
}

void ChoiceBox::set (int value)
{
    if (attachment == nullptr || parameter == nullptr)
        return;

    const auto& range = parameter->getNormalisableRange();
    attachment->setValueAsCompleteGesture (juce::jlimit (range.start, range.end, (float) value));
}

juce::Rectangle<float> ChoiceBox::boxBounds() const
{
    // Under the name's 16-point row and 6, in the 52-point row a knob's dial
    // takes: at most 116 wide, 32 high.
    const auto row = getLocalBounds().toFloat().withTrimmedTop (16.0f + 6.0f).removeFromTop (52.0f);
    return row.withSizeKeepingCentre (juce::jmin (row.getWidth(), 116.0f), 32.0f);
}

void ChoiceBox::paint (juce::Graphics& g)
{
    const auto box = boxBounds();

    g.setColour (isMouseOver() ? Palette::field.brighter (0.05f) : Palette::field);
    g.fillRoundedRectangle (box, 7.0f);
    g.setColour (zone == Palette::ZoneId::none ? Palette::fieldLine : Palette::zone (zone, rack).border);
    g.drawRoundedRectangle (box.reduced (0.5f), 7.0f, 1.0f);

    auto inner = box.reduced (10.0f, 0.0f);
    drawChevron (g, inner.removeFromRight (10.0f).getCentre(), 5.0f, Palette::inkDim);
    inner.removeFromRight (6.0f);

    g.setColour (Palette::title);
    g.setFont (Palette::font (12.0f));
    g.drawFittedText (textFor (current), inner.toNearestInt(), juce::Justification::centredLeft, 1, 0.85f);

    g.setColour (Palette::inkDim);
    g.setFont (Palette::label (12.0f));
    g.drawFittedText (label, getLocalBounds().removeFromTop (16), juce::Justification::centred, 1, 0.8f);
}

void ChoiceBox::mouseDown (const juce::MouseEvent& event)
{
    if (parameter == nullptr || ! boxBounds().contains (event.position))
        return;

    const auto& range = parameter->getNormalisableRange();
    const auto first = juce::roundToInt (range.start);
    const auto items = count();
    juce::PopupMenu menu;

    if (items > 24)
    {
        // The notes: one submenu per octave, so 128 of them stay a short list.
        for (int octaveStart = first; octaveStart < first + items; octaveStart += 12)
        {
            juce::PopupMenu octave;
            bool holdsCurrent = false;

            for (int v = octaveStart; v < juce::jmin (octaveStart + 12, first + items); ++v)
            {
                octave.addItem (v - first + 1, textFor (v), true, v == current);
                holdsCurrent = holdsCurrent || v == current;
            }

            menu.addSubMenu (textFor (octaveStart) + utf8 (" \xe2\x80\x93 ")
                                 + textFor (juce::jmin (octaveStart + 11, first + items - 1)),
                             octave, true, nullptr, holdsCurrent);
        }
    }
    else
    {
        for (int v = first; v < first + items; ++v)
            menu.addItem (v - first + 1, textFor (v), true, v == current);
    }

    // Opened from the box, not the whole control: the name under it would
    // push the list a name's height away from the box.
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                                                  .withTargetScreenArea (localAreaToGlobal (boxBounds().toNearestInt()))
                                                  .withMinimumWidth ((int) boxBounds().getWidth()),
                        [safe = juce::Component::SafePointer<ChoiceBox> (this), first] (int result)
    {
        if (safe != nullptr && result > 0)
            safe->set (first + result - 1);
    });
}

void ChoiceBox::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    // A trackpad sends many small steps: one setting per notch's worth.
    wheelAccumulator += (wheel.isReversed ? -wheel.deltaY : wheel.deltaY);

    if (std::abs (wheelAccumulator) < 0.12f)
        return;

    set (current + (wheelAccumulator > 0.0f ? -1 : 1));
    wheelAccumulator = 0.0f;
}

//==============================================================================
PanelButton::PanelButton (const juce::String& text, const juce::String& iconPath, const juce::String& iconDots, Style styleToUse)
    : juce::Button (text), style (styleToUse)
{
    setIcon (iconPath, iconDots);
}

void PanelButton::setIcon (const juce::String& iconPath, const juce::String& iconDots)
{
    stroke = iconPath.isNotEmpty() ? juce::Drawable::parseSVGPath (iconPath) : juce::Path();
    fill   = iconDots.isNotEmpty() ? juce::Drawable::parseSVGPath (iconDots) : juce::Path();
    repaint();
}

void PanelButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    const auto lit = active || down;

    g.setColour (highlighted || down ? Palette::button.brighter (0.06f) : Palette::button);
    g.fillRoundedRectangle (bounds, 8.0f);
    g.setColour (lit ? Palette::accent : Palette::buttonLine);
    g.drawRoundedRectangle (bounds, 8.0f, 1.0f);

    const auto ink = lit ? Palette::accent : Palette::icon;

    if (style == Style::field)
    {
        auto inner = bounds.reduced (14.0f, 0.0f).withTrimmedRight (-2.0f);
        drawChevron (g, inner.removeFromRight (12.0f).getCentre(), 6.0f, Palette::inkDim);
        inner.removeFromRight (8.0f);

        g.setColour (Palette::title);
        g.setFont (Palette::font (14.0f, true));
        g.drawFittedText (getButtonText(), inner.toNearestInt(), juce::Justification::centredLeft, 1, 0.9f);
        return;
    }

    const auto hasText = getButtonText().isNotEmpty();
    const auto font = Palette::label (12.0f);
    const auto textWidth = hasText ? juce::GlyphArrangement::getStringWidth (font, getButtonText().toUpperCase()) : 0.0f;
    const auto hasIcon = ! stroke.isEmpty() || ! fill.isEmpty();
    const auto contentWidth = (hasIcon ? 16.0f : 0.0f) + (hasIcon && hasText ? 8.0f : 0.0f) + textWidth;
    auto content = bounds.withSizeKeepingCentre (contentWidth, bounds.getHeight());

    if (hasIcon)
    {
        const auto iconArea = content.removeFromLeft (16.0f).withSizeKeepingCentre (16.0f, 16.0f);
        const auto place = juce::AffineTransform::translation (iconArea.getX(), iconArea.getY());

        g.setColour (ink);
        g.strokePath (stroke, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded), place);
        g.fillPath (fill, place);
        content.removeFromLeft (8.0f);
    }

    if (hasText)
    {
        g.setColour (lit ? Palette::accent : Palette::buttonText);
        g.setFont (font);
        g.drawText (getButtonText().toUpperCase(), content, juce::Justification::centredLeft);
    }
}

//==============================================================================
Pill::Pill (const juce::String& text, bool isSwitchToUse)
    : juce::Button (text), isSwitch (isSwitchToUse)
{
    setClickingTogglesState (isSwitch);
}

void Pill::attach (juce::AudioProcessorValueTreeState& state, const juce::String& parameterId)
{
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, parameterId, *this);
}

void Pill::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    const auto radius = bounds.getHeight() * 0.5f;
    const auto on = isSwitch ? getToggleState() : isEnabled();

    g.setColour ((highlighted || down) && isEnabled() ? Palette::pill.brighter (0.07f) : Palette::pill);
    g.fillRoundedRectangle (bounds, radius);
    g.setColour (Palette::fieldLine);
    g.drawRoundedRectangle (bounds, radius, 1.0f);

    auto inner = bounds.reduced (8.0f, 0.0f);
    const auto dot = inner.removeFromLeft (6.0f).withSizeKeepingCentre (6.0f, 6.0f);
    g.setColour (on ? Palette::accent : Palette::inkFaint.withAlpha (0.6f));
    g.fillEllipse (dot);
    inner.removeFromLeft (6.0f);

    const auto text = isSwitch ? juce::String (on ? "ON" : "OFF") : getButtonText();
    g.setColour (on ? Palette::buttonText : Palette::inkFaint);
    g.setFont (Palette::font (10.0f, true, 0.04f));
    g.drawText (text, inner, juce::Justification::centredLeft);
}
