#pragma once

#include <JuceHeader.h>
#include "ThoreauLookAndFeel.h"
#include "DrumEngine.h"

namespace thoreau
{

//==============================================================================
/**
    The twelve drums down the side, the steps of the pattern across.
      pad          click: play it and select the drum      M / S: mute / solo
      step         click: on / off      right- or shift-click: soft > normal > accent > off
                   drag: paint
*/
class StepGrid : public juce::Component,
                 public juce::SettableTooltipClient
{
public:
    explicit StepGrid (DrumEngine&);

    void setPattern (int p)                { pattern = p; repaint(); }
    int getPattern() const noexcept        { return pattern; }
    void setSelectedTrack (int t)          { selected = t; repaint(); }
    int getSelectedTrack() const noexcept  { return selected; }
    std::function<void (int)> onTrackSelected;
    std::function<void()> onEdited;

    /** ~30 Hz: playhead, hit lights, meters. */
    void tick();

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

    static constexpr int padWidth = 128, msWidth = 52, meterWidth = 10;

private:
    DrumEngine& engine;
    int pattern = 0, selected = 0;
    int playStep = -1;
    std::array<float, kTracks> hit {}, level {};
    int paintValue = -1, paintTrack = -1, lastPainted = -1;

    juce::Rectangle<int> rowArea (int track) const;
    juce::Rectangle<int> stepArea (int track, int step) const;
    juce::Rectangle<int> padArea (int track) const;
    juce::Rectangle<int> muteArea (int track) const;
    juce::Rectangle<int> soloArea (int track) const;
    int stepAt (juce::Point<int>, int& track) const;
    void setStep (int track, int step, int value);
};

} // namespace thoreau
