#pragma once

#include <JuceHeader.h>
#include "ThoreauLookAndFeel.h"
#include "DrumEngine.h"

namespace thoreau
{

//==============================================================================
/** The selected drum: its sound (synth or sample), level, pan and MIDI note. */
class VoicePanel : public juce::Component
{
public:
    explicit VoicePanel (DrumEngine&);

    void setTrack (int t);
    int getTrack() const noexcept { return track; }
    void refresh();                                   // after a project load
    std::function<void()> onEdited;
    std::function<void (const juce::String&)> onMessage;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::String label;
    };

    DrumEngine& engine;
    int track = 0;
    std::array<Knob, 6> knobs;       // tune, decay, tone, snap, level, pan
    juce::TextButton loadButton { "LOAD SAMPLE" }, clearButton { "USE SYNTH" }, auditionButton { "PLAY" };
    juce::ComboBox noteBox;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> titleArea, knobArea, sampleArea;

    void write (int knob, double value);
    static juce::String noteName (int n);
};

} // namespace thoreau
