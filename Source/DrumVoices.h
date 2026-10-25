#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <array>
#include <atomic>

namespace thoreau
{

constexpr int kTracks = 12;

/** The twelve instruments, in track order. */
enum class Instrument { kick, snare, clap, rim, closedHat, openHat, lowTom, midTom, highTom, crash, ride, cowbell };

inline Instrument instrumentOf (int track) noexcept { return (Instrument) juce::jlimit (0, kTracks - 1, track); }
juce::String instrumentName (int track);          // "KICK", "SNARE" ...
juce::String instrumentShortName (int track);     // "BD", "SD" ...
int defaultMidiNote (int track);                  // General MIDI drum map

/** A voice's sound controls (0..1 except tune, in semitones). */
struct VoiceParams
{
    float tune = 0.0f;      // -12 .. +12 semitones
    float decay = 0.5f;
    float tone = 0.5f;
    float snap = 0.5f;
};

/** A loaded sample (mono; mixed down on load). Immutable once made. */
struct Sample
{
    juce::AudioBuffer<float> audio;
    double sampleRate = 48000.0;
    juce::File file;

    static std::unique_ptr<Sample> load (const juce::File&, juce::String& error);
};

//==============================================================================
/**
    One drum hit: an analogue-style synthesis of the instrument, or a sample.
    Every voice is a function of time since its onset, so a hit can start at a
    fractional sample position (sub-sample timing) or part-way through (when
    THOREAU catches up with tape that is already rolling).
*/
class Voice
{
public:
    void prepare (double sampleRate);

    /** onset = position of the hit in the next block, in samples (fractional; negative =
        it started that long ago). */
    void trigger (Instrument, const VoiceParams&, float velocity, const Sample* sample, double onset) noexcept;

    /** Fade out over a few milliseconds (choke / retrigger), starting `at` samples into the next block. */
    void release (double at = 0.0) noexcept;
    bool isActive() const noexcept { return active; }

    /** Adds n samples to out. */
    void render (float* out, int n) noexcept;

private:
    struct Svf
    {
        float ic1 = 0, ic2 = 0, g = 0, k = 1, a1 = 0, a2 = 0, a3 = 0;
        void set (double sr, double hz, double q) noexcept;
        void reset() noexcept { ic1 = ic2 = 0; }
        // returns low, band, high
        inline void process (float v0, float& lp, float& bp, float& hp) noexcept
        {
            const float v3 = v0 - ic2;
            const float v1 = a1 * ic1 + a2 * v3;
            const float v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0f * v1 - ic1;
            ic2 = 2.0f * v2 - ic2;
            lp = v2; bp = v1; hp = v0 - k * v1 - v2;
        }
    };

    float noise() noexcept
    {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        return (float) (int32_t) rng * (1.0f / 2147483648.0f);
    }

    float synth (double t) noexcept;       // one sample of the synthesised instrument at time t (s)
    float sampled (double t) noexcept;     // one sample of the loaded sample at time t
    static float square (double phase, double inc) noexcept;   // band-limited (polyBLEP)

    double sr = 48000.0, dt = 1.0 / 48000.0;
    bool active = false;
    Instrument inst = Instrument::kick;
    VoiceParams p;
    float vel = 1.0f, gain = 1.0f;
    const Sample* sample = nullptr;
    double delay = 0.0;       // samples until the onset
    double t = 0.0;           // seconds since the onset
    float fade = 1.0f, fadeStep = 0.0f;
    double releaseAt = -1.0;  // samples into the next block where the fade starts (-1 = none)
    uint32_t rng = 0x9e3779b9u;

    // instrument state, set at trigger
    double f0 = 0, f1 = 0, tauP = 0, tauA = 0, tauB = 0, tauN = 0, end = 0;
    std::array<double, 6> metalF {}, metalPh {};
    double ph1 = 0, ph2 = 0;
    Svf f1s, f2s, f3s;
    float lpState = 0.0f, toneCoef = 0.0f;
};

} // namespace thoreau
