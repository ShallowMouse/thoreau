#include "TapeChase.h"

namespace thoreau
{

TapeChase::Span TapeChase::advance (const hawthorne::NetSender::TapeClock& c, bool haveClock, uint32_t ourSession,
                                    juce::uint64 frame0, int n, double sr, const Tempo& local, juce::uint32 nowMs) noexcept
{
    Span s;
    s.synced = haveClock && c.count > 0 && c.session == ourSession && ourSession != 0
            && nowMs - c.receivedMs < staleMs;

    s.reelTempo = s.synced && c.bpm > 0.0;
    s.tempo = s.reelTempo ? Tempo { c.bpm, c.beatsPerBar, c.barOne } : local;
    s.tempo.bpm = juce::jlimit (20.0, 400.0, s.tempo.bpm);

    if (s.synced)
    {
        s.tapePos0 = c.tapePos + ((double) frame0 - c.senderFrame) * c.tapePerFrame;
        s.tapePerSample = c.tapePerFrame;
    }
    s.rolling = s.synced && (c.flags & hawthorne::net::TapeSync::rolling) != 0 && c.tapePerFrame > 0.0;
    s.recording = s.rolling && (c.flags & hawthorne::net::TapeSync::recording) != 0;

    const double samplesPerBeat = sr * 60.0 / s.tempo.bpm;     // tape samples (the reel runs at our rate)
    s.beat0 = (s.tapePos0 - s.tempo.barOne) / samplesPerBeat;
    s.beat1 = (s.tapePos0 + n * s.tapePerSample - s.tempo.barOne) / samplesPerBeat;

    if (! s.rolling)
    {
        wasRolling = false;
        s.beat1 = s.beat0;
        s.scanFrom = s.beat0;
        return s;
    }

    // Carry on from where the last block ended unless the tape jumped: a tiny step
    // either way (Hawthorne's drift correction) must neither repeat nor skip a hit.
    const double tolerance = 2.0 / samplesPerBeat;      // 2 samples
    s.jumped = ! wasRolling || s.tempo.bpm != lastBpm || s.tempo.barOne != lastBarOne
            || std::abs (s.beat0 - lastBeat1) > tolerance;
    s.scanFrom = s.jumped ? s.beat0 : lastBeat1;

    wasRolling = true;
    lastBeat1 = s.beat1;
    lastBpm = s.tempo.bpm;
    lastBarOne = s.tempo.barOne;
    return s;
}

} // namespace thoreau
