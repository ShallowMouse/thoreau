#pragma once

#include <juce_dsp/juce_dsp.h>
#include "Kits.h"

namespace thoreau
{

//==============================================================================
/**
    The kit's mics and mix bus, after the drums:

      close mics   each drum, panned (its own channel on the individual outs)
      overheads    every drum, wider, a touch later, low end cut
      room         every drum into the kit's room (convolution with a room built from the
                   kit's ReverbDef: early reflections + a decay that darkens), crushed and
                   gated for the 80s kit; plus the kit's echo (slapback, AMS ...)
      bus          shelving EQ and cuts, compressor (in line or New York parallel), tape

    With THOREAU ANALOGUE's defaults (overheads and room off, no compression or tape, flat
    bus) the mix is exactly the close mics: old projects sound as they did.
*/
class KitBus
{
public:
    KitBus();

    struct Send { float pan = 0, close = 1, oh = 0, room = 0, fx = 0; };
    struct Knobs { float closeDb = 0, ohDb = -60, roomDb = -60, comp = 0, tape = 0; };

    /** Before the audio starts: allocates, builds the room. */
    void prepare (double sampleRate, int maxBlockSize);
    /** Message thread: use this kit's room, echo, compressor and bus. */
    void setKit (int kitIndex);
    int getKit() const noexcept { return kitIndex.load(); }

    /** Audio thread. tracks: kTracks mono signals (after their level); n <= maxBlockSize.
        Writes the stereo mix and the stems for Hawthorne. */
    void process (const float* const* tracks, const std::array<Send, kTracks>& sends, const Knobs& knobs,
                  float* mixL, float* mixR, int n) noexcept;

    const float* overheadStem (int ch) const noexcept { return ohBuf.getReadPointer (ch & 1); }
    const float* roomStem (int ch) const noexcept     { return roomBuf.getReadPointer (ch & 1); }

    /** The impulse response of a kit's room (also used by the tests). */
    static juce::AudioBuffer<float> makeRoom (const ReverbDef&, double sampleRate, bool mono);

private:
    struct Biquad
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        void set (double b0_, double b1_, double b2_, double a0, double a1_, double a2_) noexcept;
        void highpass (double sr, double hz) noexcept;
        void lowpass (double sr, double hz) noexcept;
        void lowShelf (double sr, double hz, double db) noexcept;
        void highShelf (double sr, double hz, double db) noexcept;
        void reset() noexcept { z1 = z2 = 0; }
        inline float process (float x) noexcept
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    struct Compressor
    {
        float env = 0.0f, att = 0.0f, rel = 0.0f;
        void set (double sr, double attackMs, double releaseMs) noexcept;
        /** Gain (linear) for a stereo-linked peak level x. */
        float gain (float x, float thresholdDb, float ratio, float kneeDb) noexcept;
    };

    void configure (const KitDef&) noexcept;       // audio thread, when the kit changes

    double sr = 48000.0;
    int maxBlock = 512;
    std::atomic<int> kitIndex { 0 };
    int configuredKit = -1;
    bool mono = false;

    juce::dsp::Convolution convolution { juce::dsp::Convolution::NonUniform { 512 } };
    juce::AudioBuffer<float> ohBuf, roomBuf, sendBuf, fxBuf, dryBuf;
    std::vector<float> ohDelay[2], echo[2];
    // overheads: a little diffusion (allpasses) so they don't line up sample-perfect with the close
    // mics - real overheads hear each drum through reflections, so there's no regular comb filter
    struct Allpass { std::vector<float> buf; int pos = 0, len = 1; float g = 0.55f;
                     inline float process (float x) noexcept { const float d = buf[(size_t) pos]; const float v = x + g * d; buf[(size_t) pos] = v; pos = (pos + 1) % len; return d - g * v; } };
    std::array<std::array<Allpass, 3>, 2> ohDiffuse;
    int ohWrite = 0, ohDelaySamples = 1, echoWrite = 0, echoLen = 1;

    std::array<Biquad, 2> ohHp, roomHp, lowShelf, highShelf, lowCut, highCut, echoLp;
    bool useLowShelf = false, useHighShelf = false, useLowCut = false, useHighCut = false;
    Compressor busComp, roomComp;
    float gate = 0.0f, gateKeyPeak = 0.0f, gateAtt = 0, gateRel = 0, keyDecay = 0;
    int gateHold = 0;
    std::array<float, 2> tapeLp {};
    float tapeCoef = 1.0f;
    float lastTape = -1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KitBus)
};

} // namespace thoreau
