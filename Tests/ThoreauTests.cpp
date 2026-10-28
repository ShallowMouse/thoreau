// Headless tests for THOREAU: voices, sequencer, internal clock, and tape sync
// end to end against Hawthorne's own network receiver over real sockets.

#include "../Source/DrumEngine.h"
#if THOREAU_HAVE_HAWTHORNE
 #include "HawthorneBridge.h"
#endif
#include <cstdio>
#include <thread>

using namespace thoreau;
using juce::int64;

static int failures = 0;
#define CHECK(cond, ...) do { if (! (cond)) { ++failures; std::printf ("  FAIL %s:%d: ", __FILE__, __LINE__); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

static constexpr double kSR = 48000.0;

/** A band-limited click as a sample: peak exactly at sample kClickPeak. */
static constexpr int kClickPeak = 32;
static juce::File writeClickSample()
{
    auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("thoreau-click.wav");
    juce::AudioBuffer<float> b (1, 64);
    for (int i = 0; i < 64; ++i)
    {
        const double x = i - kClickPeak;
        const double w = 0.5 + 0.5 * std::cos (juce::MathConstants<double>::pi * x / 32.0);
        const double a = juce::MathConstants<double>::pi * 0.4 * x;
        b.setSample (0, i, (float) (w * 0.8 * (x == 0.0 ? 1.0 : std::sin (a) / a)));
    }
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (f.createOutputStream());
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), kSR, 1, 32, {}, 0));
    if (w != nullptr) { os.release(); w->writeFromAudioSampleBuffer (b, 0, 64); }
    return f;
}

/** Peaks above threshold, sub-sample (parabolic), at least minGap apart. */
static std::vector<double> findPeaks (const std::vector<float>& d, float threshold, int minGap)
{
    std::vector<double> peaks;
    for (int i = 1; i + 1 < (int) d.size(); ++i)
    {
        const float v = d[(size_t) i];                  // the click's peak is positive; its sidelobes go negative
        if (v < threshold || v < d[(size_t) i - 1] || v < d[(size_t) i + 1]) continue;
        if (! peaks.empty() && i - peaks.back() < minGap) continue;
        const double y0 = d[(size_t) i - 1], y1 = d[(size_t) i], y2 = d[(size_t) i + 1];
        peaks.push_back (i + 0.5 * (y0 - y2) / (y0 - 2.0 * y1 + y2));
    }
    return peaks;
}

//==============================================================================
static void testVoices()
{
    std::printf ("[1] every instrument sounds, decays, stays finite\n");
    for (int t = 0; t < kTracks; ++t)
        for (float decay : { 0.0f, 0.5f, 1.0f })
        {
            Voice v;
            v.prepare (kSR);
            VoiceParams p; p.decay = decay; p.tune = (float) (t % 3 - 1) * 5.0f; p.tone = 0.7f; p.snap = 0.8f;
            v.trigger (instrumentOf (t), p, 1.0f, nullptr, 3.5);
            std::vector<float> buf (512);
            double energy = 0.0; float peak = 0.0f; bool finite = true; int blocks = 0;
            while (v.isActive() && blocks < 2000)
            {
                std::fill (buf.begin(), buf.end(), 0.0f);
                v.render (buf.data(), 512);
                for (float x : buf) { finite = finite && std::isfinite (x); energy += x * x; peak = juce::jmax (peak, std::abs (x)); }
                ++blocks;
            }
            CHECK (finite, "%s produced NaN/inf", instrumentName (t).toRawUTF8());
            CHECK (energy > 1.0 && peak > 0.05f, "%s (decay %.1f) is silent", instrumentName (t).toRawUTF8(), decay);
            CHECK (peak < 2.0f, "%s peak %.2f", instrumentName (t).toRawUTF8(), peak);
            CHECK (! v.isActive(), "%s (decay %.1f) never ended", instrumentName (t).toRawUTF8(), decay);
        }

    // Sub-sample timing and catching up: the cowbell is noise-free, so it is exactly repeatable.
    auto render = [] (double onset, int len)
    {
        Voice v; v.prepare (kSR);
        v.trigger (Instrument::cowbell, {}, 1.0f, nullptr, onset);
        std::vector<float> out ((size_t) len, 0.0f);
        for (int i = 0; i < len; i += 64) v.render (out.data() + i, juce::jmin (64, len - i));
        return out;
    };
    const auto a = render (0.0, 4000), b = render (37.0, 4000), c = render (-200.0, 4000);
    double errShift = 0, errCatch = 0;
    for (int i = 0; i + 37 < 4000; ++i) errShift = juce::jmax (errShift, (double) std::abs (b[(size_t) i + 37] - a[(size_t) i]));
    for (int i = 1000; i + 200 < 4000; ++i) errCatch = juce::jmax (errCatch, (double) std::abs (c[(size_t) i] - a[(size_t) i + 200]));
    CHECK (errShift < 1e-5, "onset 37 isn't a 37-sample shift (%.2g)", errShift);
    CHECK (errCatch < 1e-3, "catching up 200 samples in isn't the same sound (%.2g)", errCatch);

    // Sample voice: a click lands on its onset to a fraction of a sample.
    juce::String err;
    auto smp = Sample::load (writeClickSample(), err);
    CHECK (smp != nullptr, "click sample: %s", err.toRawUTF8());
    if (smp != nullptr)
    {
        double worst = 0.0;
        for (double onset : { 100.0, 100.25, 100.5, 100.75, 101.0 })
        {
            Voice v; v.prepare (kSR);
            VoiceParams p; p.decay = 1.0f; p.tone = 1.0f; p.snap = 0.0f;
            v.trigger (Instrument::kick, p, 1.0f, smp.get(), onset);
            std::vector<float> out (512, 0.0f);
            v.render (out.data(), 512);
            auto pk = findPeaks (out, 0.2f, 40);
            if (pk.size() == 1) worst = juce::jmax (worst, std::abs (pk[0] - (onset + kClickPeak)));
            else worst = 99;
        }
        CHECK (worst < 0.1, "sample click off its onset by %.3f samples", worst);
        std::printf ("    12 instruments x 3 decays clean; sub-sample onsets exact; sample clicks within %.3f samples\n", worst);
    }
}

//==============================================================================
static void testSequencer()
{
    std::printf ("[2] sequencer: patterns, swing, song\n");
    Project pr;
    for (auto& p : pr.patterns) p.clear();
    pr.patterns[0].steps[0][0] = 3;
    pr.patterns[0].steps[0][8] = 2;
    pr.patterns[0].steps[1][4] = 2;
    pr.patterns[0].steps[4][1] = 1;      // odd step: swung

    std::vector<Sequencer::Hit> hits;
    auto collect = [&] (const Sequencer::Hit& h) { hits.push_back (h); };
    Sequencer::hitsInPattern (pr, 0, 0.0, 0.0, 8.0, collect);
    CHECK (hits.size() == 8, "two bars of A: %d hits", (int) hits.size());

    hits.clear();
    pr.swing = 66.0f;
    Sequencer::hitsInPattern (pr, 0, 0.0, 0.0, 4.0, collect);
    bool sawSwung = false;
    for (auto& h : hits)
        if (h.track == 4) { sawSwung = true; CHECK (std::abs (h.beat - (1.0 + 0.32) / 4.0) < 1e-9, "swung step at %.4f", h.beat); }
    CHECK (sawSwung, "no swung hit");

    // Windows that split anywhere find every hit exactly once.
    hits.clear();
    for (double b = 0.0; b < 16.0; b += 0.0371)
        Sequencer::hitsInPattern (pr, 0, 0.0, b, juce::jmin (16.0, b + 0.0371), collect);
    CHECK (hits.size() == 16, "split windows: %d hits (want 16)", (int) hits.size());

    // Song: A x2 (16 steps) then B (8 steps of 8ths = 4 beats)
    pr.swing = 50.0f;
    pr.patterns[1].length = 8;
    pr.patterns[1].stepsPerBeat = 2;
    pr.patterns[1].steps[2][0] = 2;
    pr.setSong ({ { 0, 2 }, { 1, 1 } });
    CHECK (std::abs (pr.songBeats() - 12.0) < 1e-9, "song length %.2f beats", pr.songBeats());
    hits.clear();
    Sequencer::hitsInSong (pr, 0.0, 100.0, collect);
    CHECK (hits.size() == 9 && hits.back().track == 2 && std::abs (hits.back().beat - 8.0) < 1e-9, "song hits %d", (int) hits.size());
    pr.songLoops = true;
    hits.clear();
    Sequencer::hitsInSong (pr, 11.0, 13.0, collect);     // across the loop point
    CHECK (hits.size() == 2 && std::abs (hits[0].beat - 12.0) < 1e-9 && std::abs (hits[1].beat - 12.25) < 1e-9, "looped song: %d hits", (int) hits.size());
    int rep = 0; double start = 0;
    CHECK (Sequencer::songEntryAt (pr, 5.0, rep, start) == 0 && rep == 1, "entry at beat 5");
    CHECK (Sequencer::songEntryAt (pr, 9.0, rep, start) == 1 && start == 8.0, "entry at beat 9");

    // Save / load
    auto xml = pr.toXml();
    Project q;
    juce::String err;
    CHECK (q.fromXml (*xml, err), "load: %s", err.toRawUTF8());
    CHECK (q.patterns[1].length.load() == 8 && q.patterns[1].stepsPerBeat.load() == 2 && q.patterns[0].steps[4][1].load() == 1
           && q.getSong().size() == 2 && q.songLoops.load(), "project didn't round-trip");
    std::printf ("    hits exact with swing and any window split; song chains and loops; projects round-trip\n");
}

//==============================================================================
static void testInternalClock()
{
    std::printf ("[3] internal clock: hits on the beat at 97 BPM\n");
    Project pr;
    pr.setKit (analogKit());
    juce::String err;
    CHECK (pr.loadSample (0, writeClickSample(), err), "sample: %s", err.toRawUTF8());
    for (auto& p : pr.patterns) p.clear();
    for (int s = 0; s < 16; s += 4) pr.patterns[0].steps[0][(size_t) s] = 3;
    pr.tracks[0].decay = 1.0f; pr.tracks[0].tone = 1.0f; pr.tracks[0].snap = 0.0f; pr.tracks[0].pan = 0.0f;
    pr.bpm = 97.0;

    DrumEngine e (pr);
    e.prepare (kSR, 333, 0);
    e.play();
    std::vector<float> out;
    juce::AudioBuffer<float> buf (2, 333);
    for (int i = 0; i < 3000; ++i)
    {
        e.process (buf.getArrayOfWritePointers(), 2, 333);
        out.insert (out.end(), buf.getReadPointer (0), buf.getReadPointer (0) + 333);
    }
    auto pk = findPeaks (out, 0.1f, 1000);
    const double spb = kSR * 60.0 / 97.0;
    double worst = 0.0;
    for (size_t i = 0; i < pk.size(); ++i)
        worst = juce::jmax (worst, std::abs (pk[i] - (kClickPeak + (double) i * spb)));
    CHECK (pk.size() >= 30, "%d beats", (int) pk.size());
    CHECK (worst < 0.1, "worst beat %.3f samples off", worst);
    std::printf ("    %d beats, worst %.3f samples off\n", (int) pk.size(), worst);
}

//==============================================================================
#if THOREAU_HAVE_HAWTHORNE
// The whole thing over real sockets on this machine: THOREAU streams to Hawthorne's
// receiver, Hawthorne's sync thread sends POS back, THOREAU stripes its tempo onto the
// reel and then follows it; a simulated tape records what arrives (with round-trip
// latency compensation, and THOREAU on a clock 57 ppm fast). Every click must land on
// its beat on tape.
static void testTapeSyncEndToEnd()
{
    std::printf ("[4] tape sync end to end with Hawthorne's receiver (real sockets, ~16 s)\n");
    Project pr;
    pr.setKit (analogKit());
    juce::String err;
    CHECK (pr.loadSample (0, writeClickSample(), err), "sample: %s", err.toRawUTF8());
    for (auto& p : pr.patterns) p.clear();
    for (int s = 0; s < 16; s += 4) pr.patterns[0].steps[0][(size_t) s] = 3;
    pr.tracks[0].decay = 1.0f; pr.tracks[0].tone = 1.0f; pr.tracks[0].snap = 0.0f;
    pr.bpm = 112.0;

    DrumEngine thoreau (pr);
    CHECK (thoreau.setNetworkEnabled (true, "THOREAU test", err), "net: %s", err.toRawUTF8());
    thoreau.setClockSource (ClockSource::hawthorne);
    thoreau.prepare (kSR, 256, 0);

    HawthorneSim hw;
    CHECK (hw.start (err), "receiver: %s", err.toRawUTF8());
    hw.addManualAddress ("127.0.0.1");
    hw.connect ("THOREAU test");
    hw.prepare (kSR);
    hw.setReel ("Test Reel", 0.0, 4, 0.0);          // a reel with no tempo yet

    const int RT = 384;                              // Hawthorne's round trip (latency compensation)
    const double ppm = 57.0;
    std::vector<float> tape ((size_t) (40 * kSR), 0.0f);
    double tapePos = 0.0;
    bool rolling = false;
    double reelBpm = 0.0, reelBarOne = 0.0;
    int reelBpb = 4;

    juce::AudioBuffer<float> out (2, 512);
    double owed = 0.0;
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    int64 clockN = 0;
    double rollStart = -1, rollEnd = -1;

    for (int64 block = 0; block < (int64) (16.0 * kSR / 256); ++block)
    {
        // THOREAU's own (slightly fast) clock
        owed += 256.0 * (1.0 + ppm * 1.0e-6);
        const int n = (int) owed;
        owed -= n;
        thoreau.process (out.getArrayOfWritePointers(), 2, n);

        // Hawthorne's audio thread
        hw.process (256);
        hw.publishTape (tapePos - (rolling ? RT : 0), rolling ? 1.0 : 0.0, rolling, rolling);
        if (rolling)
            if (auto* in = hw.channel (0))
                for (int k = 0; k < 256; ++k)
                {
                    const auto at = (int64) tapePos + k - RT;
                    if (at >= 0 && at < (int64) tape.size()) tape[(size_t) at] = in[k];
                }
        if (rolling) tapePos += 256;

        // Hawthorne's message thread: tempo from THOREAU goes onto the reel
        double b; int bpb; double one;
        while (hw.popTempo (b, bpb, one)) { reelBpm = b; reelBpb = bpb; reelBarOne = one; hw.setReel ("Test Reel", b, bpb, one); }

        // the "tape operator": roll at 6 s from 2.0 s on the counter, stop at 14 s
        const double secs = (double) clockN / kSR;
        if (! rolling && secs >= 6.0 && rollStart < 0) { rolling = true; tapePos = 2.0 * kSR; rollStart = tapePos; }
        if (rolling && secs >= 14.0) { rolling = false; rollEnd = tapePos; }

        // THOREAU's message thread (what its main window does): stripe the tempo onto a reel without one
        if (block % 40 == 0)
        {
            hawthorne::NetSender::TapeClock c;
            if (thoreau.netOut.getTapeClock (c) && c.bpm <= 0.0 && (c.flags & 4) != 0)
                thoreau.netOut.sendTempo (pr.bpm.load(), 4, 0.25 * kSR);
        }

        clockN += 256;
        const double due = t0 + (double) clockN / kSR * 1000.0;
        const double now = juce::Time::getMillisecondCounterHiRes();
        if (due > now) juce::Thread::sleep ((int) (due - now));
    }

    CHECK (reelBpm == 112.0 && std::abs (reelBarOne - 0.25 * kSR) < 1e-6, "tempo not striped onto the reel (%.1f BPM)", reelBpm);
    std::printf ("    Hawthorne: %s; reel tempo %.1f BPM (striped by THOREAU)\n", hw.describe().toRawUTF8(), reelBpm);

    // Clicks on tape vs the beat grid
    std::vector<float> taken (tape.begin() + (int64) rollStart, tape.begin() + (int64) rollEnd);
    auto pk = findPeaks (taken, 0.2f, 2000);
    const double spb = kSR * 60.0 / reelBpm;
    int onBeat = 0;
    double worst = 0.0;
    for (double p : pk)
    {
        const double pos = rollStart + p - kClickPeak;            // tape position of the hit
        const double beats = (pos - reelBarOne) / spb;
        const double err = (beats - std::round (beats)) * spb;
        worst = juce::jmax (worst, std::abs (err));
        ++onBeat;
    }
    const double expected = std::floor ((rollEnd - reelBarOne) / spb) - std::ceil ((rollStart - reelBarOne) / spb) + 1;
    // the first beat or so after the tape starts is still on its way (THOREAU hears about the start late)
    CHECK (onBeat >= expected - 1 && onBeat <= expected, "%d clicks on tape, %.0f beats in the take", onBeat, expected);
    CHECK (worst < 0.25, "worst click %.3f samples off the beat", worst);
    CHECK (thoreau.netOut.getQueueOverruns() == 0, "sender overruns %lld", (long long) thoreau.netOut.getQueueOverruns());
    std::printf ("    %d clicks on %.0f beats of tape, worst %.3f samples off the beat\n", onBeat, expected, worst);

    hw.stop();
    juce::String e2;
    thoreau.setNetworkEnabled (false, {}, e2);
}
#endif

//==============================================================================
static const char* kindName (Kind k)
{
    static const char* n[] = { "analog", "kick", "snare", "rimshot", "sidestick", "brush", "sweep", "tom", "hatClosed", "hatOpen",
                               "hatPedal", "crash", "ride", "rideBell", "china", "splash", "gong", "tambourine", "claps", "syndrum", "cowbell" };
    return n[(int) k];
}

static void testKitPieces()
{
    std::printf ("[5] every drum of every kit: sounds, stays finite, rings out, sensible level\n");
    const bool table = juce::SystemStats::getEnvironmentVariable ("THOREAU_KIT_TABLE", {}).isNotEmpty();
    int pieces = 0;
    double worstCpu = 0.0;
    for (int k = 0; k < (int) kits().size(); ++k)
    {
        const auto& kit = kits()[(size_t) k];
        if (! kit.acoustic) continue;
        for (int t = 0; t < kTracks; ++t)
        {
            const auto& slot = kit.slots[(size_t) t];
            for (float vel : { 1.0f, 0.35f })
            {
                AcousticDrum d;
                d.prepare (kSR);
                d.start (slot.model, {}, vel, 2.5, 1234u + (uint32_t) t);
                std::vector<float> buf (512);
                float peak = 0.0f; bool finite = true; int64 len = 0; double energy = 0.0;
                const int64 limit = (int64) (kSR * (slot.model.kind == Kind::gong ? 60.0 : 20.0));
                const auto t0 = juce::Time::getMillisecondCounterHiRes();
                while (d.isActive() && len < limit)
                {
                    std::fill (buf.begin(), buf.end(), 0.0f);
                    d.render (buf.data(), 512);
                    for (float x : buf) { finite = finite && std::isfinite (x); peak = juce::jmax (peak, std::abs (x)); energy += (double) x * x; }
                    len += 512;
                    if (len == 512 * 40) d.excite (vel, 100.0, 99u);          // hit it again while it rings
                }
                const double secs = (double) len / kSR;
                const double cpu = (juce::Time::getMillisecondCounterHiRes() - t0) / (secs * 1000.0);
                worstCpu = juce::jmax (worstCpu, cpu);
                const auto name = kit.name + " " + slot.name;
                CHECK (finite, "%s: NaN/inf", name.toRawUTF8());
                CHECK (peak > (vel > 0.5f ? 0.04f : 0.004f), "%s (vel %.2f) is too quiet: peak %.4f", name.toRawUTF8(), vel, peak);
                CHECK (peak < 1.6f, "%s (vel %.2f) peak %.2f", name.toRawUTF8(), vel, peak);
                CHECK (! d.isActive(), "%s never died away (%.1f s)", name.toRawUTF8(), secs);
                if (table && vel > 0.5f)
                    std::printf ("      %-18s %-12s %-10s peak %6.1f dB  rms %6.1f dB  %5.2f s  cpu %.3f\n", kit.name.toRawUTF8(), slot.name.toRawUTF8(),
                                 kindName (slot.model.kind), juce::Decibels::gainToDecibels (peak),
                                 juce::Decibels::gainToDecibels ((float) std::sqrt (energy / juce::jmax ((int64) 1, juce::jmin (len, (int64) (kSR * 0.2))))), secs, cpu);
            }
            ++pieces;
        }
    }
    CHECK (worstCpu < 0.05, "a drum costs %.1f %% of a core", worstCpu * 100.0);
    std::printf ("    %d drums x 2 velocities clean (re-hits while ringing too); worst %.2f %% of a core per drum\n", pieces, worstCpu * 100.0);
}

static double measureRt60 (const juce::AudioBuffer<float>& ir, int ch)
{
    // Schroeder backward integration; T60 from the -5 to -25 dB slope
    const float* d = ir.getReadPointer (ch);
    const int n = ir.getNumSamples();
    std::vector<double> e ((size_t) n + 1, 0.0);
    for (int i = n - 1; i >= 0; --i) e[(size_t) i] = e[(size_t) i + 1] + (double) d[i] * d[i];
    int i5 = -1, i25 = -1;
    for (int i = 0; i < n; ++i)
    {
        const double db = 10.0 * std::log10 (e[(size_t) i] / e[0] + 1e-30);
        if (i5 < 0 && db <= -5.0) i5 = i;
        if (i25 < 0 && db <= -25.0) { i25 = i; break; }
    }
    return i5 >= 0 && i25 > i5 ? 3.0 * (i25 - i5) / kSR : 0.0;
}

static void testKitRoomsAndBus()
{
    std::printf ("[6] kit rooms (decay time), analogue bus untouched, every kit's beat through the mixer\n");

    // rooms decay as long as the kit says
    double worstRt = 0.0;
    for (auto& kit : kits())
    {
        auto ir = KitBus::makeRoom (kit.reverb, kSR, false);
        bool finite = true;
        for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < ir.getNumSamples(); ++i) finite = finite && std::isfinite (ir.getSample (ch, i));
        const double rt = measureRt60 (ir, 0);
        const double err = std::abs (rt / kit.reverb.rt60 - 1.0);
        worstRt = juce::jmax (worstRt, err);
        CHECK (finite && err < 0.3, "%s room: T60 %.2f s, wanted %.2f s", kit.name.toRawUTF8(), rt, kit.reverb.rt60);
    }

    // THOREAU ANALOGUE's bus = exactly the panned close mics (old projects sound as they did)
    {
        KitBus bus;
        bus.prepare (kSR, 256);
        bus.setKit (analogKit());
        juce::AudioBuffer<float> tr (kTracks, 256), out (2, 256);
        juce::Random r (5);
        std::array<KitBus::Send, kTracks> sends;
        const auto& def = kitAt (analogKit());
        for (int t = 0; t < kTracks; ++t)
        {
            sends[(size_t) t] = { def.slots[(size_t) t].pan, def.slots[(size_t) t].close, def.slots[(size_t) t].oh, def.slots[(size_t) t].room, def.slots[(size_t) t].fx };
            for (int i = 0; i < 256; ++i) tr.setSample (t, i, r.nextFloat() - 0.5f);
        }
        KitBus::Knobs knobs { def.closeDb, def.ohDb, def.roomDb, def.comp, def.tape };
        double worst = 0.0;
        for (int b = 0; b < 4; ++b)
        {
            bus.process (tr.getArrayOfReadPointers(), sends, knobs, out.getWritePointer (0), out.getWritePointer (1), 256);
            for (int i = 0; i < 256; ++i)
            {
                double l = 0, rr = 0;
                for (int t = 0; t < kTracks; ++t)
                {
                    const double a = (sends[(size_t) t].pan + 1.0) * juce::MathConstants<double>::pi * 0.25;
                    l += tr.getSample (t, i) * std::cos (a); rr += tr.getSample (t, i) * std::sin (a);
                }
                worst = juce::jmax (worst, std::abs (l - out.getSample (0, i)), std::abs (rr - out.getSample (1, i)));
            }
        }
        CHECK (worst < 1e-5, "analogue bus changes the sound (%.2g)", worst);
    }

    // every kit's starter beat, through the whole engine
    double worstCpu = 0.0;
    for (int k = 0; k < (int) kits().size(); ++k)
    {
        Project pr;
        pr.setKit (k);
        for (auto& p : pr.patterns) p.clear();
        pr.loadGroove (k, 0, 1);
        pr.setSong ({ { 0, 2 }, { 1, 1 } });
        DrumEngine e (pr);
        e.setIndividualOuts (true);
        e.prepare (kSR, 256, 0);
        e.setPlayMode (PlayMode::song);
        e.play();
        juce::AudioBuffer<float> out (2, 256);
        double sum = 0; float peak = 0; bool finite = true; int64 n = 0;
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        for (int b = 0; b < (int) (6.0 * kSR / 256); ++b)
        {
            e.process (out.getArrayOfWritePointers(), 2, 256);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 256; ++i) { const float x = out.getSample (ch, i); finite = finite && std::isfinite (x); peak = juce::jmax (peak, std::abs (x)); sum += (double) x * x; }
            n += 512;
        }
        const double cpu = (juce::Time::getMillisecondCounterHiRes() - t0) / 6000.0;
        worstCpu = juce::jmax (worstCpu, cpu);
        const double rms = juce::Decibels::gainToDecibels ((float) std::sqrt (sum / n));
        CHECK (finite && peak <= 1.0f && rms > -32.0 && rms < -6.0, "%s beat: peak %.2f, rms %.1f dB", kits()[(size_t) k].name.toRawUTF8(), peak, rms);
        if (juce::SystemStats::getEnvironmentVariable ("THOREAU_KIT_TABLE", {}).isNotEmpty())
            std::printf ("      %-18s beat: peak %5.1f dB  rms %5.1f dB  cpu %4.1f %%\n", kits()[(size_t) k].name.toRawUTF8(),
                         juce::Decibels::gainToDecibels (peak), rms, cpu * 100.0);
    }
    CHECK (worstCpu < 0.35, "a kit costs %.0f %% of a core", worstCpu * 100.0);
    std::printf ("    rooms within %.0f %% of their decay times; analogue bus exact; %d kits' beats clean; worst %.1f %% of a core\n",
                 worstRt * 100.0, (int) kits().size(), worstCpu * 100.0);
}

/** THOREAU_DEMO=<folder>: renders each kit's beat (A A B) to <folder>/<kit>.wav. */
static void renderDemos (const juce::File& folder)
{
    folder.createDirectory();
    for (int k = 0; k < (int) kits().size(); ++k)
    {
        Project pr;
        pr.setKit (k);
        for (auto& p : pr.patterns) p.clear();
        pr.loadGroove (k, 0, 1);
        pr.setSong ({ { 0, 2 }, { 1, 1 }, { 0, 1 } });
        DrumEngine e (pr);
        e.prepare (kSR, 512, 0);
        e.setPlayMode (PlayMode::song);
        e.play();
        const double secs = pr.songBeats() * 60.0 / pr.bpm.load() + 2.5;
        juce::AudioBuffer<float> all (2, (int) (secs * kSR)), out (2, 512);
        for (int pos = 0; pos < all.getNumSamples(); pos += 512)
        {
            const int m = juce::jmin (512, all.getNumSamples() - pos);
            e.process (out.getArrayOfWritePointers(), 2, m);
            for (int ch = 0; ch < 2; ++ch) all.copyFrom (ch, pos, out, ch, 0, m);
        }
        auto f = folder.getChildFile (juce::String (k).paddedLeft ('0', 2) + " " + juce::File::createLegalFileName (kits()[(size_t) k].name) + ".wav");
        f.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> os (f.createOutputStream());
        if (auto* w = wav.createWriterFor (os.get(), kSR, 2, 24, {}, 0))
        {
            os.release();
            std::unique_ptr<juce::AudioFormatWriter> writer (w);
            writer->writeFromAudioSampleBuffer (all, 0, all.getNumSamples());
        }
        std::printf ("    %s\n", f.getFullPathName().toRawUTF8());
    }
}

/** Hi-hats must sound like hats: bright, little low end, and noise-like (a wash, not a ringing chord). */
static void testHats()
{
    std::printf ("[7] hi-hats: bright, little low end, a wash not a chord\n");
    const int N = 8192;
    double worstLow = 0, worstCentroid = 1e9, worstCentroidHigh = 0, worstFlat = 1;
    for (auto& kit : kits())
    {
        if (! kit.acoustic) continue;
        for (int t : { 4, 5 })
        {
            const auto& slot = kit.slots[(size_t) t];
            AcousticDrum d; d.prepare (kSR);
            d.start (slot.model, {}, 0.8f, 0.0, 3u);
            std::vector<float> x ((size_t) N, 0.0f);
            d.render (x.data(), N);
            // spectrum: FFT of the whole hit (Hann window)
            juce::dsp::FFT fft (13);
            std::vector<float> buf ((size_t) N * 2, 0.0f);
            for (int i = 0; i < N; ++i)
                buf[(size_t) i] = x[(size_t) i] * (float) (0.5 - 0.5 * std::cos (2 * juce::MathConstants<double>::pi * i / N));
            fft.performFrequencyOnlyForwardTransform (buf.data());
            double low = 0, all = 0, cen = 0, logSum = 0, linSum = 0; int nFlat = 0;
            for (int b = 1; b < N / 2; ++b)
            {
                const double f = b * kSR / N;
                const double p = (double) buf[(size_t) b] * buf[(size_t) b] + 1e-20;
                all += p; cen += p * f;
                if (f < 1000.0) low += p;
                if (f > 3000.0 && f < 12000.0) { logSum += std::log (p); linSum += p; ++nFlat; }
            }
            const double lowShare = low / all, centroid = cen / all;
            const double flat = std::exp (logSum / nFlat) / (linSum / nFlat);      // 1 = white noise, ~0 = pure tones
            worstLow = juce::jmax (worstLow, lowShare);
            worstCentroid = juce::jmin (worstCentroid, centroid);
            worstFlat = juce::jmin (worstFlat, flat);
            const auto name = kit.name + " " + slot.name;
            CHECK (lowShare < 0.06, "%s: %.0f %% of its energy below 1 kHz", name.toRawUTF8(), lowShare * 100);
            CHECK (centroid > 4500.0 && centroid < 10500.0, "%s: centre %.0f Hz (a hat sits ~5-10 kHz)", name.toRawUTF8(), centroid);
            worstCentroidHigh = juce::jmax (worstCentroidHigh, centroid);
            CHECK (flat > 0.25, "%s: rings like a chord (flatness %.2f)", name.toRawUTF8(), flat);
        }
    }
    std::printf ("    worst: %.1f %% below 1 kHz, centres %.0f-%.0f Hz, flatness %.2f (1 = pure wash)\n", worstLow * 100, worstCentroid, worstCentroidHigh, worstFlat);
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    const auto demo = juce::SystemStats::getEnvironmentVariable ("THOREAU_DEMO", {});
    if (demo.isNotEmpty())
    {
        const int pieceKit = juce::SystemStats::getEnvironmentVariable ("THOREAU_DEMO_PIECES", "-1").getIntValue();
        if (pieceKit >= 0)
        {
            // each drum of one kit on its own, raw (no mics / room): a hit, then a soft one
            juce::File (demo).createDirectory();
            for (int t = 0; t < kTracks; ++t)
            {
                const auto& slot = kitAt (pieceKit).slots[(size_t) t];
                AcousticDrum d; d.prepare (kSR);
                juce::AudioBuffer<float> b (1, (int) (kSR * 3));
                b.clear();
                d.start (slot.model, {}, 1.0f, 0.0, 7u);
                for (int pos = 0; pos < b.getNumSamples(); pos += 256)
                {
                    if (pos == (int) (kSR * 1.5) / 256 * 256) d.excite (0.4f, 0.0, 8u);
                    d.render (b.getWritePointer (0) + pos, juce::jmin (256, b.getNumSamples() - pos));
                }
                auto f = juce::File (demo).getChildFile ("piece " + juce::String (t).paddedLeft ('0', 2) + " " + juce::File::createLegalFileName (slot.name) + ".wav");
                f.deleteFile();
                juce::WavAudioFormat wav;
                std::unique_ptr<juce::OutputStream> os (f.createOutputStream());
                if (auto* w = wav.createWriterFor (os.get(), kSR, 1, 24, {}, 0)) { os.release(); std::unique_ptr<juce::AudioFormatWriter> wr (w); wr->writeFromAudioSampleBuffer (b, 0, b.getNumSamples()); }
            }
            return 0;
        }
        if (juce::SystemStats::getEnvironmentVariable ("THOREAU_DEMO_HATS", {}).isNotEmpty())
        {
            // hats alone (two bars), then the kit's beat (two bars)
            juce::File (demo).createDirectory();
            for (int k : { 7, 1, 9, 10, 12 })
            {
                Project pr; pr.setKit (k);
                for (auto& p : pr.patterns) p.clear();
                pr.loadGroove (k, 1, 2);
                auto& h = pr.patterns[0];
                h.length = 16; h.stepsPerBeat = 4;
                const char* cl = "xgogxgogxgogxg..";
                for (int i = 0; i < 16; ++i)
                    h.steps[4][(size_t) i] = (uint8_t) (cl[i] == 'x' ? 3 : cl[i] == 'o' ? 2 : cl[i] == 'g' ? 1 : 0);
                h.steps[5][14] = 2;
                pr.bpm = 100.0; pr.swing = 50.0f;
                pr.setSong ({ { 0, 2 }, { 1, 2 } });
                DrumEngine e (pr); e.prepare (kSR, 512, 0); e.setPlayMode (PlayMode::song); e.play();
                const double secs = pr.songBeats() * 60.0 / 100.0 + 1.5;
                juce::AudioBuffer<float> all (2, (int) (secs * kSR)), out (2, 512);
                for (int pos = 0; pos < all.getNumSamples(); pos += 512)
                {
                    const int m = juce::jmin (512, all.getNumSamples() - pos);
                    e.process (out.getArrayOfWritePointers(), 2, m);
                    for (int ch = 0; ch < 2; ++ch) all.copyFrom (ch, pos, out, ch, 0, m);
                }
                auto f = juce::File (demo).getChildFile ("hats " + juce::String (k).paddedLeft ('0', 2) + ".wav");
                f.deleteFile();
                juce::WavAudioFormat wav;
                std::unique_ptr<juce::OutputStream> os (f.createOutputStream());
                if (auto* w = wav.createWriterFor (os.get(), kSR, 2, 24, {}, 0)) { os.release(); std::unique_ptr<juce::AudioFormatWriter> wr (w); wr->writeFromAudioSampleBuffer (all, 0, all.getNumSamples()); }
            }
            return 0;
        }
        renderDemos (juce::File (demo));
        return 0;
    }
    if (juce::SystemStats::getEnvironmentVariable ("THOREAU_ONLY_SYNC", {}).isNotEmpty())
    {
       #if THOREAU_HAVE_HAWTHORNE
        testTapeSyncEndToEnd();
       #endif
        std::printf (failures == 0 ? "\nALL TESTS PASSED\n" : "\n%d FAILURE(S)\n", failures);
        return failures == 0 ? 0 : 1;
    }
    testVoices();
    testSequencer();
    testInternalClock();
    testKitPieces();
    testKitRoomsAndBus();
    testHats();
   #if THOREAU_HAVE_HAWTHORNE
    testTapeSyncEndToEnd();
   #else
    std::printf ("[4] skipped: build with -DHAWTHORNE_SOURCE=<Hawthorne checkout> for the end-to-end sync test\n");
   #endif
    std::printf (failures == 0 ? "\nALL TESTS PASSED\n" : "\n%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
