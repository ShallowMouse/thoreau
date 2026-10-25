#pragma once

#include <JuceHeader.h>
#include "ThoreauLookAndFeel.h"
#include "DrumEngine.h"

namespace thoreau
{

//==============================================================================
/** The song: a chain of patterns, each repeated. Click a row to select it,
    double-click to edit that pattern. */
class SongPanel : public juce::Component
{
public:
    explicit SongPanel (DrumEngine&);

    /** The pattern the ADD button appends. */
    void setCurrentPattern (int p) { currentPattern = p; }
    std::function<void (int pattern)> onEditPattern;
    std::function<void()> onEdited;

    void tick();
    void refresh() { repaint(); updateButtons(); }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    DrumEngine& engine;
    int currentPattern = 0, selected = -1, playing = -1, scroll = 0;
    juce::TextButton addButton { "+ ADD" }, removeButton { "REMOVE" }, upButton { "UP" }, downButton { "DOWN" },
                     lessButton { "-" }, moreButton { "+" }, loopButton { "LOOP" };
    juce::Rectangle<int> listArea;
    static constexpr int rowH = 24;

    void edit (std::function<void (std::vector<SongEntry>&)>);
    void updateButtons();
    int rowAt (juce::Point<int>) const;
};

} // namespace thoreau
