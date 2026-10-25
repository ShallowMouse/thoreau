#pragma once

#include "DrumVoices.h"
#include <juce_data_structures/juce_data_structures.h>
#include <memory>
#include <vector>

namespace thoreau
{

constexpr int kPatterns = 16;       // A .. P
constexpr int kMaxSteps = 64;
constexpr int kSongMax  = 128;

/** Step values: 0 off, 1 soft, 2 normal, 3 accent. */
inline float stepVelocity (int v) noexcept { return v <= 0 ? 0.0f : v == 1 ? 0.45f : v == 2 ? 0.78f : 1.0f; }
inline juce::String patternName (int p) { return juce::String::charToString ((juce::juce_wchar) ('A' + juce::jlimit (0, kPatterns - 1, p))); }

//==============================================================================
/** A track's sound and mixer settings. Atomics: written by the UI, read by the audio thread. */
struct TrackSettings
{
    std::atomic<float> tune { 0.0f }, decay { 0.5f }, tone { 0.5f }, snap { 0.5f };
    std::atomic<float> levelDb { 0.0f }, pan { 0.0f };
    std::atomic<bool> mute { false }, solo { false };
    std::atomic<int> midiNote { 36 };
    std::atomic<const Sample*> sample { nullptr };      // owned by the Project's SampleStore

    VoiceParams voiceParams() const noexcept { return { tune.load(), decay.load(), tone.load(), snap.load() }; }
};

/** One pattern: up to 64 steps per track. */
struct Pattern
{
    std::atomic<int> length { 16 };                 // steps
    std::atomic<int> stepsPerBeat { 4 };            // 4 = 16ths, 3 = 8th triplets, 2 = 8ths, 8 = 32nds
    std::array<std::array<std::atomic<uint8_t>, kMaxSteps>, kTracks> steps;

    Pattern() { clear(); }
    void clear() noexcept { for (auto& t : steps) for (auto& s : t) s.store (0); }
    bool isEmpty() const noexcept { for (auto& t : steps) for (auto& s : t) if (s.load() != 0) return false; return true; }
    void copyFrom (const Pattern& o) noexcept
    {
        length = o.length.load(); stepsPerBeat = o.stepsPerBeat.load();
        for (int t = 0; t < kTracks; ++t) for (int s = 0; s < kMaxSteps; ++s) steps[(size_t) t][(size_t) s] = o.steps[(size_t) t][(size_t) s].load();
    }
    /** Length in beats. */
    double beats() const noexcept { return (double) juce::jmax (1, length.load()) / (double) juce::jmax (1, stepsPerBeat.load()); }
};

/** Song: a chain of patterns, each played a number of times. */
struct SongEntry { int pattern = 0; int repeats = 1; };

//==============================================================================
/**
    Everything THOREAU saves: kit, patterns, song, tempo. Real-time safe to read from
    the audio thread (atomics; samples are never freed while they might be playing).
*/
class Project
{
public:
    Project();

    std::array<TrackSettings, kTracks> tracks;
    std::array<Pattern, kPatterns> patterns;

    std::atomic<double> bpm { 120.0 };
    std::atomic<int> beatsPerBar { 4 };
    std::atomic<float> swing { 50.0f };          // 50 (straight) .. 75 %
    std::atomic<float> masterDb { 0.0f };

    // Song (audio thread reads songLength entries; the UI edits them in place)
    std::array<std::atomic<int>, kSongMax> songPattern, songRepeats;
    std::atomic<int> songLength { 0 };
    std::atomic<bool> songLoops { false };

    std::vector<SongEntry> getSong() const;
    void setSong (const std::vector<SongEntry>&);
    /** Total length of the song in beats. */
    double songBeats() const noexcept;

    //==========================================================================
    // Samples (message thread)
    bool loadSample (int track, const juce::File&, juce::String& error);
    void clearSample (int track);
    juce::File getSampleFile (int track) const;
    /** Frees samples nobody can still be playing (call from a timer). */
    void collectGarbage();

    //==========================================================================
    void resetToDefault();                 // default kit, a starter beat in A
    std::unique_ptr<juce::XmlElement> toXml() const;
    bool fromXml (const juce::XmlElement&, juce::String& error);

    bool save (const juce::File&, juce::String& error) const;
    bool load (const juce::File&, juce::String& error);

    /** Bumped by every change the UI should redraw for (message thread). */
    std::atomic<int> version { 0 };
    void changed() noexcept { version.fetch_add (1); }

private:
    std::array<std::unique_ptr<Sample>, kTracks> ownedSamples;
    struct Dead { std::unique_ptr<Sample> sample; juce::uint32 at; };
    std::vector<Dead> graveyard;
};

} // namespace thoreau
