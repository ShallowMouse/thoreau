#pragma once

#include "Kits.h"

namespace thoreau
{

//==============================================================================
/**
    A physically modelled acoustic drum or cymbal.

    The instrument is a bank of resonant modes - the vibration patterns of a drum
    head (Bessel modes of a circular membrane, weighted by where it's struck and how
    well each radiates) or the dense inharmonic partials of a cymbal - set ringing by
    the force of the stick, beater or mallet. A hard, fast hit is a short force pulse
    (bright: it excites the upper modes); a soft hit is a longer one (darker). On top:
    tension pitch glide, snare wires driven by the head's motion, the stick's tick,
    cymbal sizzle and the wash building up after a crash.

    A drum that's hit again keeps ringing and takes the new hit on top (excite()),
    like the real thing. Everything is deterministic for a given seed, starts at a
    fractional sample (sub-sample timing) and can start part-way through (catching up
    with a tape that's already rolling).
*/
class AcousticDrum
{
public:
    void prepare (double sampleRate) noexcept;

    /** A fresh drum. onset = samples into the next block (fractional; negative = it was hit that long ago). */
    void start (const DrumModel&, const VoiceParams&, float velocity, double onset, uint32_t seed) noexcept;
    /** Another hit on the same drum (it keeps ringing). onset >= 0. */
    void excite (float velocity, double onset, uint32_t seed) noexcept;
    /** Grab it (cymbal choke, hi-hat closing): it dies away over `seconds` from `at` samples in. */
    void choke (double at, double seconds) noexcept;

    /** Adds n samples. Returns false once it has died away. */
    bool render (float* out, int n) noexcept;

    bool isActive() const noexcept   { return active; }
    bool isChoked() const noexcept   { return chokeAt >= 0.0 || chokeGain < 1.0f; }
    bool matches (const DrumModel& m, const VoiceParams& p) const noexcept
    {
        return m == model && p.tune == params.tune && p.decay == params.decay && p.tone == params.tone && p.snap == params.snap;
    }

    static constexpr int maxModes = 176;

private:
    struct Biquad
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        void bandpass (double sr, double hz, double q) noexcept;
        void highpass (double sr, double hz, double q) noexcept;
        inline float process (float x) noexcept
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    struct Pulse { double at = 0, width = 1; float a = 0, b = 0; };               // force pulse into streams A / B
    struct Burst { double at = 0; float amp = 0, kd = 0, ka = 1, d = 0, att = 0; int filter = 0; bool started = false; };

    void setupMembrane (float f0, float ring, float damp, float bright, float radiusA, float radiusB, int count, float stretch);
    void addMode (double hz, double t60, float bA, float bB, bool high = false) noexcept;
    void setupMetal (float fLow, float fHigh, int count, float slope, float ring1k, float decayExp, float swellHz, uint32_t layoutSeed);
    void addPulse (double at, double widthSeconds, float a, float b) noexcept;
    void addBurst (double at, float amp, double decaySeconds, double attackSeconds, int filter) noexcept;
    void updateGlide() noexcept;
    float nextRandom() noexcept { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (float) (rng & 0xffffff) / 16777216.0f; }
    float noise() noexcept { return nextRandom() * 2.0f - 1.0f; }
    void renderChunk (float* out, int n) noexcept;

    double sr = 48000.0;
    bool active = false;
    DrumModel model;
    VoiceParams params;

    // effective sound (model + knobs)
    float pitch = 100, ring = 0.5f, bright = 0.5f, hard = 0.5f, extra = 0.5f;

    // modes (structure of arrays)
    int numModes = 0, numHigh = 0;               // modes [numModes - numHigh, numModes) are the "wash" group
    alignas(16) float re[maxModes] {}, im[maxModes] {}, pr[maxModes] {}, pi[maxModes] {}, bA[maxModes] {}, bB[maxModes] {};
    float w0[maxModes] {}, r0[maxModes] {};
    int numGlide = 0;                             // the first numGlide modes bend with head tension
    float glide = 0, glideCoef = 0, glideAmount = 0;
    int glideCountdown = 0;

    // excitation
    std::array<Pulse, 32> pulses;
    int numPulses = 0;
    std::array<Burst, 32> bursts;
    int numBursts = 0;
    std::array<Biquad, 8> filters;
    std::array<float, 8> filterGain {};
    enum { fTick = 0, fSizzle = 1, fWires = 2, fBody = 3, fClap = 4 };

    // wires
    float wireLevel = 0, wireEnv = 0, wireAtt = 0, wireRel = 0, wireRef = 1;

    // wash (cymbals): the high partials bloom after the hit
    float swell = 1, swellFloor = 1, swellCoef = 0;
    float trash = 0;
    // roughness: the partials' level wobbles with band-limited noise, smearing each one into a band
    // (a real cymbal has thousands of modes; this makes ~100 sound like a wash, not a chord)
    float rough = 0, roughLp1 = 0, roughLp2 = 0, roughCoef = 0;
    bool roughLow = false;          // apply to the main group rather than the wash group

    // hi-hats: two plates clamped together sound like a bright band of noise (coloured by a few
    // broad resonances that make each pair of hats its own), not a ringing chord
    static constexpr int hatBands = 10;
    bool hat = false;
    std::array<Biquad, hatBands> hatBand;
    std::array<float, hatBands> hatGain {};
    Biquad hatHp1, hatHp2;
    float hatNorm = 1, szNorm = 1;
    float hatFast = 0, hatSlow = 0, hatSz = 0, hatFastK = 0, hatSlowK = 0, hatSzK = 0;
    float hatSlowShare = 0.4f, hatSzMix = 0.5f;
    float wobblePhase = 0, wobbleInc = 0, wobbleDepth = 0;

    // output
    float outGain = 1, lp = 0, lpCoef = 1, hpState = 0, hpCoef = 0;
    float chokeGain = 1, chokeCoef = 1;
    double chokeAt = -1.0;
    double now = 0.0;                            // samples since start
    double lastHit = 0.0;
    float peakAmp = 0;                           // for the end-of-sound test
    int quietChunks = 0;
    uint32_t rng = 1;
};

} // namespace thoreau
