#include "KitPanel.h"

namespace thoreau
{

using namespace Pequod;

const char* const KitPanel::labels[5] = { "CLOSE", "OVERHEADS", "ROOM", "COMP", "TAPE" };

KitPanel::KitPanel (DrumEngine& e) : engine (e)
{
    for (int i = 0; i < (int) kits().size(); ++i)
    {
        if (i == 1) kitBox.addSeparator();
        kitBox.addItem (kits()[(size_t) i].name, i + 1);
    }
    kitBox.setTooltip ("The kit. Changing it resets each drum's sound knobs, level and pan to the kit's (patterns stay)");
    kitBox.onChange = [this]
    {
        const int k = kitBox.getSelectedId() - 1;
        if (k < 0 || k == engine.project.kit.load()) return;
        engine.project.setKit (k);
        engine.kitChanged();
        refresh();
        if (onKitChanged) onKitChanged();
    };
    addAndMakeVisible (kitBox);

    const char* tips[5] = {
        "The close mics: each drum on its own",
        "The overhead pair: the whole kit from above, cymbals especially",
        "The room mics (and the kit's echo, if it has one)",
        "Bus compression, in the kit's style",
        "Tape: saturation and a softer top end" };
    for (int i = 0; i < 5; ++i)
    {
        auto& s = knobs[(size_t) i];
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 15);
        s.setColour (juce::Slider::textBoxTextColourId, Pequod::Colours::textDim);
        s.setTooltip (tips[i]);
        if (i < 3)
        {
            s.setRange (-60.0, 12.0, 0.1);
            s.setSkewFactorFromMidPoint (-12.0);
            s.textFromValueFunction = [] (double v) { return v <= -59.9 ? juce::String ("OFF") : juce::String (v, 1) + " dB"; };
        }
        else
        {
            s.setRange (0.0, 1.0, 0.01);
            s.textFromValueFunction = [] (double v) { return v < 0.005 ? juce::String ("OFF") : juce::String (juce::roundToInt (v * 100)); };
        }
        s.onValueChange = [this, i] { write (i, knobs[(size_t) i].getValue()); };
        s.onDragEnd = [this] { if (onEdited) onEdited(); };
        addAndMakeVisible (s);
    }
    refresh();
}

void KitPanel::write (int i, double v)
{
    auto& p = engine.project;
    switch (i)
    {
        case 0: p.closeDb = (float) v; break;
        case 1: p.ohDb = (float) v; break;
        case 2: p.roomDb = (float) v; break;
        case 3: p.compAmount = (float) v; break;
        case 4: p.tapeAmount = (float) v; break;
        default: break;
    }
    p.changed();
}

void KitPanel::refresh()
{
    auto& p = engine.project;
    kitBox.setSelectedId (p.kit.load() + 1, juce::dontSendNotification);
    const double v[5] = { p.closeDb.load(), p.ohDb.load(), p.roomDb.load(), p.compAmount.load(), p.tapeAmount.load() };
    for (int i = 0; i < 5; ++i)
    {
        knobs[(size_t) i].setValue (v[i], juce::dontSendNotification);
        knobs[(size_t) i].updateText();
        knobs[(size_t) i].setDoubleClickReturnValue (true, (double) (i == 0 ? kitAt (p.kit).closeDb : i == 1 ? kitAt (p.kit).ohDb
                                                                    : i == 2 ? kitAt (p.kit).roomDb : i == 3 ? kitAt (p.kit).comp : kitAt (p.kit).tape));
    }
    knobs[1].setEnabled (kitAt (p.kit).acoustic);     // the synth kit has no overheads
    repaint();
}

void KitPanel::resized()
{
    auto r = getLocalBounds().reduced (24, 8);
    titleArea = r.removeFromTop (30);
    kitBox.setBounds (titleArea.withTrimmedLeft (50).reduced (0, 3));
    descArea = r.removeFromTop (30);
    knobArea = r;
    const int w = knobArea.getWidth() / 5;
    for (int i = 0; i < 5; ++i)
        knobs[(size_t) i].setBounds (knobArea.getX() + i * w, knobArea.getY() + 14, w, knobArea.getHeight() - 14);
}

void KitPanel::paint (juce::Graphics& g)
{
    drawModulePanel (g, getLocalBounds().toFloat(), true);
    g.setColour (Pequod::Colours::mustard);
    g.setFont (heading (16.0f));
    g.drawText ("KIT", titleArea.withWidth (46), juce::Justification::centredLeft);

    g.setColour (Pequod::Colours::textDim);
    g.setFont (body (11.5f));
    g.drawFittedText (kitAt (engine.project.kit).description, descArea, juce::Justification::topLeft, 2, 0.9f);

    g.setFont (heading (10.5f));
    g.setColour (Pequod::Colours::text);
    const int w = knobArea.getWidth() / 5;
    for (int i = 0; i < 5; ++i)
        g.drawText (labels[i], knobArea.getX() + i * w, knobArea.getY(), w, 14, juce::Justification::centred);
}

} // namespace thoreau
