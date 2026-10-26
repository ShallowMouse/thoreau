#include "VoicePanel.h"

namespace thoreau
{

using namespace Pequod;

juce::String VoicePanel::noteName (int n)
{
    return juce::MidiMessage::getMidiNoteName (n, true, true, 3) + "  (" + juce::String (n) + ")";
}

VoicePanel::VoicePanel (DrumEngine& e) : engine (e)
{
    static const char* labels[6] = { "TUNE", "DECAY", "TONE", "SNAP", "LEVEL", "PAN" };
    for (int i = 0; i < 6; ++i)
    {
        auto& k = knobs[(size_t) i];
        k.label = labels[i];
        auto& s = k.slider;
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
        s.setColour (juce::Slider::textBoxTextColourId, Pequod::Colours::textDim);
        switch (i)
        {
            case 0: s.setRange (-12.0, 12.0, 0.1); s.setDoubleClickReturnValue (true, 0.0); s.setTextValueSuffix (" st"); break;
            case 4: s.setRange (-48.0, 6.0, 0.1); s.setDoubleClickReturnValue (true, 0.0); s.setTextValueSuffix (" dB"); s.setSkewFactorFromMidPoint (-12.0); break;
            case 5: s.setRange (-1.0, 1.0, 0.01); s.setDoubleClickReturnValue (true, 0.0); break;
            default: s.setRange (0.0, 1.0, 0.001); s.setDoubleClickReturnValue (true, 0.5); break;
        }
        if (i == 5)
            s.textFromValueFunction = [] (double v) { return std::abs (v) < 0.01 ? juce::String ("C") : (v < 0 ? "L" : "R") + juce::String (juce::roundToInt (std::abs (v) * 100)); };
        else if (i >= 1 && i <= 3)
            s.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v * 100)); };
        s.onValueChange = [this, i] { write (i, knobs[(size_t) i].slider.getValue()); };
        s.onDragEnd = [this] { if (onEdited) onEdited(); };
        addAndMakeVisible (s);
    }
    knobs[0].slider.setTooltip ("Pitch, in semitones (double-click: 0)");
    knobs[1].slider.setTooltip ("How long it rings");
    knobs[2].slider.setTooltip ("Brightness (kick: drive)");
    knobs[3].slider.setTooltip ("Attack: pitch sweep, noise, click");
    knobs[4].slider.setTooltip ("Level into the mix (and its own Hawthorne channel)");
    knobs[5].slider.setTooltip ("Pan in the stereo mix");

    loadButton.setTooltip ("Play a WAV / AIFF / FLAC file on this pad instead of THOREAU's own sound");
    loadButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Sample for " + instrumentName (track),
                                                       juce::File::getSpecialLocation (juce::File::userMusicDirectory), "*.wav;*.aif;*.aiff;*.flac;*.ogg");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
        {
            const auto f = fc.getResult();
            if (f == juce::File()) return;
            juce::String err;
            if (! engine.project.loadSample (track, f, err)) { if (onMessage) onMessage ("!! " + err); return; }
            if (onMessage) onMessage (instrumentName (track) + " now plays " + f.getFileName());
            if (onEdited) onEdited();
            refresh();
            engine.audition (track, 0.9f);
        });
    };
    addAndMakeVisible (loadButton);

    clearButton.setTooltip ("Back to the kit's own drum");
    clearButton.onClick = [this] { engine.project.clearSample (track); if (onEdited) onEdited(); refresh(); engine.audition (track, 0.9f); };
    addAndMakeVisible (clearButton);

    auditionButton.onClick = [this] { engine.audition (track, 0.9f); };
    addAndMakeVisible (auditionButton);

    fromBox.addItem ("FROM THIS KIT", 1);
    fromBox.addSeparator();
    for (int i = 0; i < (int) kits().size(); ++i)
        fromBox.addItem ("FROM " + kits()[(size_t) i].name, i + 2);
    fromBox.setTooltip ("Play this pad's drum from another kit: mix and match pieces across kits");
    fromBox.onChange = [this]
    {
        const int id = fromBox.getSelectedId();
        if (id <= 0) return;
        const int k = id == 1 ? -1 : id - 2;
        auto& tr = engine.project.tracks[(size_t) track];
        if (tr.pieceKit.load() == k) return;
        tr.pieceKit = (k == engine.project.kit.load()) ? -1 : k;
        engine.project.changed();
        if (onEdited) onEdited();
        refresh();
        engine.audition (track, 0.9f);
    };
    addAndMakeVisible (fromBox);

    for (int n = 24; n <= 96; ++n) noteBox.addItem (noteName (n), n + 1);
    noteBox.setTooltip ("MIDI note that plays this drum");
    noteBox.onChange = [this]
    {
        if (noteBox.getSelectedId() > 0)
        {
            engine.project.tracks[(size_t) track].midiNote = noteBox.getSelectedId() - 1;
            if (onEdited) onEdited();
        }
    };
    addAndMakeVisible (noteBox);

    refresh();
}

void VoicePanel::setTrack (int t)
{
    track = juce::jlimit (0, kTracks - 1, t);
    refresh();
}

void VoicePanel::write (int k, double v)
{
    auto& tr = engine.project.tracks[(size_t) track];
    switch (k)
    {
        case 0: tr.tune = (float) v; break;
        case 1: tr.decay = (float) v; break;
        case 2: tr.tone = (float) v; break;
        case 3: tr.snap = (float) v; break;
        case 4: tr.levelDb = (float) v; break;
        case 5: tr.pan = (float) v; break;
        default: break;
    }
    engine.project.changed();
}

void VoicePanel::refresh()
{
    const auto& tr = engine.project.tracks[(size_t) track];
    const double vals[6] = { tr.tune.load(), tr.decay.load(), tr.tone.load(), tr.snap.load(), tr.levelDb.load(), tr.pan.load() };
    for (int i = 0; i < 6; ++i)
    {
        knobs[(size_t) i].slider.setValue (vals[i], juce::dontSendNotification);
        knobs[(size_t) i].slider.updateText();
    }
    noteBox.setSelectedId (tr.midiNote.load() + 1, juce::dontSendNotification);
    fromBox.setSelectedId (tr.pieceKit.load() < 0 ? 1 : tr.pieceKit.load() + 2, juce::dontSendNotification);
    const bool acoustic = engine.project.slotFor (track).model.kind != Kind::analog && tr.sample.load() == nullptr;
    knobs[1].label = acoustic ? "RING" : "DECAY";
    knobs[3].label = acoustic ? "ATTACK" : "SNAP";
    knobs[1].slider.setTooltip (acoustic ? "How long the drum rings (and how much it's muffled)" : "How long it rings");
    knobs[2].slider.setTooltip (acoustic ? "Brightness: the head, and how the mic hears it" : "Brightness (kick: drive)");
    knobs[3].slider.setTooltip (acoustic ? "Stick or beater: soft and round to hard and cracking" : "Attack: pitch sweep, noise, click");
    clearButton.setEnabled (tr.sample.load() != nullptr);
    repaint();
}

void VoicePanel::resized()
{
    auto r = getLocalBounds().reduced (24, 8);
    titleArea = r.removeFromLeft (200);
    fromBox.setBounds (titleArea.withTop (titleArea.getBottom() - 30).reduced (0, 3).withTrimmedRight (10));
    sampleArea = r.removeFromRight (230);
    knobArea = r;

    const int w = knobArea.getWidth() / 6;
    for (int i = 0; i < 6; ++i)
    {
        auto k = juce::Rectangle<int> (knobArea.getX() + i * w, knobArea.getY() + 16, w, knobArea.getHeight() - 16);
        knobs[(size_t) i].slider.setBounds (k.withSizeKeepingCentre (juce::jmin (w, 84), k.getHeight()));
    }

    auto s = sampleArea.reduced (8, 18);
    const int h = juce::jmin (26, s.getHeight() / 3 - 4);
    loadButton.setBounds (s.removeFromTop (h));
    s.removeFromTop (6);
    auto row = s.removeFromTop (h);
    clearButton.setBounds (row.removeFromLeft (row.getWidth() / 2 - 3));
    row.removeFromLeft (6);
    auditionButton.setBounds (row);
    s.removeFromTop (6);
    noteBox.setBounds (s.removeFromTop (h));
}

void VoicePanel::paint (juce::Graphics& g)
{
    drawModulePanel (g, getLocalBounds().toFloat(), true);

    auto t = titleArea;
    g.setColour (Pequod::Colours::mustard);
    g.setFont (heading (22.0f));
    g.drawText (engine.project.padName (track), t.removeFromTop (34).withTrimmedTop (10), juce::Justification::centredLeft);
    const auto file = engine.project.getSampleFile (track);
    const int pk = engine.project.tracks[(size_t) track].pieceKit.load();
    const auto& kitName = kitAt (pk >= 0 ? pk : engine.project.kit.load()).name;
    g.setFont (body (13.0f));
    g.setColour (Pequod::Colours::textDim);
    g.drawFittedText (file != juce::File() ? "SAMPLE: " + file.getFileName() : kitName,
                      t.removeFromTop (34), juce::Justification::topLeft, 2);
    g.setFont (heading (11.0f, false));
    g.drawText ("> pad " + juce::String (track + 1) + " of " + juce::String (kTracks), t.removeFromTop (18), juce::Justification::centredLeft);

    g.setFont (heading (12.0f));
    g.setColour (Pequod::Colours::text);
    const int w = knobArea.getWidth() / 6;
    for (int i = 0; i < 6; ++i)
        g.drawText (knobs[(size_t) i].label, knobArea.getX() + i * w, knobArea.getY(), w, 16, juce::Justification::centred);
}

} // namespace thoreau
