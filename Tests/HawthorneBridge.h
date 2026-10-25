#pragma once

#include <juce_core/juce_core.h>
#include <memory>

/** Hawthorne's real network receiver (from a Hawthorne checkout), behind an interface
    that doesn't drag its copy of the protocol header into the tests. */
class HawthorneSim
{
public:
    HawthorneSim();
    ~HawthorneSim();

    bool start (juce::String& error);
    void stop();
    void addManualAddress (const juce::String& ip);
    void connect (const juce::String& senderName);

    // audio thread
    void prepare (double sampleRate);
    void process (int n);                      // n <= 256
    const float* channel (int ch) const;       // nullptr while not live
    bool isLive() const;
    void publishTape (double tapePosAtBlockStart, double tapePerSample, bool rolling, bool recording);

    // message thread
    void setReel (const juce::String& name, double bpm, int beatsPerBar, double barOne);
    bool popTempo (double& bpm, int& beatsPerBar, double& barOne);
    bool isSyncing() const;
    juce::String describe() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
