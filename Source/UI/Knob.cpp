//
//  Knob.cpp
//  Kitbox
//

#include "Knob.h"
#include "Palette.h"

namespace
{
    // The mockup's dial, in its own 52-unit box: travel 270 degrees, ring
    // radius 21, face radius 15.5, pointer from 5 to 12.5.
    constexpr float box = 52.0f, ringRadius = 21.0f, faceRadius = 15.5f, pointerIn = 5.0f, pointerOut = 12.5f;
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
                     bool hover, juce::Colour valueColour, juce::Colour trackColour, float strokeScale)
{
    const auto scale  = juce::jmin (area.getWidth(), area.getHeight()) / box;
    const auto centre = area.getCentre();
    const auto angle  = startAngle + proportion * (endAngle - startAngle);
    const auto zero   = bipolar ? 0.0f : startAngle;
    const auto stroke = juce::PathStrokeType (3.5f * scale * strokeScale, juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded);
    const auto ring = ringRadius * scale;

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

    const auto face = faceRadius * scale;
    g.setColour (hover ? Palette::knobFace.brighter (0.08f) : Palette::knobFace);
    g.fillEllipse (centre.x - face, centre.y - face, face * 2.0f, face * 2.0f);
    g.setColour (Palette::knobEdge);
    g.drawEllipse (centre.x - face, centre.y - face, face * 2.0f, face * 2.0f, 1.0f);

    const auto at = [&] (float r) { return juce::Point<float> (centre.x + r * scale * std::sin (angle),
                                                               centre.y - r * scale * std::cos (angle)); };
    g.setColour (Palette::knobPointer);
    // The header's heavier dial has a heavier pointer too: 2.6 against 2.2.
    g.drawLine ({ at (pointerIn), at (pointerOut) }, (strokeScale > 1.0f ? 2.6f : 2.2f) * scale);
}

void Knob::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    const auto proportion = (float) valueToProportionOfLength (getValue());
    const auto hover = isMouseOverOrDragging();
    const auto value = getTextFromValue (getValue());
    const auto colours = Palette::zone (zone, rack);
    const auto track = tinted ? colours.track : Palette::knobTrack;
    const auto nameColour = tinted ? colours.label : Palette::inkDim;

    if (layout == Layout::beside)
    {
        // 38 points, a heavier ring (the mockup's 4 against 3.5), name and value to the right.
        const auto dial = bounds.removeFromLeft (bounds.getHeight());
        drawDial (g, dial, proportion, bipolar, hover, colours.line, track, 4.0f / 3.5f);

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

    // Stacked: a 16-point row with the name - or the value - then 6, the dial.
    const auto text = bounds.removeFromTop (16.0f).toNearestInt();

    if (isShowingValue())
    {
        g.setColour (isEnabled() ? Palette::ink : Palette::inkDim);
        g.setFont (Palette::font (12.5f, true));
        g.drawFittedText (value, text, juce::Justification::centred, 1, 0.8f);
    }
    else
    {
        g.setColour (nameColour);
        g.setFont (Palette::label (12.0f));
        g.drawFittedText (label, text, juce::Justification::centred, 1, 0.8f);
    }

    bounds.removeFromTop (6.0f);
    const auto dialRow = bounds.removeFromTop (52.0f);
    drawDial (g, dialRow.withSizeKeepingCentre (52.0f, 52.0f), proportion, bipolar, hover, colours.line, track);
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
