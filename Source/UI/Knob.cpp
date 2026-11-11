//
//  Knob.cpp
//  Kitbox
//

#include "Knob.h"
#include "Palette.h"

namespace
{
    // Variante B's dial, in its own 52-unit box: travel 270 degrees, ring
    // radius 22, the cap's rim 15.6 and its face 14.4 (0.8 higher), a
    // highlight of 9, the pointer from 4 to 12.5.
    constexpr float box = 52.0f, ringRadius = 22.0f, pointerIn = 4.0f, pointerOut = 12.5f;
    constexpr float startAngle = -0.75f * juce::MathConstants<float>::pi;
    constexpr float endAngle   =  0.75f * juce::MathConstants<float>::pi;
}

Knob::Knob (const juce::String& labelText, bool isBipolar, Layout layoutToUse)
    : label (labelText.toUpperCase()), bipolar (isBipolar), layout (layoutToUse)
{
    setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                         juce::MathConstants<float>::pi * 2.75f, true);
    setMouseDragSensitivity (220);
    setVelocityBasedMode (false);
}

Knob::~Knob()
{
    attachment.reset();
}

void Knob::attach (juce::AudioProcessorValueTreeState& state, const juce::String& parameterId)
{
    attachment.reset();
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, parameterId, *this);

    if (auto* parameter = state.getParameter (parameterId))
        setDoubleClickReturnValue (true, parameter->convertFrom0to1 (parameter->getDefaultValue()));

    repaint();
}

void Knob::setLabel (const juce::String& newLabel)
{
    label = newLabel.toUpperCase();
    repaint();
}

void Knob::setZone (Palette::ZoneId zoneToUse, bool tintTrackAndName, bool inRack)
{
    zone = zoneToUse;
    tinted = tintTrackAndName;
    rack = inRack;
    repaint();
}

void Knob::drawDial (juce::Graphics& g, juce::Rectangle<float> area, float proportion, bool bipolar,
                     bool hover, juce::Colour valueColour, juce::Colour trackColour, bool big)
{
    const auto scale  = juce::jmin (area.getWidth(), area.getHeight()) / box;
    const auto origin = area.getCentre() - juce::Point<float> (26.0f, 26.0f) * scale;
    const auto at = [&] (float x, float y) { return origin + juce::Point<float> (x, y) * scale; };
    const auto centre = at (26.0f, 26.0f);
    const auto angle  = startAngle + proportion * (endAngle - startAngle);
    const auto zero   = bipolar ? 0.0f : startAngle;
    const auto stroke = juce::PathStrokeType ((big ? 4.0f : 4.2f) * scale, juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded);
    const auto ring = ringRadius * scale;
    const auto polar = [&] (float a, float r) { return juce::Point<float> (centre.x + r * scale * std::sin (a),
                                                                           centre.y - r * scale * std::cos (a)); };

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, ring, ring, 0.0f, startAngle, endAngle, true);
    g.setColour (trackColour);
    g.strokePath (track, stroke);

    if (std::abs (angle - zero) > 0.01f)
    {
        juce::Path value;
        value.addCentredArc (centre.x, centre.y, ring, ring, 0.0f, juce::jmin (zero, angle), juce::jmax (zero, angle), true);
        g.setColour (valueColour);
        g.strokePath (value, stroke);
    }

    // Where the value starts from: the bottom left, or twelve o'clock.
    if (! big)
    {
        const auto dot = polar (zero, ringRadius);
        const auto r = 2.6f * scale;
        g.setColour (valueColour);
        g.fillEllipse (dot.x - r, dot.y - r, 2.0f * r, 2.0f * r);
    }

    const auto circle = [&] (float cx, float cy, float r, juce::Colour colour)
    {
        const auto c = at (cx, cy);
        g.setColour (colour);
        g.fillEllipse (c.x - r * scale, c.y - r * scale, 2.0f * r * scale, 2.0f * r * scale);
    };

    const auto lift = [hover] (juce::Colour c) { return hover ? c.brighter (0.06f) : c; };

    if (big)
    {
        circle (26.0f, 26.0f, 16.0f, Palette::bigCapRim);
        circle (26.0f, 25.3f, 14.6f, lift (Palette::bigCap));
    }
    else
    {
        circle (26.0f, 26.0f, 15.6f, Palette::capRim);
        circle (26.0f, 25.2f, 14.4f, lift (Palette::cap));
        circle (25.0f, 23.4f, 9.0f, lift (Palette::capHighlight));
    }

    g.setColour (Palette::capPointer);
    g.drawLine ({ polar (angle, pointerIn), polar (angle, pointerOut) }, 2.8f * scale);
}

void Knob::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    const auto proportion = (float) valueToProportionOfLength (getValue());
    const auto hover = isMouseOverOrDragging();
    const auto value = getTextFromValue (getValue());
    const auto colours = Palette::zone (zone, rack);

    if (layout == Layout::beside)
    {
        // 38 points, the header's heavier dial, name and value to the right.
        const auto dial = bounds.removeFromLeft (bounds.getHeight());
        drawDial (g, dial, proportion, bipolar, hover, colours.line, colours.track, true);

        bounds.removeFromLeft (10.0f);
        auto text = bounds.withSizeKeepingCentre (bounds.getWidth(), 30.0f);

        g.setColour (Palette::inkDim);
        g.setFont (Palette::label (11.0f));
        g.drawText (label, text.removeFromTop (15.0f), juce::Justification::bottomLeft);

        g.setColour (Palette::ink);
        g.setFont (Palette::font (12.0f));
        g.drawText (value, text, juce::Justification::topLeft);
        return;
    }

    // Stacked: the value, 1, the 42-point dial, 1, the name.
    const auto valueRow = bounds.removeFromTop (14.0f).toNearestInt();
    g.setColour (isShowingValue() ? Palette::ink : Palette::inkFaint);
    g.setFont (Palette::font (11.0f, isShowingValue()));
    g.drawFittedText (value, valueRow, juce::Justification::centred, 1, 0.8f);

    bounds.removeFromTop (1.0f);
    const auto dialRow = bounds.removeFromTop (42.0f);
    drawDial (g, dialRow.withSizeKeepingCentre (42.0f, 42.0f), proportion, bipolar, hover, colours.line, colours.track);

    bounds.removeFromTop (1.0f);
    g.setColour (Palette::knobName);
    g.setFont (Palette::label (11.0f));
    g.drawFittedText (label, bounds.removeFromTop (14.0f).toNearestInt(), juce::Justification::centred, 1, 0.8f);
}

void Knob::holdValue()
{
    startTimer (valueHoldMs);
    repaint();
}

void Knob::valueChanged()
{
    // Only a change made here - drag, wheel, double-click - while the mouse
    // is on the knob; the host's automation reaches the knob without it.
    if (isMouseOverOrDragging())
        holdValue();
}

void Knob::mouseEnter (const juce::MouseEvent& event)
{
    Slider::mouseEnter (event);
    repaint();
}

void Knob::mouseExit (const juce::MouseEvent& event)
{
    Slider::mouseExit (event);
    holdValue();
}

void Knob::mouseUp (const juce::MouseEvent& event)
{
    Slider::mouseUp (event);
    holdValue();
}

void Knob::timerCallback()
{
    stopTimer();
    repaint();
}
