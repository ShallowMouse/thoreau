#pragma once

#include "HawthorneLink/NetSender.h"

namespace thoreau
{

//==============================================================================
/**
    Locks THOREAU to Hawthorne's tape, like a drum machine chasing tape sync.

    Hawthorne tells us (Hawthorne Sync POS messages) that our audio frame F lands on
    tape at  tapePos + (F - senderFrame) * tapePerFrame.  Every audio block, this turns
    the frames we are about to render into a span of beats (from the reel's tempo and
    the tape position of bar 1), so hits are rendered exactly where they belong on tape -
    however late the audio arrives at Hawthorne.
*/
class TapeChase
{
public:
    struct Tempo { double bpm = 120.0; int beatsPerBar = 4; double barOne = 0.0; };

    struct Span
    {
        bool rolling = false;       // tape is moving and we are synced
        bool synced = false;        // fresh POS messages for our current stream
        bool recording = false;
        bool jumped = false;        // started, located or tempo changed: nothing carries over
        double beat0 = 0.0;         // beat (from bar 1 = 0) at the first sample of the block
        double beat1 = 0.0;         // ... at the sample after the block
        double scanFrom = 0.0;      // events from here (= where the previous block ended, unless jumped)
        double tapePos0 = 0.0;      // tape position of the first sample
        double tapePerSample = 0.0;
        Tempo tempo;                // what was used (the reel's if it has one)
        bool reelTempo = false;
    };

    /** Audio thread, once per block.
        frame0 = our stream's frame number of the block's first sample; nowMs = Time::getMillisecondCounter(). */
    Span advance (const hawthorne::NetSender::TapeClock& clock, bool haveClock, uint32_t ourSession,
                  juce::uint64 frame0, int numSamples, double sampleRate, const Tempo& localTempo,
                  juce::uint32 nowMs) noexcept;

    void reset() noexcept { wasRolling = false; }

    static constexpr juce::uint32 staleMs = 400;        // no POS for this long: Hawthorne has gone

private:
    bool wasRolling = false;
    double lastBeat1 = 0.0, lastBpm = 0.0, lastBarOne = 0.0;
};

} // namespace thoreau
