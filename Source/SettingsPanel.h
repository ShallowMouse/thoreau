#pragma once

#include <JuceHeader.h>
#include "ThoreauLookAndFeel.h"
#include "DrumEngine.h"

namespace thoreau
{

//==============================================================================
/** AUDIO (device, MIDI inputs) and HAWTHORNE (link, channels, reel follow). */
class SettingsPanel : public juce::Component
{
public:
    struct Callbacks
    {
        std::function<void (bool on, const juce::String& name)> setLink;
        std::function<void (bool on)> setIndividualOuts;     // restarts the audio
        std::function<void (bool on)> setLocalOut;
        std::function<void (bool on)> setFollowReel;
    };

    SettingsPanel (juce::AudioDeviceManager&, DrumEngine&, bool followReel, Callbacks);

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    struct LinkPage : public juce::Component
    {
        LinkPage (DrumEngine&, bool followReel, Callbacks&);
        void resized() override;
        void paint (juce::Graphics&) override;

        DrumEngine& engine;
        Callbacks& cb;
        juce::ToggleButton linkToggle { "Send to Hawthorne (Hawthorne Link)" },
                           indivToggle { "Each drum and mic on its own channel too (18 channels)" },
                           localToggle { "Also play through this PC's audio outputs" },
                           followToggle { "Follow Hawthorne's reel: a THOREAU project per reel" };
        juce::TextEditor nameEditor;
        juce::Label nameLabel { {}, "NAME IN HAWTHORNE" };
    };

    Callbacks callbacks;
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
};

} // namespace thoreau
