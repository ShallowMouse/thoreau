#pragma once

#include <JuceHeader.h>
#include "ThoreauLookAndFeel.h"
#include "DrumEngine.h"
#include "StepGrid.h"
#include "VoicePanel.h"
#include "SongPanel.h"
#include "KitPanel.h"
#include "Updater.h"

namespace thoreau
{

//==============================================================================
/** Big tempo readout: drag up/down to change (shift = fine), double-click to type. */
class BpmDisplay : public juce::Component, public juce::SettableTooltipClient
{
public:
    std::function<void (double)> onChange;
    void setValue (double bpm, bool fromReel);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    double value = 120.0, dragStart = 120.0;
    bool reel = false;
    std::unique_ptr<juce::TextEditor> editor;
};

//==============================================================================
class MainComponent : public juce::Component,
                      private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void timerCallback() override;

    // audio / settings
    void initialiseAudio();
    void restartAudio();
    void showSettings();
    void setLink (bool on, const juce::String& name);

    // project
    juce::File projectsFolder() const;
    juce::File reelsFolder() const;
    void loadProject (const juce::File&);
    void saveProject();
    void projectEdited();
    void showFileMenu();
    void afterProjectLoad();
    void followReel();

    // transport / tempo
    void setClock (ClockSource);
    void setMode (PlayMode);
    void setTempo (double bpm);
    void setBarOne (double tapePos);
    void handleTapeTempo();
    bool freshClock (hawthorne::NetSender::TapeClock&) const;

    // pattern
    void selectPattern (int p);
    void refreshPatternBar();

    void message (const juce::String&);
    juce::String counterText() const;
    juce::String formatTape (double samples) const;

    juce::ApplicationProperties props;
    Project project;
    DrumEngine engine { project };
    juce::AudioDeviceManager deviceManager;
    Updater updater;
    juce::TooltipWindow tooltips { this, 700 };

    // header
    juce::TextButton clockInternal { "INTERNAL" }, clockTape { "HAWTHORNE" },
                     playButton { "PLAY" }, stopButton { "STOP" }, recButton { "REC" }, tapButton { "TAP" },
                     modePattern { "PATTERN" }, modeSong { "SONG" },
                     fileButton { "FILE" }, settingsButton { "SETTINGS" }, updateButton { "UPDATE" };
    BpmDisplay bpmDisplay;
    juce::ComboBox bpbBox;
    juce::Slider swingKnob { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox },
                 masterKnob { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };

    // pattern bar
    juce::OwnedArray<juce::TextButton> patternButtons;
    juce::ComboBox lengthBox, resBox;
    juce::TextButton copyButton { "COPY" }, pasteButton { "PASTE" }, clearButton { "CLEAR" };
    std::unique_ptr<Pattern> clipboard;

    StepGrid grid { engine };
    VoicePanel voice { engine };
    SongPanel song { engine };
    KitPanel kitPanel { engine };

    // sync panel
    juce::TextButton barOneButton { "BAR 1 = TAPE POSITION" }, nudgeEarly { "< 1 ms" }, nudgeLate { "1 ms >" };

    juce::Rectangle<int> headerArea, logoArea, counterArea, patternBarArea, gridArea, syncArea, statusArea,
                         swingLabelArea, masterLabelArea, tempoLabelArea;

    // state
    int editPattern = 0;
    juce::File projectFile;
    juce::String currentReel;
    bool followReelOn = true;
    double saveDueMs = 0.0, lastStripeMs = 0.0, lastTempoSendMs = 0.0, pendingTempo = 0.0, tempoEditMs = 0.0;
    int seenVersion = -1;
    juce::StringArray log;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Component::SafePointer<juce::DialogWindow> settingsWindow;
    std::vector<double> taps;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace thoreau
