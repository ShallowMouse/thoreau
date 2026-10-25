#include "SettingsPanel.h"

namespace thoreau
{

using namespace Pequod;

SettingsPanel::SettingsPanel (juce::AudioDeviceManager& dm, DrumEngine& engine, bool followReel, Callbacks c)
    : callbacks (std::move (c))
{
    auto* audio = new juce::AudioDeviceSelectorComponent (dm, 0, 0, 2, 2, true, false, true, false);
    tabs.addTab ("AUDIO", Pequod::Colours::panel, audio, true);
    tabs.addTab ("HAWTHORNE", Pequod::Colours::panel, new LinkPage (engine, followReel, callbacks), true);
    tabs.setTabBarDepth (34);
    tabs.setOutline (0);
    addAndMakeVisible (tabs);
    setSize (680, 560);
}

void SettingsPanel::resized()  { tabs.setBounds (getLocalBounds().reduced (8)); }
void SettingsPanel::paint (juce::Graphics& g) { g.fillAll (Pequod::Colours::panel); }

//==============================================================================
SettingsPanel::LinkPage::LinkPage (DrumEngine& e, bool followReel, Callbacks& c) : engine (e), cb (c)
{
    linkToggle.setToggleState (engine.isNetworkEnabled(), juce::dontSendNotification);
    linkToggle.onClick = [this] { if (cb.setLink) cb.setLink (linkToggle.getToggleState(), nameEditor.getText()); repaint(); };

    nameEditor.setText (engine.netOut.getName(), false);
    nameEditor.setTooltip ("How THOREAU appears in Hawthorne's NETWORK window");
    nameEditor.onReturnKey = nameEditor.onFocusLost = [this]
    {
        engine.netOut.setName (nameEditor.getText());
        if (cb.setLink && engine.isNetworkEnabled()) cb.setLink (true, nameEditor.getText());
    };

    indivToggle.setToggleState (engine.getIndividualOuts(), juce::dontSendNotification);
    indivToggle.onClick = [this] { if (cb.setIndividualOuts) cb.setIndividualOuts (indivToggle.getToggleState()); repaint(); };
    localToggle.setToggleState (engine.getLocalOutput(), juce::dontSendNotification);
    localToggle.onClick = [this] { if (cb.setLocalOut) cb.setLocalOut (localToggle.getToggleState()); };
    followToggle.setToggleState (followReel, juce::dontSendNotification);
    followToggle.onClick = [this] { if (cb.setFollowReel) cb.setFollowReel (followToggle.getToggleState()); };

    nameLabel.setFont (heading (12.0f));
    nameLabel.setColour (juce::Label::textColourId, Pequod::Colours::textDim);
    for (juce::Component* comp : { (juce::Component*) &linkToggle, (juce::Component*) &nameLabel, (juce::Component*) &nameEditor,
                                   (juce::Component*) &indivToggle, (juce::Component*) &localToggle, (juce::Component*) &followToggle })
        addAndMakeVisible (comp);
}

void SettingsPanel::LinkPage::resized()
{
    auto r = getLocalBounds().reduced (20, 16);
    linkToggle.setBounds (r.removeFromTop (28));
    r.removeFromTop (6);
    auto row = r.removeFromTop (28);
    nameLabel.setBounds (row.removeFromLeft (170));
    nameEditor.setBounds (row.removeFromLeft (260));
    r.removeFromTop (14);
    indivToggle.setBounds (r.removeFromTop (28));
    localToggle.setBounds (r.removeFromTop (28));
    followToggle.setBounds (r.removeFromTop (28));
}

void SettingsPanel::LinkPage::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().reduced (20, 16);
    r.removeFromTop (28 + 6 + 28 + 14 + 28 * 3 + 16);
    g.setColour (Pequod::Colours::textDim);
    g.setFont (body (13.5f));
    g.drawFittedText (
        "In Hawthorne, open NETWORK and connect \"" + engine.netOut.getName() + "\". THOREAU shows up as a block of inputs: "
        "1-2 are the stereo mix" + juce::String (engine.getIndividualOuts() ? ", 3-14 the drums one by one (kick, snare, clap ... cowbell)" : "")
        + ". Patch them to tracks like any input.\n\n"
        "Set THOREAU's clock to HAWTHORNE and it follows the tape: press play on Hawthorne and the drums play from "
        "wherever the tape is, at the reel's tempo (THOREAU stores its tempo on the reel the first time). Hits land "
        "sample-exact where they're recorded. Monitored live, the drums are one interface round trip behind the tape, "
        "like any outboard gear.\n\n"
        "Both PCs (or both programs) must run at the same sample rate. If Hawthorne is on another subnet or VLAN, add "
        "this PC's IP address in Hawthorne's NETWORK window.",
        r, juce::Justification::topLeft, 20);
}

} // namespace thoreau
