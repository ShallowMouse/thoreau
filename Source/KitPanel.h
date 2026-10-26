#pragma once

#include <JuceHeader.h>
#include "ThoreauLookAndFeel.h"
#include "DrumEngine.h"

namespace thoreau
{

//==============================================================================
/** The kit: which one, and its mics and bus (CLOSE, OVERHEADS, ROOM, COMP, TAPE). */
class KitPanel : public juce::Component
{
public:
    explicit KitPanel (DrumEngine&);

    /** After a project load or a kit change from elsewhere. */
    void refresh();

    std::function<void()> onKitChanged;      // the drums' settings changed too
    std::function<void()> onEdited;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    DrumEngine& engine;
    juce::ComboBox kitBox;
    std::array<juce::Slider, 5> knobs;
    juce::Rectangle<int> titleArea, descArea, knobArea;
    static const char* const labels[5];

    void write (int knob, double value);
};

} // namespace thoreau
