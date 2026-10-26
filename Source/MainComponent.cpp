#include "MainComponent.h"
#include "SettingsPanel.h"

namespace thoreau
{

using namespace Pequod;

//==============================================================================
// BpmDisplay

void BpmDisplay::setValue (double bpm, bool fromReel)
{
    if (std::abs (bpm - value) > 1e-6 || fromReel != reel)
    {
        value = bpm;
        reel = fromReel;
        repaint();
    }
}

void BpmDisplay::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colours::black);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (Pequod::Colours::line);
    g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
    g.setColour (Pequod::Colours::mustard);
    g.setFont (heading (r.getHeight() * 0.62f));
    g.drawText (juce::String (value, 1), r.withTrimmedRight (34), juce::Justification::centredRight);
    g.setFont (heading (10.0f));
    g.setColour (Pequod::Colours::textDim);
    g.drawText ("BPM", r.reduced (6, 5), juce::Justification::topRight);
    if (reel)
    {
        g.setColour (Pequod::Colours::mustard);
        g.drawText ("REEL", r.reduced (6, 5), juce::Justification::bottomRight);
    }
}

void BpmDisplay::mouseDown (const juce::MouseEvent&) { dragStart = value; }

void BpmDisplay::mouseDrag (const juce::MouseEvent& e)
{
    const double step = e.mods.isShiftDown() ? 0.02 : 0.2;
    const double v = juce::jlimit (30.0, 300.0, std::round ((dragStart - e.getDistanceFromDragStartY() * step) * 10.0) / 10.0);
    if (v != value && onChange) onChange (v);
}

void BpmDisplay::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const double v = juce::jlimit (30.0, 300.0, value + (w.deltaY > 0 ? 1.0 : -1.0) * (e.mods.isShiftDown() ? 0.1 : 1.0));
    if (onChange) onChange (v);
}

void BpmDisplay::mouseDoubleClick (const juce::MouseEvent&)
{
    editor = std::make_unique<juce::TextEditor>();
    editor->setFont (heading (20.0f));
    editor->setJustification (juce::Justification::centred);
    editor->setText (juce::String (value, 1));
    editor->setInputRestrictions (6, "0123456789.");
    editor->setBounds (getLocalBounds());
    addAndMakeVisible (*editor);
    editor->grabKeyboardFocus();
    editor->selectAll();
    auto finish = [this] (bool apply)
    {
        if (editor == nullptr) return;
        const double v = editor->getText().getDoubleValue();
        auto* ed = editor.release();
        juce::MessageManager::callAsync ([ed] { delete ed; });
        if (apply && v >= 30.0 && v <= 300.0 && onChange) onChange (std::round (v * 10.0) / 10.0);
    };
    editor->onReturnKey = [finish] { finish (true); };
    editor->onEscapeKey = [finish] { finish (false); };
    editor->onFocusLost = [finish] { finish (true); };
}

//==============================================================================
// MainComponent

MainComponent::MainComponent()
{
    juce::PropertiesFile::Options o;
    o.applicationName = "THOREAU";
    o.filenameSuffix = ".settings";
    o.folderName = "PEQUOD/THOREAU";
    o.osxLibrarySubFolder = "Application Support";
    props.setStorageParameters (o);
    auto* s = props.getUserSettings();

    // header
    for (auto* b : { &clockInternal, &clockTape })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (1);
        addAndMakeVisible (*b);
    }
    clockInternal.setTooltip ("THOREAU runs on its own clock: PLAY starts it from bar 1");
    clockTape.setTooltip ("THOREAU follows Hawthorne's tape: the drums play wherever the tape is, at the reel's tempo");
    clockInternal.onClick = [this] { if (clockInternal.getToggleState()) setClock (ClockSource::internal); };
    clockTape.onClick     = [this] { if (clockTape.getToggleState()) setClock (ClockSource::hawthorne); };

    playButton.onClick = [this] { engine.play(); };
    stopButton.onClick = [this] { engine.stop(); };
    recButton.setClickingTogglesState (true);
    recButton.setTooltip ("Live record: pads and MIDI drums you play while it runs go into the pattern");
    recButton.onClick = [this] { engine.setRecording (recButton.getToggleState()); };
    recButton.setColour (juce::TextButton::buttonOnColourId, Pequod::Colours::danger);
    tapButton.setTooltip ("Tap the tempo");
    tapButton.onClick = [this]
    {
        const double now = juce::Time::getMillisecondCounterHiRes();
        if (! taps.empty() && now - taps.back() > 2000.0) taps.clear();
        taps.push_back (now);
        if (taps.size() > 6) taps.erase (taps.begin());
        if (taps.size() >= 3)
            setTempo (std::round (60000.0 / ((taps.back() - taps.front()) / (double) (taps.size() - 1)) * 10.0) / 10.0);
    };
    for (auto* b : { &playButton, &stopButton, &recButton, &tapButton })
        addAndMakeVisible (*b);

    bpmDisplay.setTooltip ("Tempo: drag up / down (shift: fine), scroll, or double-click to type");
    bpmDisplay.onChange = [this] (double v) { setTempo (v); };
    addAndMakeVisible (bpmDisplay);

    for (int b = 2; b <= 12; ++b) bpbBox.addItem (juce::String (b) + "/4", b);
    bpbBox.setTooltip ("Beats per bar");
    bpbBox.onChange = [this]
    {
        project.beatsPerBar = bpbBox.getSelectedId();
        hawthorne::NetSender::TapeClock c;
        if (engine.getClockSource() == ClockSource::hawthorne && freshClock (c) && c.bpm > 0.0)
            engine.netOut.sendTempo (c.bpm, bpbBox.getSelectedId(), c.barOne);
        projectEdited();
    };
    addAndMakeVisible (bpbBox);

    swingKnob.setRange (50.0, 75.0, 0.5);
    swingKnob.setDoubleClickReturnValue (true, 50.0);
    swingKnob.setTooltip ("Swing: delays every second 16th (50 % = straight)");
    swingKnob.onValueChange = [this] { project.swing = (float) swingKnob.getValue(); repaint (swingLabelArea); };
    swingKnob.onDragEnd = [this] { projectEdited(); };
    masterKnob.setRange (-36.0, 6.0, 0.1);
    masterKnob.setSkewFactorFromMidPoint (-6.0);
    masterKnob.setDoubleClickReturnValue (true, 0.0);
    masterKnob.setTooltip ("Master level");
    masterKnob.onValueChange = [this] { project.masterDb = (float) masterKnob.getValue(); repaint (masterLabelArea); };
    masterKnob.onDragEnd = [this] { projectEdited(); };
    addAndMakeVisible (swingKnob);
    addAndMakeVisible (masterKnob);

    for (auto* b : { &modePattern, &modeSong })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (2);
        addAndMakeVisible (*b);
    }
    modePattern.setTooltip ("Loop the pattern you pick (a new pick takes over at the end of the bar)");
    modeSong.setTooltip ("Play the song chain");
    modePattern.onClick = [this] { if (modePattern.getToggleState()) setMode (PlayMode::pattern); };
    modeSong.onClick    = [this] { if (modeSong.getToggleState()) setMode (PlayMode::song); };

    fileButton.onClick = [this] { showFileMenu(); };
    settingsButton.onClick = [this] { showSettings(); };
    updateButton.setColour (juce::TextButton::buttonColourId, Pequod::Colours::mustard);
    updateButton.setColour (juce::TextButton::textColourOffId, Pequod::Colours::bg);
    updateButton.onClick = [this]
    {
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::InfoIcon, "THOREAU " + updater.getLatestVersion(),
            updater.getReleaseNotes() + "\n\nInstall it now? THOREAU closes, updates and starts again.", "INSTALL", "LATER", this,
            juce::ModalCallbackFunction::create ([this] (int r) { if (r == 1) { saveProject(); updater.install(); } }));
    };
    addChildComponent (updateButton);
    for (auto* b : { &fileButton, &settingsButton })
        addAndMakeVisible (*b);

    // pattern bar
    for (int p = 0; p < kPatterns; ++p)
    {
        auto* b = patternButtons.add (new juce::TextButton (patternName (p)));
        b->setTooltip ("Pattern " + patternName (p) + " - click to edit and play it");
        b->onClick = [this, p] { selectPattern (p); };
        addAndMakeVisible (b);
    }
    for (int l : { 4, 8, 12, 16, 24, 32, 48, 64 }) lengthBox.addItem (juce::String (l) + " STEPS", l);
    for (int l = 1; l <= kMaxSteps; ++l) if (lengthBox.indexOfItemId (l) < 0) lengthBox.addItem (juce::String (l) + " STEPS", l);
    lengthBox.setTooltip ("Pattern length");
    lengthBox.onChange = [this] { project.patterns[(size_t) editPattern].length = juce::jmax (1, lengthBox.getSelectedId()); grid.repaint(); projectEdited(); };
    resBox.addItem ("1/8", 2); resBox.addItem ("1/8 T", 3); resBox.addItem ("1/16", 4); resBox.addItem ("1/16 T", 6); resBox.addItem ("1/32", 8);
    resBox.setTooltip ("Step size");
    resBox.onChange = [this] { project.patterns[(size_t) editPattern].stepsPerBeat = juce::jmax (1, resBox.getSelectedId()); grid.repaint(); projectEdited(); };
    copyButton.onClick = [this] { clipboard = std::make_unique<Pattern>(); clipboard->copyFrom (project.patterns[(size_t) editPattern]); message ("copied pattern " + patternName (editPattern)); };
    pasteButton.onClick = [this]
    {
        if (clipboard == nullptr) return;
        project.patterns[(size_t) editPattern].copyFrom (*clipboard);
        refreshPatternBar(); grid.repaint(); projectEdited();
        message ("pasted into pattern " + patternName (editPattern));
    };
    clearButton.onClick = [this]
    {
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Clear pattern " + patternName (editPattern) + "?",
            "Every step of every drum in this pattern is turned off.", "CLEAR", "CANCEL", this,
            juce::ModalCallbackFunction::create ([this] (int r)
            {
                if (r != 1) return;
                project.patterns[(size_t) editPattern].clear();
                refreshPatternBar(); grid.repaint(); projectEdited();
            }));
    };
    for (juce::Component* c : { (juce::Component*) &lengthBox, (juce::Component*) &resBox, (juce::Component*) &copyButton,
                                (juce::Component*) &pasteButton, (juce::Component*) &clearButton })
        addAndMakeVisible (c);

    grid.onTrackSelected = [this] (int t) { voice.setTrack (t); };
    grid.onEdited = [this] { projectEdited(); refreshPatternBar(); };
    voice.onEdited = [this] { projectEdited(); grid.repaint(); };
    voice.onMessage = [this] (const juce::String& m) { message (m); };
    song.onEdited = [this] { projectEdited(); };
    song.onEditPattern = [this] (int p) { selectPattern (p); };
    kitPanel.onKitChanged = [this]
    {
        voice.refresh(); grid.repaint(); refreshPatternBar();
        message ("kit: " + kitAt (project.kit).name + " - " + kitAt (project.kit).description);
    };
    kitPanel.onEdited = [this] { projectEdited(); };
    addAndMakeVisible (grid);
    addAndMakeVisible (voice);
    addAndMakeVisible (song);
    addAndMakeVisible (kitPanel);

    barOneButton.setTooltip ("Bar 1 starts where the tape is now (stop the tape at the downbeat first)");
    barOneButton.onClick = [this] { setBarOne (engine.getTapePosition()); };
    nudgeEarly.setTooltip ("Move bar 1 - and every beat - 1 ms earlier on the tape");
    nudgeLate.setTooltip ("Move bar 1 - and every beat - 1 ms later on the tape");
    nudgeEarly.onClick = [this] { hawthorne::NetSender::TapeClock c; if (freshClock (c) && c.bpm > 0) setBarOne (c.barOne - 0.001 * engine.getSampleRate()); };
    nudgeLate.onClick  = [this] { hawthorne::NetSender::TapeClock c; if (freshClock (c) && c.bpm > 0) setBarOne (c.barOne + 0.001 * engine.getSampleRate()); };
    for (auto* b : { &barOneButton, &nudgeEarly, &nudgeLate })
        addChildComponent (*b);

    // settings
    followReelOn = s->getBoolValue ("followReel", true);
    engine.setIndividualOuts (s->getBoolValue ("individualOuts", false));
    engine.setLocalOutput (s->getBoolValue ("localOut", true));
    engine.netOut.setName (s->getValue ("linkName", "THOREAU"));

    // project: the last one, or a fresh starter kit
    projectFile = juce::File (s->getValue ("lastProject"));
    if (projectFile.existsAsFile()) loadProject (projectFile);
    else { projectFile = projectsFolder().getChildFile ("Untitled.thoreau"); afterProjectLoad(); }

    initialiseAudio();
    if (s->getBoolValue ("link", true))
        setLink (true, engine.netOut.getName());

    setClock (s->getValue ("clock", "internal") == "hawthorne" ? ClockSource::hawthorne : ClockSource::internal);
    setMode (PlayMode::pattern);
    selectPattern (0);

    updater.onChange = [this] { updateButton.setVisible (updater.isUpdateAvailable()); updateButton.setButtonText (updater.isDownloading() ? "DOWNLOADING " + juce::String (juce::roundToInt (updater.getProgress() * 100)) + "%" : "UPDATE " + updater.getLatestVersion()); resized(); };
    updater.onStatus = [this] (const juce::String& m) { message ("!! " + m); };
    updater.checkSoon();

    setWantsKeyboardFocus (true);
    message ("THOREAU " + Updater::currentVersion() + " ready");
    setSize (1400, 860);
    startTimerHz (30);
}

MainComponent::~MainComponent()
{
    stopTimer();
    saveProject();
    auto* s = props.getUserSettings();
    if (auto state = deviceManager.createStateXml())
        s->setValue ("audio", state.get());
    props.saveIfNeeded();
    deviceManager.removeMidiInputDeviceCallback ({}, &engine);
    deviceManager.removeAudioCallback (&engine);
    deviceManager.closeAudioDevice();
}

//==============================================================================
// Audio

void MainComponent::initialiseAudio()
{
    auto saved = props.getUserSettings()->getXmlValue ("audio");
    const auto err = deviceManager.initialise (0, 2, saved.get(), true);
    if (err.isNotEmpty()) message ("!! audio: " + err);
    deviceManager.addAudioCallback (&engine);
    // every enabled MIDI input plays the drums
    for (auto& d : juce::MidiInput::getAvailableDevices())
        if (saved == nullptr) deviceManager.setMidiInputDeviceEnabled (d.identifier, true);
    deviceManager.addMidiInputDeviceCallback ({}, &engine);
}

void MainComponent::restartAudio()
{
    deviceManager.removeAudioCallback (&engine);
    deviceManager.addAudioCallback (&engine);     // runs audioDeviceAboutToStart again: new channel layout
}

void MainComponent::setLink (bool on, const juce::String& name)
{
    juce::String err;
    engine.netOut.setName (name.trim().isNotEmpty() ? name.trim() : juce::String ("THOREAU"));
    if (! engine.setNetworkEnabled (on, engine.netOut.getName(), err))
        message ("!! Hawthorne Link: " + err);
    else
        message (on ? "Hawthorne Link on: \"" + engine.netOut.getName() + "\"" : juce::String ("Hawthorne Link off"));
    auto* s = props.getUserSettings();
    s->setValue ("link", on);
    s->setValue ("linkName", engine.netOut.getName());
}

void MainComponent::showSettings()
{
    if (settingsWindow != nullptr) { settingsWindow->toFront (true); return; }

    SettingsPanel::Callbacks cb;
    cb.setLink = [this] (bool on, const juce::String& n) { setLink (on, n); };
    cb.setIndividualOuts = [this] (bool on)
    {
        engine.setIndividualOuts (on);
        props.getUserSettings()->setValue ("individualOuts", on);
        restartAudio();
        message (on ? "Hawthorne gets 14 channels: mix + each drum" : "Hawthorne gets the stereo mix");
    };
    cb.setLocalOut = [this] (bool on) { engine.setLocalOutput (on); props.getUserSettings()->setValue ("localOut", on); };
    cb.setFollowReel = [this] (bool on) { followReelOn = on; props.getUserSettings()->setValue ("followReel", on); };

    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (new SettingsPanel (deviceManager, engine, followReelOn, std::move (cb)));
    o.dialogTitle = "THOREAU settings";
    o.dialogBackgroundColour = Pequod::Colours::panel;
    o.useNativeTitleBar = true;
    o.resizable = true;
    o.componentToCentreAround = this;
    settingsWindow = o.launchAsync();
}

//==============================================================================
// Project files

juce::File MainComponent::projectsFolder() const
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("THOREAU");
}

juce::File MainComponent::reelsFolder() const
{
    return projectsFolder().getChildFile ("Reels");
}

void MainComponent::loadProject (const juce::File& f)
{
    juce::String err;
    const bool ok = project.load (f, err);
    if (! ok) { message ("!! " + err); return; }
    if (err.isNotEmpty()) message ("!! " + err.replace ("\n", "  "));
    projectFile = f;
    afterProjectLoad();
    message ("opened " + f.getFileNameWithoutExtension());
}

void MainComponent::afterProjectLoad()
{
    props.getUserSettings()->setValue ("lastProject", projectFile.getFullPathName());
    swingKnob.setValue (project.swing.load(), juce::dontSendNotification);
    masterKnob.setValue (project.masterDb.load(), juce::dontSendNotification);
    bpbBox.setSelectedId (project.beatsPerBar.load(), juce::dontSendNotification);
    engine.kitChanged();
    kitPanel.refresh();
    voice.refresh();
    song.refresh();
    refreshPatternBar();
    grid.repaint();
    seenVersion = project.version.load();
    saveDueMs = 0.0;
    repaint();
}

void MainComponent::saveProject()
{
    if (projectFile == juce::File()) return;
    juce::String err;
    if (! project.save (projectFile, err)) message ("!! " + err);
    saveDueMs = 0.0;
}

void MainComponent::projectEdited()
{
    project.changed();
}

void MainComponent::showFileMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader (projectFile.getFileNameWithoutExtension());
    m.addItem ("New (starter kit)", [this]
    {
        saveProject();
        project.resetToDefault();
        projectFile = projectsFolder().getChildFile ("Untitled.thoreau").getNonexistentSibling();
        afterProjectLoad();
        saveProject();
        message ("new project " + projectFile.getFileNameWithoutExtension());
    });
    m.addItem ("Put " + kitAt (project.kit).name + "'s starter beat in A and fill in B", [this]
    {
        project.loadGroove (project.kit, 0, 1);
        afterProjectLoad();
        selectPattern (0);
        message ("starter beat for " + kitAt (project.kit).name + " in A, fill in B");
    });
    m.addItem ("Open...", [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Open a THOREAU project", projectsFolder(), "*.thoreau");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
        {
            if (fc.getResult().existsAsFile()) { saveProject(); loadProject (fc.getResult()); }
        });
    });
    m.addItem ("Save as...", [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Save the THOREAU project", projectFile, "*.thoreau");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this] (const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (f == juce::File()) return;
            projectFile = f.withFileExtension (".thoreau");
            saveProject();
            props.getUserSettings()->setValue ("lastProject", projectFile.getFullPathName());
            message ("saved as " + projectFile.getFileNameWithoutExtension());
        });
    });
    m.addSeparator();
    m.addItem ("Show projects folder", [this] { projectsFolder().createDirectory(); projectsFolder().startAsProcess(); });
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&fileButton));
}

void MainComponent::followReel()
{
    if (! followReelOn || engine.getClockSource() != ClockSource::hawthorne) return;
    hawthorne::NetSender::TapeClock c;
    if (! freshClock (c)) return;
    const auto reel = engine.netOut.getReelName();
    if (reel.isEmpty() || reel == currentReel) return;

    currentReel = reel;
    const auto file = reelsFolder().getChildFile (juce::File::createLegalFileName (reel) + ".thoreau");
    if (file == projectFile) return;
    saveProject();
    if (file.existsAsFile())
        loadProject (file);
    else
    {
        projectFile = file;           // this reel starts from what's loaded now
        saveProject();
        afterProjectLoad();
        message ("reel \"" + reel + "\": new THOREAU project (from what was loaded)");
    }
}

//==============================================================================
// Transport and tempo

void MainComponent::setClock (ClockSource c)
{
    engine.setClockSource (c);
    clockInternal.setToggleState (c == ClockSource::internal, juce::dontSendNotification);
    clockTape.setToggleState (c == ClockSource::hawthorne, juce::dontSendNotification);
    playButton.setEnabled (c == ClockSource::internal);
    stopButton.setEnabled (c == ClockSource::internal);
    for (auto* b : { &barOneButton, &nudgeEarly, &nudgeLate }) b->setVisible (c == ClockSource::hawthorne);
    props.getUserSettings()->setValue ("clock", c == ClockSource::hawthorne ? "hawthorne" : "internal");
    if (c == ClockSource::hawthorne && ! engine.isNetworkEnabled())
        setLink (true, engine.netOut.getName());
    currentReel.clear();
    message (c == ClockSource::hawthorne ? "following Hawthorne's tape" : "on THOREAU's own clock");
    repaint();
}

void MainComponent::setMode (PlayMode m)
{
    engine.setPlayMode (m);
    modePattern.setToggleState (m == PlayMode::pattern, juce::dontSendNotification);
    modeSong.setToggleState (m == PlayMode::song, juce::dontSendNotification);
    song.repaint();
}

bool MainComponent::freshClock (hawthorne::NetSender::TapeClock& c) const
{
    return engine.isNetworkEnabled() && engine.netOut.getTapeClock (c)
        && juce::Time::getMillisecondCounter() - c.receivedMs < TapeChase::staleMs
        && c.session == engine.netOut.getSession();
}

void MainComponent::setTempo (double bpm)
{
    bpm = juce::jlimit (30.0, 300.0, bpm);
    hawthorne::NetSender::TapeClock c;
    if (engine.getClockSource() == ClockSource::hawthorne && freshClock (c) && (c.flags & 4) != 0)
    {
        pendingTempo = bpm;             // sent (throttled) by the timer; the reel keeps it
        tempoEditMs = juce::Time::getMillisecondCounterHiRes();
    }
    project.bpm = bpm;
    bpmDisplay.setValue (bpm, engine.isReelTempo());
    projectEdited();
}

void MainComponent::setBarOne (double tapePos)
{
    hawthorne::NetSender::TapeClock c;
    if (! freshClock (c) || (c.flags & 4) == 0)
    {
        message ("!! not synced to Hawthorne - connect THOREAU in Hawthorne's NETWORK window");
        return;
    }
    const double bpm = c.bpm > 0 ? c.bpm : project.bpm.load();
    const int bpb = c.bpm > 0 ? c.beatsPerBar : project.beatsPerBar.load();
    engine.netOut.sendTempo (bpm, bpb, std::round (juce::jmax (0.0, tapePos)));
    message ("bar 1 at " + formatTape (tapePos) + " on the tape");
}

void MainComponent::handleTapeTempo()
{
    if (engine.getClockSource() != ClockSource::hawthorne) return;
    hawthorne::NetSender::TapeClock c;
    if (! freshClock (c) || (c.flags & 4) == 0) return;
    const double now = juce::Time::getMillisecondCounterHiRes();

    if (pendingTempo > 0.0 && now - lastTempoSendMs > 60.0)
    {
        engine.netOut.sendTempo (pendingTempo, c.bpm > 0 ? c.beatsPerBar : project.beatsPerBar.load(), c.bpm > 0 ? c.barOne : 0.0);
        lastTempoSendMs = now;
        if (std::abs (c.bpm - pendingTempo) < 1e-6) pendingTempo = 0.0;      // the reel has it
        return;
    }

    if (c.bpm <= 0.0)
    {
        // a reel with no tempo yet: put ours on it (bar 1 at the start of the tape)
        if (now - lastStripeMs > 1000.0)
        {
            lastStripeMs = now;
            engine.netOut.sendTempo (project.bpm.load(), project.beatsPerBar.load(), 0.0);
            message ("putting " + juce::String (project.bpm.load(), 1) + " BPM on reel \"" + engine.netOut.getReelName() + "\"");
        }
        return;
    }

    // the reel's tempo is the tempo: keep the project in step (so it saves the same)
    if (now - tempoEditMs > 800.0 && (project.bpm.load() != c.bpm || project.beatsPerBar.load() != c.beatsPerBar))
    {
        project.bpm = c.bpm;
        project.beatsPerBar = c.beatsPerBar;
        bpbBox.setSelectedId (c.beatsPerBar, juce::dontSendNotification);
        projectEdited();
    }
}

//==============================================================================
// Patterns

void MainComponent::selectPattern (int p)
{
    editPattern = juce::jlimit (0, kPatterns - 1, p);
    grid.setPattern (editPattern);
    song.setCurrentPattern (editPattern);
    if (engine.getPlayMode() == PlayMode::pattern)
        engine.selectPattern (editPattern);
    refreshPatternBar();
}

void MainComponent::refreshPatternBar()
{
    const auto& pt = project.patterns[(size_t) editPattern];
    lengthBox.setSelectedId (pt.length.load(), juce::dontSendNotification);
    resBox.setSelectedId (pt.stepsPerBeat.load(), juce::dontSendNotification);
    const int playing = engine.isRolling() ? engine.getPlayingPattern() : -1;
    const int queued = engine.getQueuedPattern();
    for (int i = 0; i < kPatterns; ++i)
    {
        auto* b = patternButtons[i];
        const bool empty = project.patterns[(size_t) i].isEmpty();
        b->setToggleState (i == editPattern, juce::dontSendNotification);
        b->setColour (juce::TextButton::textColourOffId,
                      i == playing ? Pequod::Colours::mustard : i == queued ? Pequod::Colours::mustard.withAlpha (0.6f) : empty ? Pequod::Colours::textDim : Pequod::Colours::text);
        b->setColour (juce::TextButton::buttonColourId, i == playing && i != editPattern ? Pequod::Colours::panelHi.brighter (0.15f) : Pequod::Colours::panelHi);
    }
}

//==============================================================================

void MainComponent::message (const juce::String& m)
{
    log.add (juce::Time::getCurrentTime().toString (false, true, true, true) + "  > " + m);
    while (log.size() > 50) log.remove (0);
    repaint (statusArea);
}

juce::String MainComponent::formatTape (double samples) const
{
    const double sr = juce::jmax (1.0, engine.getSampleRate());
    const double secs = samples / sr;
    const bool neg = secs < 0;
    const double a = std::abs (secs);
    const int h = (int) (a / 3600), m = (int) std::fmod (a / 60, 60), s = (int) std::fmod (a, 60);
    const int ms = (int) std::fmod (a * 1000.0, 1000.0);
    return (neg ? "-" : "") + juce::String (h).paddedLeft ('0', 2) + ":" + juce::String (m).paddedLeft ('0', 2) + ":"
         + juce::String (s).paddedLeft ('0', 2) + "." + juce::String (ms).paddedLeft ('0', 3);
}

juce::String MainComponent::counterText() const
{
    const double beat = engine.getBeat();
    const int bpb = juce::jmax (1, engine.getBeatsPerBar());
    const auto b = (juce::int64) std::floor (beat + 1.0e-9);
    auto bar = b >= 0 ? b / bpb : -((-b + bpb - 1) / bpb);
    const int beatIn = (int) (b - bar * bpb) + 1;
    const int sixteenth = (int) std::floor ((beat - std::floor (beat)) * 4.0) + 1;
    const auto barNum = bar >= 0 ? bar + 1 : bar;
    return juce::String (barNum).paddedLeft (' ', 3) + "." + juce::String (beatIn) + "." + juce::String (sixteenth);
}

void MainComponent::timerCallback()
{
    grid.tick();
    song.tick();
    handleTapeTempo();
    followReel();
    project.collectGarbage();

    // live-recorded hits go into their patterns
    DrumEngine::RecordedHit h;
    bool rec = false;
    while (engine.popRecordedHit (h))
    {
        auto& cell = project.patterns[(size_t) h.pattern].steps[(size_t) h.track][(size_t) h.step];
        cell = (uint8_t) juce::jmax ((int) cell.load(), h.value);
        rec = true;
    }
    if (rec) { projectEdited(); grid.repaint(); refreshPatternBar(); }

    // autosave two seconds after the last change
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (project.version.load() != seenVersion)
    {
        seenVersion = project.version.load();
        saveDueMs = now + 2000.0;
        song.refresh();
    }
    if (saveDueMs > 0.0 && now >= saveDueMs) saveProject();

    bpmDisplay.setValue (engine.getClockSource() == ClockSource::hawthorne && engine.isReelTempo() && pendingTempo == 0.0
                             ? engine.getBpm() : project.bpm.load(),
                         engine.isReelTempo());
    playButton.setToggleState (engine.isRolling() && engine.getClockSource() == ClockSource::internal, juce::dontSendNotification);

    static int frame = 0;
    if (++frame % 6 == 0) refreshPatternBar();
    repaint (counterArea);
    if (frame % 10 == 0) repaint (syncArea);
}

bool MainComponent::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::spaceKey && engine.getClockSource() == ClockSource::internal)
    {
        if (engine.isRolling()) engine.stop(); else engine.play();
        return true;
    }
    return false;
}

//==============================================================================
// Layout and drawing

void MainComponent::resized()
{
    auto r = getLocalBounds();
    headerArea = r.removeFromTop (72);
    statusArea = r.removeFromBottom (26);

    auto h = headerArea.reduced (16, 12);
    settingsButton.setBounds (h.removeFromRight (100).reduced (2, 6));
    fileButton.setBounds (h.removeFromRight (64).reduced (2, 6));
    if (updateButton.isVisible()) updateButton.setBounds (h.removeFromRight (160).reduced (2, 6));
    h.removeFromRight (6);
    logoArea = h.removeFromLeft (h.getWidth() > 1300 ? 210 : 150);
    auto clock = h.removeFromLeft (212);
    clockInternal.setBounds (clock.removeFromLeft (100).reduced (2, 6));
    clockTape.setBounds (clock.removeFromLeft (112).reduced (2, 6));
    h.removeFromLeft (8);
    playButton.setBounds (h.removeFromLeft (58).reduced (2, 6));
    stopButton.setBounds (h.removeFromLeft (58).reduced (2, 6));
    recButton.setBounds (h.removeFromLeft (52).reduced (2, 6));
    h.removeFromLeft (12);
    counterArea = h.removeFromLeft (150).reduced (0, 2);
    h.removeFromLeft (10);
    bpmDisplay.setBounds (h.removeFromLeft (130).reduced (0, 2));
    h.removeFromLeft (4);
    auto tempoCol = h.removeFromLeft (70);
    tapButton.setBounds (tempoCol.removeFromTop (tempoCol.getHeight() / 2).reduced (2, 1));
    bpbBox.setBounds (tempoCol.reduced (2, 1));
    h.removeFromLeft (8);
    auto sw = h.removeFromLeft (54);
    swingLabelArea = sw.removeFromBottom (14);
    swingKnob.setBounds (sw);
    auto mk = h.removeFromLeft (54);
    masterLabelArea = mk.removeFromBottom (14);
    masterKnob.setBounds (mk);
    h.removeFromLeft (10);
    modePattern.setBounds (h.removeFromLeft (88).reduced (2, 6));
    modeSong.setBounds (h.removeFromLeft (68).reduced (2, 6));

    auto body = r.reduced (12, 8);
    auto bottom = body.removeFromBottom (170);
    auto side = body.removeFromRight (340);
    body.removeFromRight (10);

    patternBarArea = body.removeFromTop (40);
    {
        auto pb = patternBarArea;
        auto right = pb.removeFromRight (430);
        const int w = juce::jmin (36, pb.getWidth() / kPatterns);
        for (int i = 0; i < kPatterns; ++i)
            patternButtons[i]->setBounds (pb.removeFromLeft (w).reduced (2, 5));
        right.removeFromLeft (8);
        lengthBox.setBounds (right.removeFromLeft (110).reduced (2, 6));
        resBox.setBounds (right.removeFromLeft (80).reduced (2, 6));
        copyButton.setBounds (right.removeFromLeft (76).reduced (2, 6));
        pasteButton.setBounds (right.removeFromLeft (76).reduced (2, 6));
        clearButton.setBounds (right.removeFromLeft (76).reduced (2, 6));
    }
    body.removeFromTop (4);
    gridArea = body;
    grid.setBounds (body);

    syncArea = side.removeFromTop (240);
    side.removeFromTop (10);
    song.setBounds (side);
    {
        auto b = syncArea.reduced (24, 12).removeFromBottom (28);
        barOneButton.setBounds (b.removeFromLeft (b.getWidth() - 150).reduced (2, 0));
        nudgeEarly.setBounds (b.removeFromLeft (75).reduced (2, 0));
        nudgeLate.setBounds (b.reduced (2, 0));
    }

    bottom.removeFromTop (10);
    kitPanel.setBounds (bottom.removeFromRight (side.getWidth()));
    bottom.removeFromRight (10);
    voice.setBounds (bottom);
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (Pequod::Colours::bg);

    // header
    g.setColour (Pequod::Colours::panel);
    g.fillRect (headerArea);
    g.setColour (Pequod::Colours::mustard);
    g.fillRect (headerArea.withTop (headerArea.getBottom() - 2));
    {
        auto l = logoArea;
        g.setFont (heading (30.0f));
        g.setColour (Pequod::Colours::mustard);
        g.drawText ("THOREAU", l.removeFromTop (34), juce::Justification::bottomLeft);
        g.setFont (heading (11.0f, false));
        g.setColour (Pequod::Colours::textDim);
        g.drawText ("DRUM MACHINE", l, juce::Justification::topLeft);
    }

    // counter
    {
        auto c = counterArea.toFloat();
        g.setColour (juce::Colours::black);
        g.fillRoundedRectangle (c, 4.0f);
        g.setColour (Pequod::Colours::line);
        g.drawRoundedRectangle (c.reduced (0.5f), 4.0f, 1.0f);
        const bool tape = engine.getClockSource() == ClockSource::hawthorne;
        const bool rolling = engine.isRolling();
        g.setColour (engine.isTapeRecording() ? Pequod::Colours::danger : Pequod::Colours::mustard);
        g.setFont (heading (c.getHeight() * 0.5f));
        g.drawText (tape && ! engine.isTapeSynced() ? juce::String ("--.-.-") : counterText(), c.withTrimmedTop (8), juce::Justification::centred);
        g.setFont (heading (10.0f));
        g.setColour (rolling ? Pequod::Colours::mustard : Pequod::Colours::textDim);
        const juce::String state = tape ? (! engine.isTapeSynced() ? "NO TAPE" : engine.isTapeRecording() ? "TAPE REC" : rolling ? "TAPE PLAY" : "TAPE STOP")
                                        : (rolling ? "PLAY" : "STOP");
        g.drawText (state, c.reduced (7, 4), juce::Justification::topLeft);
        g.drawText ("BAR.BEAT", c.reduced (7, 4), juce::Justification::topRight);
    }

    g.setFont (heading (10.0f));
    g.setColour (Pequod::Colours::textDim);
    g.drawText ("SWING " + juce::String (juce::roundToInt (project.swing.load())), swingLabelArea, juce::Justification::centred);
    g.drawText ("OUT " + juce::String (project.masterDb.load(), 1), masterLabelArea, juce::Justification::centred);

    // pattern bar caption
    g.setColour (Pequod::Colours::textDim);
    g.setFont (heading (11.0f));

    // sync panel
    {
        const bool tape = engine.getClockSource() == ClockSource::hawthorne;
        drawModulePanel (g, syncArea.toFloat(), tape);
        auto r = syncArea.reduced (24, 12);
        g.setColour (Pequod::Colours::mustard);
        g.setFont (heading (16.0f));
        g.drawText ("HAWTHORNE", r.removeFromTop (24), juce::Justification::centredLeft);
        r.removeFromTop (4);

        juce::StringArray lines;
        const auto subs = engine.netOut.getSubscribers();
        if (! engine.isNetworkEnabled())
            lines.add ("> link OFF (SETTINGS > HAWTHORNE)");
        else
            lines.add ("> \"" + engine.netOut.getName() + "\"  " + juce::String (engine.getNetworkChannels()) + " ch  -> "
                       + (subs.empty() ? juce::String ("waiting for Hawthorne") : juce::String ((int) subs.size()) + " receiver" + (subs.size() == 1 ? "" : "s")));

        hawthorne::NetSender::TapeClock c;
        const bool fresh = freshClock (c);
        if (! tape)
            lines.add ("> clock INTERNAL - choose HAWTHORNE to follow the tape");
        else if (! fresh)
        {
            lines.add ("> no tape position from Hawthorne yet");
            lines.add (subs.empty() ? "> connect THOREAU in Hawthorne's NETWORK window" : "> check both run at the same sample rate");
        }
        else
        {
            const auto reel = engine.netOut.getReelName();
            lines.add ("> reel  " + (reel.isNotEmpty() ? "\"" + reel + "\"" : juce::String ("(none on the deck)")));
            lines.add ("> tape  " + formatTape (engine.getTapePosition()) + "  "
                       + ((c.flags & 2) ? "RECORDING" : (c.flags & 1) ? "PLAYING" : "STOPPED")
                       + (std::abs (c.tapePerFrame - 1.0) > 1e-4 && c.tapePerFrame > 0 ? "  x" + juce::String (c.tapePerFrame, 3) : juce::String()));
            if (c.bpm > 0)
                lines.add ("> tempo " + juce::String (c.bpm, 1) + " BPM " + juce::String (c.beatsPerBar) + "/4 on the reel, bar 1 at "
                           + formatTape (c.barOne));
            else
                lines.add ("> reel has no tempo yet - putting ours on it");
            if (followReelOn && reel.isNotEmpty())
                lines.add ("> project: " + projectFile.getFileNameWithoutExtension());
        }

        g.setFont (heading (12.5f, false));
        for (auto& l : lines)
        {
            g.setColour (l.containsIgnoreCase ("no tape") || l.containsIgnoreCase ("check") ? Pequod::Colours::danger.brighter (0.2f) : Pequod::Colours::text);
            g.drawFittedText (l, r.removeFromTop (20), juce::Justification::centredLeft, 1);
        }
    }

    // status bar
    g.setColour (juce::Colours::black);
    g.fillRect (statusArea);
    const auto last = log.isEmpty() ? juce::String() : log[log.size() - 1];
    g.setColour (last.contains ("!!") ? Pequod::Colours::danger : Pequod::Colours::textDim);
    g.setFont (heading (12.5f, false));
    g.drawText (last, statusArea.reduced (12, 0), juce::Justification::centredLeft, true);
    g.drawText (projectFile.getFileNameWithoutExtension() + "   v" + Updater::currentVersion(), statusArea.reduced (12, 0), juce::Justification::centredRight);
}

} // namespace thoreau
