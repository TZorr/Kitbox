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
    // Where a knob's dial goes - under the 14-point value row and 1, in the
    // 42-point row - at most 92 wide, 26 high (Variante B).
    const auto row = getLocalBounds().toFloat().withTrimmedTop (14.0f + 1.0f).removeFromTop (42.0f);
    return row.withSizeKeepingCentre (juce::jmin (row.getWidth() - 4.0f, 92.0f), 26.0f);
}

void ChoiceBox::paint (juce::Graphics& g)
{
    const auto box = boxBounds();

    g.setColour (isMouseOver() ? Palette::choiceFace.brighter (0.08f) : Palette::choiceFace);
    g.fillRoundedRectangle (box, 4.0f);
    g.setColour (isMouseOver() && zone != Palette::ZoneId::none ? Palette::zone (zone, rack).border : Palette::choiceLine);
    g.drawRoundedRectangle (box.reduced (0.5f), 4.0f, 1.0f);

    // The text, and a small filled triangle at the right.
    auto inner = box.reduced (8.0f, 0.0f);
    {
        const auto arrow = inner.removeFromRight (8.0f).withSizeKeepingCentre (8.0f, 8.0f);
        juce::Path triangle;
        triangle.addTriangle (arrow.getX() + 1.0f, arrow.getY() + 2.5f, arrow.getRight() - 1.0f, arrow.getY() + 2.5f,
                              arrow.getCentreX(), arrow.getY() + 6.0f);
        g.setColour (Palette::choiceArrow);
        g.fillPath (triangle);
    }
    inner.removeFromRight (4.0f);

    g.setColour (Palette::choiceInk);
    g.setFont (Palette::font (11.0f, true, 0.02f));
    g.drawFittedText (textFor (current).toUpperCase(), inner.toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);

    // The name under it, where a knob has its own.
    g.setColour (Palette::knobName);
    g.setFont (Palette::label (11.0f));
    g.drawFittedText (label, getLocalBounds().withTrimmedTop (14 + 1 + 42 + 1).removeFromTop (14),
                      juce::Justification::centred, 1, 0.8f);
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
    menu.showMenuAsync (panelMenu (*this).withTargetScreenArea (localAreaToGlobal (boxBounds().toNearestInt()))
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

        // The name, then the tag framed after it (Variante B's "FACTORY").
        const auto nameFont = Palette::font (14.0f, true);
        const auto tagFont = Palette::font (10.0f, true, 0.1f);
        const auto tagText = tag.toUpperCase();
        const auto tagWidth = tag.isEmpty() ? 0.0f : juce::GlyphArrangement::getStringWidth (tagFont, tagText) + 12.0f;
        const auto nameWidth = juce::jmin (juce::GlyphArrangement::getStringWidth (nameFont, getButtonText()) + 2.0f,
                                           inner.getWidth() - (tagWidth > 0.0f ? tagWidth + 10.0f : 0.0f));

        g.setColour (Palette::title);
        g.setFont (nameFont);
        g.drawFittedText (getButtonText(), inner.removeFromLeft (nameWidth).toNearestInt(), juce::Justification::centredLeft, 1, 0.9f);

        if (tagWidth > 0.0f)
        {
            inner.removeFromLeft (10.0f);
            const auto column = inner.removeFromLeft (juce::jmin (tagWidth, inner.getWidth()));
            const auto frame = column.withSizeKeepingCentre (column.getWidth(), 18.0f);
            g.setColour (Palette::padEmptyLine.withMultipliedAlpha (0.9f));
            g.drawRoundedRectangle (frame.reduced (0.5f), 4.0f, 1.0f);
            g.setColour (Palette::inkDim);
            g.setFont (tagFont);
            g.drawFittedText (tagText, frame.toNearestInt(), juce::Justification::centred, 1, 0.8f);
        }
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

//==============================================================================
RackPower::RackPower (const juce::String& titleText)
    : juce::Button (titleText), title (titleText.toUpperCase())
{
    setClickingTogglesState (true);
}

void RackPower::attach (juce::AudioProcessorValueTreeState& state, const juce::String& parameterId)
{
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, parameterId, *this);
}

void RackPower::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    const auto on = getToggleState();
    const auto colours = Palette::zone (zone, true);

    g.setColour (highlighted || down ? Palette::powerFace.brighter (0.07f) : Palette::powerFace);
    g.fillRoundedRectangle (bounds, 4.0f);
    g.setColour (Palette::powerLine);
    g.drawRoundedRectangle (bounds, 4.0f, 1.0f);

    // The dot over the title, the title read bottom to top; centred as a pair.
    const auto font = Palette::font (11.0f, true, 0.14f);
    const auto length = juce::GlyphArrangement::getStringWidth (font, title);
    const auto total = 6.0f + 6.0f + length;
    const auto top = bounds.getCentreY() - total * 0.5f;

    g.setColour (on ? colours.line : Palette::powerOffDot);
    g.fillEllipse (bounds.getCentreX() - 3.0f, top, 6.0f, 6.0f);

    const auto textCentre = juce::Point<float> (bounds.getCentreX(), top + 12.0f + length * 0.5f);
    juce::Graphics::ScopedSaveState state (g);
    g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, textCentre.x, textCentre.y));
    g.setColour (on ? colours.label : Palette::powerOffInk);
    g.setFont (font);
    g.drawText (title, juce::Rectangle<float> (length + 4.0f, 14.0f).withCentre (textCentre), juce::Justification::centred, false);
}
