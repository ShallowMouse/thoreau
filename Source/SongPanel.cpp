#include "SongPanel.h"

namespace thoreau
{

using namespace Pequod;

SongPanel::SongPanel (DrumEngine& e) : engine (e)
{
    addButton.setTooltip ("Add the pattern you're editing to the end of the song");
    addButton.onClick = [this]
    {
        edit ([this] (auto& s) { if ((int) s.size() < kSongMax) s.push_back ({ currentPattern, 1 }); });
        selected = engine.project.songLength.load() - 1;
        updateButtons();
    };
    removeButton.onClick = [this]
    {
        edit ([this] (auto& s) { if (juce::isPositiveAndBelow (selected, (int) s.size())) s.erase (s.begin() + selected); });
        selected = juce::jmin (selected, engine.project.songLength.load() - 1);
        updateButtons();
    };
    upButton.onClick = [this]
    {
        if (selected <= 0) return;
        edit ([this] (auto& s) { std::swap (s[(size_t) selected], s[(size_t) selected - 1]); });
        --selected;
    };
    downButton.onClick = [this]
    {
        if (selected < 0 || selected >= engine.project.songLength.load() - 1) return;
        edit ([this] (auto& s) { std::swap (s[(size_t) selected], s[(size_t) selected + 1]); });
        ++selected;
    };
    lessButton.onClick = [this] { edit ([this] (auto& s) { if (juce::isPositiveAndBelow (selected, (int) s.size())) s[(size_t) selected].repeats = juce::jmax (1, s[(size_t) selected].repeats - 1); }); };
    moreButton.onClick = [this] { edit ([this] (auto& s) { if (juce::isPositiveAndBelow (selected, (int) s.size())) s[(size_t) selected].repeats = juce::jmin (99, s[(size_t) selected].repeats + 1); }); };
    lessButton.setTooltip ("Fewer repeats");
    moreButton.setTooltip ("More repeats");
    loopButton.setClickingTogglesState (true);
    loopButton.setTooltip ("Start the song again when it ends");
    loopButton.onClick = [this] { engine.project.songLoops = loopButton.getToggleState(); engine.project.changed(); if (onEdited) onEdited(); };

    for (auto* b : { &addButton, &removeButton, &upButton, &downButton, &lessButton, &moreButton, &loopButton })
        addAndMakeVisible (*b);
    updateButtons();
}

void SongPanel::edit (std::function<void (std::vector<SongEntry>&)> fn)
{
    auto s = engine.project.getSong();
    fn (s);
    engine.project.setSong (s);
    if (onEdited) onEdited();
    repaint();
}

void SongPanel::updateButtons()
{
    const int n = engine.project.songLength.load();
    const bool sel = juce::isPositiveAndBelow (selected, n);
    removeButton.setEnabled (sel);
    upButton.setEnabled (sel && selected > 0);
    downButton.setEnabled (sel && selected < n - 1);
    lessButton.setEnabled (sel);
    moreButton.setEnabled (sel);
    loopButton.setToggleState (engine.project.songLoops.load(), juce::dontSendNotification);
}

void SongPanel::tick()
{
    const int e = engine.isRolling() && engine.getPlayMode() == PlayMode::song ? engine.getSongEntry() : -1;
    if (e != playing)
    {
        playing = e;
        // keep the playing row in view
        const int visible = juce::jmax (1, listArea.getHeight() / rowH);
        if (playing >= 0 && (playing < scroll || playing >= scroll + visible))
            scroll = juce::jmax (0, playing - 1);
        repaint();
    }
}

int SongPanel::rowAt (juce::Point<int> p) const
{
    if (! listArea.contains (p)) return -1;
    const int r = scroll + (p.y - listArea.getY()) / rowH;
    return r < engine.project.songLength.load() ? r : -1;
}

void SongPanel::mouseDown (const juce::MouseEvent& e)
{
    selected = rowAt (e.getPosition());
    updateButtons();
    repaint();
}

void SongPanel::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int r = rowAt (e.getPosition());
    if (r >= 0 && onEditPattern) onEditPattern (engine.project.songPattern[(size_t) r].load());
}

void SongPanel::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    const int n = engine.project.songLength.load();
    const int visible = juce::jmax (1, listArea.getHeight() / rowH);
    scroll = juce::jlimit (0, juce::jmax (0, n - visible), scroll + (w.deltaY < 0 ? 1 : -1));
    repaint();
}

void SongPanel::resized()
{
    auto r = getLocalBounds().reduced (24, 10);
    r.removeFromTop (26);
    auto bottom = r.removeFromBottom (58);
    listArea = r.reduced (0, 4);

    auto row1 = bottom.removeFromTop (26);
    const int w = row1.getWidth() / 4;
    addButton.setBounds (row1.removeFromLeft (w).reduced (2, 0));
    removeButton.setBounds (row1.removeFromLeft (w).reduced (2, 0));
    upButton.setBounds (row1.removeFromLeft (w).reduced (2, 0));
    downButton.setBounds (row1.reduced (2, 0));
    bottom.removeFromTop (6);
    auto row2 = bottom.removeFromTop (26);
    lessButton.setBounds (row2.removeFromLeft (w).reduced (2, 0));
    moreButton.setBounds (row2.removeFromLeft (w).reduced (2, 0));
    loopButton.setBounds (row2.removeFromRight (w * 2).reduced (2, 0));
}

void SongPanel::paint (juce::Graphics& g)
{
    drawModulePanel (g, getLocalBounds().toFloat(), engine.getPlayMode() == PlayMode::song);

    auto head = getLocalBounds().reduced (24, 10).removeFromTop (26);
    g.setColour (Pequod::Colours::mustard);
    g.setFont (heading (16.0f));
    g.drawText ("SONG", head, juce::Justification::centredLeft);
    const double beats = engine.project.songBeats();
    const int bpb = juce::jmax (1, engine.project.beatsPerBar.load());
    g.setColour (Pequod::Colours::textDim);
    g.setFont (heading (11.0f, false));
    g.drawText (juce::String (beats / bpb, beats / bpb == std::floor (beats / bpb) ? 0 : 1) + " BARS", head, juce::Justification::centredRight);

    g.setColour (Pequod::Colours::well);
    g.fillRect (listArea);

    const int n = engine.project.songLength.load();
    const int visible = listArea.getHeight() / rowH;
    double start = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const int p = engine.project.songPattern[(size_t) i].load();
        const int rep = engine.project.songRepeats[(size_t) i].load();
        const double len = engine.project.patterns[(size_t) p].beats() * rep;
        if (i >= scroll && i < scroll + visible)
        {
            auto r = juce::Rectangle<int> (listArea.getX(), listArea.getY() + (i - scroll) * rowH, listArea.getWidth(), rowH);
            if (i == selected) { g.setColour (Pequod::Colours::panelHi.brighter (0.1f)); g.fillRect (r); }
            if (i == playing)  { g.setColour (Pequod::Colours::mustard); g.fillRect (r.withWidth (4)); }
            g.setFont (heading (13.0f));
            g.setColour (i == playing ? Pequod::Colours::mustard : Pequod::Colours::text);
            auto rr = r.reduced (12, 0);
            g.drawText (juce::String (i + 1).paddedLeft (' ', 3) + "   " + patternName (p), rr, juce::Justification::centredLeft);
            g.setFont (heading (12.0f, false));
            g.setColour (Pequod::Colours::textDim);
            g.drawText ("x" + juce::String (rep) + "    bar " + juce::String ((int) (start / bpb) + 1), rr, juce::Justification::centredRight);
        }
        start += len;
    }
    if (n == 0)
    {
        g.setColour (Pequod::Colours::textDim);
        g.setFont (body (13.0f));
        g.drawFittedText ("> empty - pick a pattern and press + ADD", listArea.reduced (10), juce::Justification::centred, 2);
    }
}

} // namespace thoreau
