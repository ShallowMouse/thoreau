#include "DrumEngine.h"

namespace thoreau
{

int Sequencer::songEntryAt (const Project& pr, double beat, int& repeat, double& entryStart)
{
    repeat = 0;
    entryStart = 0.0;
    const int n = juce::jlimit (0, kSongMax, pr.songLength.load());
    const double total = pr.songBeats();
    if (n == 0 || total <= 0.0 || beat < 0.0) return -1;
    if (beat >= total)
    {
        if (! pr.songLoops.load()) return -1;
        const double passes = std::floor (beat / total);
        beat -= passes * total;
        entryStart = passes * total;
    }

    double start = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const auto& pt = pr.patterns[(size_t) juce::jlimit (0, kPatterns - 1, pr.songPattern[(size_t) i].load())];
        const double one = pt.beats();
        const double len = one * juce::jmax (1, pr.songRepeats[(size_t) i].load());
        if (beat < start + len)
        {
            repeat = (int) std::floor ((beat - start) / one);
            entryStart += start;
            return i;
        }
        start += len;
    }
    return -1;
}

//==============================================================================
DrumEngine::DrumEngine (Project& p) : project (p)
{
}

DrumEngine::~DrumEngine()
{
    netOut.stop();
}

void DrumEngine::setClockSource (ClockSource c)
{
    clock = c;
    stopRequested = true;
}

void DrumEngine::setPlayMode (PlayMode m)
{
    mode = m;
}

void DrumEngine::play()
{
    if (clock.load() == ClockSource::internal)
        playRequested = true;
}

void DrumEngine::stop()
{
    stopRequested = true;
}

void DrumEngine::selectPattern (int p)
{
    p = juce::jlimit (0, kPatterns - 1, p);
    if (uiRolling.load() && mode.load() == PlayMode::pattern)
        queuedPattern = p;
    else
    {
        currentPattern = p;
        queuedPattern = -1;
    }
}

void DrumEngine::audition (int track, float velocity) noexcept
{
    if (! juce::isPositiveAndBelow (track, kTracks)) return;
    const auto scope = auditionFifo.write (1);
    if (scope.blockSize1 > 0) auditions[(size_t) scope.startIndex1] = { track, velocity };
}

bool DrumEngine::popRecordedHit (RecordedHit& h)
{
    const auto scope = recordFifo.read (1);
    if (scope.blockSize1 == 0) return false;
    h = recorded[(size_t) scope.startIndex1];
    return true;
}

bool DrumEngine::setNetworkEnabled (bool on, const juce::String& name, juce::String& error)
{
    if (on)
    {
        if (! netOut.start (name, error))
        {
            networkOn = false;
            return false;
        }
        networkOn = true;
        return true;
    }
    networkOn = false;
    netOut.stop();
    return true;
}

//==============================================================================
// Audio

void DrumEngine::prepare (double sr, int maxBlockSize, int outputLatency)
{
    sampleRate = sr;
    maxBlock = juce::jmax (64, maxBlockSize);
    for (auto& tv : voices)
    {
        for (auto& v : tv.v) v.prepare (sr);
        for (auto& d : tv.drums) d.prepare (sr);
    }
    trackBuf.setSize (kTracks, maxBlock);
    mixBuf.setSize (2, maxBlock);
    bus.prepare (sr, maxBlock);
    if (bus.getKit() != project.kit.load()) bus.setKit (project.kit.load());
    const int ch = individualOuts.load() ? 2 + kTracks + 4 : 2;
    netChannels = ch;
    netBuf.setSize (ch, maxBlock);
    netPtrs.assign ((size_t) ch, nullptr);
    midiCollector.reset (sr);
    midiBlock.ensureSize (4096);
    internalRolling = false;
    chase.reset();
    netOut.prepare (sr, ch, juce::jmax (0, outputLatency) + maxBlock);
}

void DrumEngine::audioDeviceAboutToStart (juce::AudioIODevice* d)
{
    prepare (d->getCurrentSampleRate(), d->getCurrentBufferSizeSamples(), d->getOutputLatencyInSamples());
}

void DrumEngine::audioDeviceStopped()
{
}

void DrumEngine::audioDeviceIOCallbackWithContext (const float* const*, int, float* const* outputs, int numOutputs,
                                                   int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    process (outputs, numOutputs, numSamples);
}

void DrumEngine::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m)
{
    midiCollector.addMessageToQueue (m);
}

void DrumEngine::process (float* const* outputs, int numOutputs, int numSamples) noexcept
{
    for (int o = 0; o < numOutputs; ++o)
        if (outputs[o] != nullptr)
            juce::FloatVectorOperations::clear (outputs[o], numSamples);

    for (int done = 0; done < numSamples;)
    {
        const int n = juce::jmin (maxBlock, numSamples - done);
        renderChunk (outputs, numOutputs, done, n);
        done += n;
    }
}

void DrumEngine::trigger (int track, float velocity, double onset) noexcept
{
    auto& tr = project.tracks[(size_t) track];
    auto& tv = voices[(size_t) track];
    const auto& slot = project.slotFor (track);
    const auto* sample = tr.sample.load();
    const auto params = tr.voiceParams();
    const double at = juce::jmax (0.0, onset);

    // closing the hats (etc.) silences the drum this one chokes
    if (juce::isPositiveAndBelow (slot.chokes, kTracks) && slot.chokes != track)
    {
        auto& other = voices[(size_t) slot.chokes];
        for (auto& v : other.v)     if (v.isActive()) v.release (at);
        for (auto& d : other.drums) if (d.isActive()) d.choke (at, 0.045);
    }

    if (sample != nullptr || slot.model.kind == Kind::analog)
    {
        // one hit at a time per drum: the previous one fades out as this one starts
        for (auto& v : tv.v)
            if (v.isActive()) v.release (at);
        for (auto& d : tv.drums)
            if (d.isActive()) d.choke (at, 0.02);
        auto& v = tv.v[(size_t) tv.next];
        tv.next ^= 1;
        v.trigger (slot.model.kind == Kind::analog ? slot.model.analog : instrumentOf (track), params, velocity, sample, onset);
    }
    else
    {
        // acoustic: a drum that's ringing takes the new hit on top, like the real thing
        for (auto& v : tv.v)
            if (v.isActive()) v.release (at);
        const uint32_t seed = (hitCount++ * 2654435761u) ^ (uint32_t) (track * 40503);
        auto& d = tv.drums[(size_t) tv.cur];
        if (onset >= 0.0 && d.isActive() && ! d.isChoked() && d.matches (slot.model, params))
            d.excite (velocity, onset, seed);
        else
        {
            tv.cur ^= 1;
            auto& fresh = tv.drums[(size_t) tv.cur];
            if (fresh.isActive()) fresh.choke (at, 0.03);   // the one before last: let go
            fresh.start (slot.model, params, velocity, onset, seed);
        }
    }

    float prev = trackHit[(size_t) track].load (std::memory_order_relaxed);
    if (velocity > prev) trackHit[(size_t) track].store (velocity, std::memory_order_relaxed);
}

void DrumEngine::scheduleHit (const Sequencer::Hit& h, double beat0, double beat1, int n) noexcept
{
    const double span = beat1 - beat0;
    const double onset = span > 0.0 ? (h.beat - beat0) / span * n : 0.0;
    trigger (h.track, h.velocity, onset);
}

void DrumEngine::scheduleSpan (double scanFrom, double beat0, double beat1, int n, bool catchUp) noexcept
{
    if (beat1 <= scanFrom) return;

    // Catching up with tape that is already rolling: hits from just before still sound, part-way in.
    if (catchUp)
    {
        const double perSample = (beat1 - beat0) / juce::jmax (1, n);
        const double back = catchUpSeconds * sampleRate.load() * perSample;
        scanFrom -= back;
        catchUps.fetch_add (1, std::memory_order_relaxed);
    }

    auto onHit = [&] (const Sequencer::Hit& h) { scheduleHit (h, beat0, beat1, n); };

    if (mode.load() == PlayMode::song)
    {
        Sequencer::hitsInSong (project, juce::jmax (0.0, scanFrom), beat1, onHit);
        return;
    }

    // Pattern mode: the queued pattern takes over at the end of the current one.
    double from = juce::jmax (0.0, scanFrom);
    for (int guard = 0; guard < 4 && from < beat1; ++guard)
    {
        const int cur = currentPattern.load();
        const double len = project.patterns[(size_t) cur].beats();
        const double passEnd = patternStart + (std::floor ((from - patternStart) / len + 1.0e-9) + 1.0) * len;
        const int q = queuedPattern.load();
        if (q >= 0 && passEnd < beat1)
        {
            Sequencer::hitsInPattern (project, cur, patternStart, from, passEnd, onHit);
            currentPattern = q;
            queuedPattern = -1;
            patternStart = passEnd;
            from = passEnd;
            continue;
        }
        Sequencer::hitsInPattern (project, cur, patternStart, from, beat1, onHit);
        break;
    }
}

void DrumEngine::recordHit (int track, float velocity, double beat) noexcept
{
    if (! liveRecord.load() || ! lastSpanRolling || beat < 0.0) return;

    int pattern = currentPattern.load();
    double start = patternStart;
    if (mode.load() == PlayMode::song)
    {
        int rep = 0;
        const int e = Sequencer::songEntryAt (project, beat, rep, start);
        if (e < 0) return;
        pattern = project.songPattern[(size_t) e].load();
    }
    const auto& pt = project.patterns[(size_t) pattern];
    const int spb = juce::jmax (1, pt.stepsPerBeat.load());
    const int len = juce::jmax (1, pt.length.load());
    auto g = (juce::int64) std::llround ((beat - start) * spb);
    const int step = (int) (((g % len) + len) % len);
    const int value = velocity > 0.9f ? 3 : velocity > 0.55f ? 2 : 1;

    const auto scope = recordFifo.write (1);
    if (scope.blockSize1 > 0) recorded[(size_t) scope.startIndex1] = { pattern, step, track, value };
}

void DrumEngine::renderChunk (float* const* outputs, int numOutputs, int start, int n) noexcept
{
    const double sr = sampleRate.load (std::memory_order_relaxed);
    const juce::uint64 frame0 = netOut.getNextFrame();

    //--------------------------------------------------------------------------
    // Where we are in the music for this block
    double beat0 = 0.0, beat1 = 0.0, scanFrom = 0.0;
    bool rolling = false, jumped = false, catchUp = false;
    TapeChase::Tempo tempo { project.bpm.load(), project.beatsPerBar.load(), 0.0 };

    if (stopRequested.exchange (false))
    {
        internalRolling = false;
        chase.reset();
    }

    if (clock.load() == ClockSource::internal)
    {
        uiSynced = false;
        uiTapeRec = false;
        uiReelTempo = false;
        if (playRequested.exchange (false))
        {
            internalRolling = true;
            internalBeat = 0.0;
            patternStart = 0.0;
            jumped = true;
        }
        rolling = internalRolling;
        beat0 = internalBeat;
        beat1 = beat0 + (double) n * tempo.bpm / (60.0 * sr);
        scanFrom = beat0;
        if (rolling)
        {
            internalBeat = beat1;
            if (mode.load() == PlayMode::song && ! project.songLoops.load() && beat0 >= project.songBeats())
                internalRolling = rolling = false;     // end of the song
        }
    }
    else
    {
        hawthorne::NetSender::TapeClock c;
        const bool have = networkOn.load() && netOut.getTapeClock (c);
        const auto span = chase.advance (c, have, netOut.getSession(), frame0, n, sr, tempo, juce::Time::getMillisecondCounter());
        rolling = span.rolling;
        beat0 = span.beat0; beat1 = span.beat1; scanFrom = span.scanFrom;
        jumped = span.jumped;
        catchUp = span.jumped;
        tempo = span.tempo;
        uiSynced = span.synced;
        uiTapeRec = span.recording;
        uiReelTempo = span.reelTempo;
        uiTapePos = span.tapePos0;
        if (jumped)
            patternStart = 0.0;    // after a locate, the pattern is taken to have looped from bar 1
    }

    if (jumped && mode.load() == PlayMode::pattern)
    {
        const int q = queuedPattern.exchange (-1);
        if (q >= 0) currentPattern = q;
    }

    if (rolling)
        scheduleSpan (scanFrom, beat0, beat1, n, catchUp);

    lastSpanBeat0 = beat0; lastSpanBeat1 = beat1; lastSpanRolling = rolling; lastSpanN = n;

    //--------------------------------------------------------------------------
    // Pads and MIDI: play now (and into the pattern when live recording)
    {
        const auto scope = auditionFifo.read (auditionFifo.getNumReady());
        auto play = [&] (int s0, int count)
        {
            for (int i = 0; i < count; ++i)
            {
                const auto& a = auditions[(size_t) (s0 + i)];
                trigger (a.track, a.velocity, 0.0);
                recordHit (a.track, a.velocity, beat0);
            }
        };
        play (scope.startIndex1, scope.blockSize1);
        play (scope.startIndex2, scope.blockSize2);
    }
    midiBlock.clear();
    midiCollector.removeNextBlockOfMessages (midiBlock, n);
    for (const auto meta : midiBlock)
    {
        const auto m = meta.getMessage();
        if (! m.isNoteOn()) continue;
        for (int t = 0; t < kTracks; ++t)
            if (project.tracks[(size_t) t].midiNote.load() == m.getNoteNumber())
            {
                const double onset = juce::jlimit (0, n - 1, meta.samplePosition);
                trigger (t, m.getFloatVelocity(), onset);
                recordHit (t, m.getFloatVelocity(), beat0 + (beat1 - beat0) * onset / n);
            }
    }

    //--------------------------------------------------------------------------
    // Voices -> tracks -> the kit's mics and bus -> master
    bool anySolo = false;
    for (auto& tr : project.tracks) anySolo = anySolo || tr.solo.load (std::memory_order_relaxed);

    std::array<const float*, kTracks> trackPtrs {};
    for (int t = 0; t < kTracks; ++t)
    {
        float* tb = trackBuf.getWritePointer (t);
        juce::FloatVectorOperations::clear (tb, n);
        auto& tv = voices[(size_t) t];
        for (auto& v : tv.v) v.render (tb, n);
        for (auto& d : tv.drums)
        {
            if (! d.isActive()) continue;
            // a fresh drum that was choked (the one before last) may still render a few ms
            d.render (tb, n);
        }

        const auto& tr = project.tracks[(size_t) t];
        const bool audible = ! tr.mute.load (std::memory_order_relaxed) && (! anySolo || tr.solo.load (std::memory_order_relaxed));
        const float level = audible ? juce::Decibels::decibelsToGain (tr.levelDb.load (std::memory_order_relaxed), -59.9f) : 0.0f;
        if (level > 0.0f) juce::FloatVectorOperations::multiply (tb, level, n);
        else              juce::FloatVectorOperations::clear (tb, n);

        float peak = 0.0f;
        for (int i = 0; i < n; ++i) peak = juce::jmax (peak, std::abs (tb[i]));
        if (peak > trackPeak[(size_t) t].load (std::memory_order_relaxed)) trackPeak[(size_t) t].store (peak, std::memory_order_relaxed);

        const auto& slot = project.slotFor (t);
        auto& sd = sends[(size_t) t];
        sd.pan = tr.pan.load (std::memory_order_relaxed);
        sd.close = slot.close; sd.oh = slot.oh; sd.room = slot.room; sd.fx = slot.fx;
        trackPtrs[(size_t) t] = tb;
    }

    KitBus::Knobs knobs;
    knobs.closeDb = project.closeDb.load (std::memory_order_relaxed);
    knobs.ohDb    = project.ohDb.load (std::memory_order_relaxed);
    knobs.roomDb  = project.roomDb.load (std::memory_order_relaxed);
    knobs.comp    = project.compAmount.load (std::memory_order_relaxed);
    knobs.tape    = project.tapeAmount.load (std::memory_order_relaxed);
    bus.process (trackPtrs.data(), sends, knobs, mixBuf.getWritePointer (0), mixBuf.getWritePointer (1), n);

    // Master: level, then a soft knee above -6 dBFS so a pile-up never clips harshly.
    const float master = juce::Decibels::decibelsToGain (project.masterDb.load (std::memory_order_relaxed), -59.9f);
    for (int ch = 0; ch < 2; ++ch)
    {
        float* d = mixBuf.getWritePointer (ch);
        float peak = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            float x = d[i] * master;
            const float ax = std::abs (x);
            if (ax > 0.5f) x = std::copysign (0.5f + 0.5f * std::tanh ((ax - 0.5f) / 0.5f), x);
            d[i] = x;
            peak = juce::jmax (peak, std::abs (x));
        }
        if (peak > masterPeak[(size_t) ch].load (std::memory_order_relaxed)) masterPeak[(size_t) ch].store (peak, std::memory_order_relaxed);
    }

    //--------------------------------------------------------------------------
    // Out: this PC's outputs, and Hawthorne Link
    if (localOut.load (std::memory_order_relaxed))
        for (int ch = 0; ch < juce::jmin (2, numOutputs); ++ch)
            if (outputs[ch] != nullptr)
                juce::FloatVectorOperations::copy (outputs[ch] + start, mixBuf.getReadPointer (ch), n);

    const int nc = netChannels.load (std::memory_order_relaxed);
    if (networkOn.load (std::memory_order_relaxed) && (int) netPtrs.size() >= nc)
    {
        netPtrs[0] = mixBuf.getReadPointer (0);
        netPtrs[1] = mixBuf.getReadPointer (1);
        if (nc >= 2 + kTracks + 4)
        {
            for (int t = 0; t < kTracks; ++t)
                netPtrs[(size_t) (2 + t)] = trackBuf.getReadPointer (t);
            netPtrs[(size_t) (2 + kTracks)]     = bus.overheadStem (0);
            netPtrs[(size_t) (2 + kTracks + 1)] = bus.overheadStem (1);
            netPtrs[(size_t) (2 + kTracks + 2)] = bus.roomStem (0);
            netPtrs[(size_t) (2 + kTracks + 3)] = bus.roomStem (1);
        }
        netOut.push (netPtrs.data(), nc, n);
    }

    //--------------------------------------------------------------------------
    // Display
    uiRolling.store (rolling, std::memory_order_relaxed);
    uiBeat.store (rolling || clock.load() == ClockSource::hawthorne ? beat0 : 0.0, std::memory_order_relaxed);
    uiBpm.store (tempo.bpm, std::memory_order_relaxed);
    uiBeatsPerBar.store (tempo.beatsPerBar, std::memory_order_relaxed);

    int pat = currentPattern.load(), step = -1, entry = -1;
    double pStart = patternStart;
    if (rolling && beat0 >= 0.0)
    {
        if (mode.load() == PlayMode::song)
        {
            int rep = 0;
            entry = Sequencer::songEntryAt (project, beat0, rep, pStart);
            if (entry >= 0) pat = project.songPattern[(size_t) entry].load();
        }
        if (entry >= 0 || mode.load() == PlayMode::pattern)
        {
            const auto& pt = project.patterns[(size_t) pat];
            const auto g = (juce::int64) std::floor ((beat0 - pStart) * pt.stepsPerBeat.load() + 1.0e-9);
            step = (int) (g % juce::jmax (1, pt.length.load()));
        }
    }
    uiPattern.store (pat, std::memory_order_relaxed);
    uiStep.store (step, std::memory_order_relaxed);
    uiEntry.store (entry, std::memory_order_relaxed);
}

} // namespace thoreau
