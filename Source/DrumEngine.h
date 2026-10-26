#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include "Project.h"
#include "TapeChase.h"
#include "AcousticDrum.h"
#include "KitBus.h"
#include "HawthorneLink/NetSender.h"

namespace thoreau
{

enum class ClockSource { internal, hawthorne };
enum class PlayMode { pattern, song };

//==============================================================================
/**
    Finds the hits that fall in a span of beats (beat 0 = the downbeat of bar 1).
    Pattern mode loops one pattern from patternStart; song mode plays the chain.
*/
struct Sequencer
{
    struct Hit { int track; float velocity; double beat; int pattern; int step; };

    /** Calls fn (Hit) for every hit with from <= beat < to, in time order per pattern pass. */
    template <typename Fn>
    static void hitsInPattern (const Project& pr, int pattern, double patternStart, double from, double to, Fn&& fn)
    {
        const auto& pt = pr.patterns[(size_t) juce::jlimit (0, kPatterns - 1, pattern)];
        const int len = juce::jlimit (1, kMaxSteps, pt.length.load());
        const int spb = juce::jmax (1, pt.stepsPerBeat.load());
        const double shift = swingShift (pr, spb);
        const double a = (from - patternStart) * spb, b = (to - patternStart) * spb;
        for (auto g = (juce::int64) std::floor (a) - 1; (double) g < b; ++g)
        {
            if (g < 0) continue;
            const int step = (int) (g % len);
            const double beat = patternStart + ((double) g + ((step & 1) ? shift : 0.0)) / spb;
            if (beat < from || beat >= to) continue;
            for (int t = 0; t < kTracks; ++t)
                if (const int v = pt.steps[(size_t) t][(size_t) step].load(std::memory_order_relaxed))
                    fn (Hit { t, stepVelocity (v), beat, pattern, step });
        }
    }

    template <typename Fn>
    static void hitsInSong (const Project& pr, double from, double to, Fn&& fn)
    {
        const int n = juce::jlimit (0, kSongMax, pr.songLength.load());
        if (n == 0) return;
        const double total = pr.songBeats();
        if (total <= 0.0) return;

        double offset = 0.0;           // song passes (when looping)
        if (pr.songLoops.load() && from >= total)
            offset = std::floor (from / total) * total;

        for (int pass = 0; pass < 2; ++pass, offset += total)
        {
            double start = offset;
            for (int i = 0; i < n && start < to; ++i)
            {
                const int p = juce::jlimit (0, kPatterns - 1, pr.songPattern[(size_t) i].load());
                const double len = pr.patterns[(size_t) p].beats() * juce::jmax (1, pr.songRepeats[(size_t) i].load());
                if (start + len > from)
                    hitsInPattern (pr, p, start, juce::jmax (from, start), juce::jmin (to, start + len), fn);
                start += len;
            }
            if (! pr.songLoops.load() || start >= to) break;
        }
    }

    /** Where in the song a beat is: entry index and pass (-1 = past the end / before bar 1). */
    static int songEntryAt (const Project& pr, double beat, int& repeat, double& entryStart);

    static double swingShift (const Project& pr, int stepsPerBeat) noexcept
    {
        // swing delays every second step of an even grid: 50 % straight, 66.7 % triplet feel, 75 % dotted
        if (stepsPerBeat % 2 != 0) return 0.0;
        return (juce::jlimit (50.0f, 75.0f, pr.swing.load()) - 50.0) / 50.0;
    }
};

//==============================================================================
/**
    THOREAU's audio engine: voices, sequencer, mixer, Hawthorne Link output and tape chase.
    Public methods are message-thread only unless stated.
*/
class DrumEngine : public juce::AudioIODeviceCallback,
                   public juce::MidiInputCallback
{
public:
    explicit DrumEngine (Project&);
    ~DrumEngine() override;

    Project& project;
    hawthorne::NetSender netOut;

    //==========================================================================
    // Transport
    void setClockSource (ClockSource);
    ClockSource getClockSource() const noexcept { return clock.load(); }
    void setPlayMode (PlayMode);
    PlayMode getPlayMode() const noexcept       { return mode.load(); }

    /** Internal clock: start from bar 1 / stop. (Following Hawthorne, the tape decides.) */
    void play();
    void stop();
    bool isRolling() const noexcept             { return uiRolling.load(); }

    /** Pattern mode: changes at the end of the current pattern while playing, at once when stopped. */
    void selectPattern (int p);
    int getPattern() const noexcept             { return currentPattern.load(); }
    int getQueuedPattern() const noexcept       { return queuedPattern.load(); }

    /** Play a drum now (pads, MIDI). Any thread. */
    void audition (int track, float velocity) noexcept;

    /** Live record: pads / MIDI played while rolling go into the pattern, on the nearest step. */
    void setRecording (bool r) noexcept        { liveRecord = r; }
    bool isLiveRecording() const noexcept      { return liveRecord.load(); }
    struct RecordedHit { int pattern, step, track, value; };
    bool popRecordedHit (RecordedHit&);

    //==========================================================================
    // Hawthorne Link
    bool setNetworkEnabled (bool on, const juce::String& name, juce::String& error);
    bool isNetworkEnabled() const noexcept     { return networkOn.load(); }
    /** Message thread: after project.setKit() (or loading a project), so the mics and room follow. */
    void kitChanged()                          { bus.setKit (project.kit.load()); }

    /** 16 extra channels after the stereo mix: each drum's close mic (12), overheads (2), room and echo (2).
        Takes effect when the audio restarts. */
    void setIndividualOuts (bool on) noexcept  { individualOuts = on; }
    bool getIndividualOuts() const noexcept    { return individualOuts.load(); }
    int getNetworkChannels() const noexcept    { return netChannels.load(); }
    /** Also play through this PC's audio outputs. */
    void setLocalOutput (bool on) noexcept     { localOut = on; }
    bool getLocalOutput() const noexcept       { return localOut.load(); }

    //==========================================================================
    // For the display (any thread)
    double getBeat() const noexcept            { return uiBeat.load(); }
    double getBpm() const noexcept             { return uiBpm.load(); }
    int getBeatsPerBar() const noexcept        { return uiBeatsPerBar.load(); }
    bool isTapeSynced() const noexcept         { return uiSynced.load(); }
    bool isTapeRecording() const noexcept      { return uiTapeRec.load(); }
    bool isReelTempo() const noexcept          { return uiReelTempo.load(); }
    double getTapePosition() const noexcept    { return uiTapePos.load(); }
    int getPlayingPattern() const noexcept     { return uiPattern.load(); }
    int getPlayingStep() const noexcept        { return uiStep.load(); }
    int getSongEntry() const noexcept          { return uiEntry.load(); }
    float popTrackPeak (int t) noexcept        { return trackPeak[(size_t) t].exchange (0.0f); }
    float popMasterPeak (int ch) noexcept      { return masterPeak[(size_t) (ch & 1)].exchange (0.0f); }
    float popTrackHit (int t) noexcept         { return trackHit[(size_t) t].exchange (0.0f); }
    double getSampleRate() const noexcept      { return sampleRate.load(); }
    juce::int64 getCatchUps() const noexcept   { return catchUps.load(); }

    //==========================================================================
    // Audio (device-independent entry points, used by the callback and the tests)
    void prepare (double sampleRate, int maxBlockSize, int outputLatency);
    void process (float* const* outputs, int numOutputs, int numSamples) noexcept;

    void audioDeviceIOCallbackWithContext (const float* const*, int, float* const* outputs, int numOutputs,
                                           int numSamples, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;

    /** Hits scheduled against the tape catch up this far: a hit up to 150 ms before the tape
        started (or jumped) still plays, from part-way through. */
    static constexpr double catchUpSeconds = 0.15;

private:
    struct TrackVoices
    {
        std::array<Voice, 2> v; int next = 0;            // analogue synth / samples
        std::array<AcousticDrum, 2> drums; int cur = 0;  // acoustic: one rings on while a changed sound starts
    };
    void trigger (int track, float velocity, double onset) noexcept;
    void renderChunk (float* const* outputs, int numOutputs, int start, int n) noexcept;
    void scheduleSpan (double scanFrom, double beat0, double beat1, int n, bool catchUp) noexcept;
    void scheduleHit (const Sequencer::Hit&, double beat0, double beat1, int n) noexcept;
    void recordHit (int track, float velocity, double beat) noexcept;

    std::array<TrackVoices, kTracks> voices;
    KitBus bus;
    std::array<KitBus::Send, kTracks> sends;
    uint32_t hitCount = 1;
    juce::AudioBuffer<float> trackBuf, mixBuf, netBuf;
    std::vector<const float*> netPtrs;
    std::atomic<double> sampleRate { 48000.0 };
    int maxBlock = 512;

    std::atomic<ClockSource> clock { ClockSource::internal };
    std::atomic<PlayMode> mode { PlayMode::pattern };
    std::atomic<bool> playRequested { false }, stopRequested { false };
    std::atomic<int> currentPattern { 0 }, queuedPattern { -1 };
    std::atomic<bool> liveRecord { false }, networkOn { false }, individualOuts { false }, localOut { true };
    std::atomic<int> netChannels { 2 };

    // audio thread state
    bool internalRolling = false;
    double internalBeat = 0.0;
    double patternStart = 0.0;
    TapeChase chase;
    double lastSpanBeat0 = 0.0, lastSpanBeat1 = 0.0;    // for live record
    bool lastSpanRolling = false;
    int lastSpanN = 1;

    // UI → audio: auditions
    struct Audition { int track; float velocity; };
    juce::AbstractFifo auditionFifo { 256 };
    std::array<Audition, 256> auditions;
    // audio → UI: recorded hits
    juce::AbstractFifo recordFifo { 512 };
    std::array<RecordedHit, 512> recorded;
    // MIDI → audio
    juce::MidiMessageCollector midiCollector;
    juce::MidiBuffer midiBlock;

    std::atomic<double> uiBeat { 0.0 }, uiBpm { 120.0 }, uiTapePos { 0.0 };
    std::atomic<int> uiBeatsPerBar { 4 }, uiPattern { 0 }, uiStep { -1 }, uiEntry { -1 };
    std::atomic<bool> uiRolling { false }, uiSynced { false }, uiTapeRec { false }, uiReelTempo { false };
    std::array<std::atomic<float>, kTracks> trackPeak {}, trackHit {};
    std::array<std::atomic<float>, 2> masterPeak {};
    std::atomic<juce::int64> catchUps { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumEngine)
};

} // namespace thoreau
