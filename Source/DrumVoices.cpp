#include "DrumVoices.h"

namespace thoreau
{

namespace
{
    constexpr double twoPi = juce::MathConstants<double>::twoPi;

    const char* const names[kTracks]  = { "KICK", "SNARE", "CLAP", "RIM", "CL HAT", "OP HAT",
                                          "LO TOM", "MID TOM", "HI TOM", "CRASH", "RIDE", "COWBELL" };
    const char* const shorts[kTracks] = { "BD", "SD", "CP", "RS", "CH", "OH", "LT", "MT", "HT", "CY", "RD", "CB" };
    const int gmNotes[kTracks]        = { 36, 38, 39, 37, 42, 46, 41, 45, 48, 49, 51, 56 };

    inline double semis (float s) noexcept { return std::pow (2.0, (double) s / 12.0); }
}

juce::String instrumentName (int track)      { return names[juce::jlimit (0, kTracks - 1, track)]; }
juce::String instrumentShortName (int track) { return shorts[juce::jlimit (0, kTracks - 1, track)]; }
int defaultMidiNote (int track)              { return gmNotes[juce::jlimit (0, kTracks - 1, track)]; }

//==============================================================================
std::unique_ptr<Sample> Sample::load (const juce::File& file, juce::String& error)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (file));
    if (r == nullptr)
    {
        error = "Couldn't read " + file.getFileName() + " (WAV, AIFF, FLAC or OGG).";
        return nullptr;
    }
    const auto maxLen = (juce::int64) (r->sampleRate * 30.0);
    if (r->lengthInSamples <= 0 || r->lengthInSamples > maxLen)
    {
        error = file.getFileName() + " is empty or longer than 30 seconds.";
        return nullptr;
    }

    auto s = std::make_unique<Sample>();
    s->file = file;
    s->sampleRate = r->sampleRate;
    const int len = (int) r->lengthInSamples;
    const int ch = (int) juce::jmax (1u, r->numChannels);
    juce::AudioBuffer<float> tmp (ch, len);
    r->read (&tmp, 0, len, 0, true, true);
    s->audio.setSize (1, len + 4);
    s->audio.clear();
    for (int c = 0; c < ch; ++c)
        s->audio.addFrom (0, 0, tmp, c, 0, len, 1.0f / (float) ch);
    return s;
}

//==============================================================================
void Voice::Svf::set (double sampleRate, double hz, double q) noexcept
{
    hz = juce::jlimit (10.0, sampleRate * 0.45, hz);
    g = (float) std::tan (juce::MathConstants<double>::pi * hz / sampleRate);
    k = (float) (1.0 / juce::jmax (0.1, q));
    a1 = 1.0f / (1.0f + g * (g + k));
    a2 = g * a1;
    a3 = g * a2;
}

float Voice::square (double phase, double inc) noexcept
{
    // phase in cycles [0,1); naive square with polyBLEP at both edges
    double ph = phase - std::floor (phase);
    float v = ph < 0.5 ? 1.0f : -1.0f;
    auto blep = [] (double x, double d) -> double
    {
        if (x < d)        { x /= d; return x + x - x * x - 1.0; }
        if (x > 1.0 - d)  { x = (x - 1.0) / d; return x * x + x + x + 1.0; }
        return 0.0;
    };
    v += (float) blep (ph, inc);
    v -= (float) blep (std::fmod (ph + 0.5, 1.0), inc);
    return v;
}

void Voice::prepare (double sampleRate)
{
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    dt = 1.0 / sr;
    active = false;
}

void Voice::trigger (Instrument i, const VoiceParams& params, float velocity, const Sample* smp, double onset) noexcept
{
    inst = i;
    p = params;
    vel = juce::jlimit (0.0f, 1.0f, velocity);
    sample = smp;
    fade = 1.0f;
    fadeStep = 0.0f;
    releaseAt = -1.0;
    delay = onset;
    t = 0.0;
    if (delay < 0.0) { t = -delay * dt; delay = 0.0; }        // catching up: start part-way through
    rng = 0x9e3779b9u ^ (uint32_t) (juce::Time::getHighResolutionTicks() & 0xffffffff);
    f1s.reset(); f2s.reset(); f3s.reset();
    lpState = 0.0f;
    ph1 = ph2 = 0.0;
    metalPh.fill (0.0);
    active = true;

    const double tune = semis (p.tune);
    const double d = p.decay, tn = p.tone, sn = p.snap;
    gain = 0.35f + 0.65f * vel;

    if (sample != nullptr)
    {
        tauA = d >= 0.98 ? 1.0e9 : 0.02 + d * d * 3.0;
        end = (double) sample->audio.getNumSamples() / (sample->sampleRate * tune);
        toneCoef = (float) (1.0 - std::exp (-twoPi * (300.0 + tn * tn * 19000.0) / sr));
        return;
    }

    switch (inst)
    {
        case Instrument::kick:
            f0 = 48.0 * tune;
            f1 = f0 * (2.0 + 7.0 * sn);
            tauP = 0.006 + 0.03 * (1.0 - sn);
            tauA = 0.06 + d * d * 1.4;
            end = tauA * 9.0;
            f1s.set (sr, 2500.0, 0.7);
            break;

        case Instrument::snare:
            f0 = 185.0 * tune; f1 = 330.0 * tune;
            tauA = 0.03 + d * 0.12;
            tauN = 0.05 + d * d * 0.35;
            f1s.set (sr, 1200.0 + tn * 7000.0, 0.8);
            end = juce::jmax (tauA, tauN) * 9.0;
            break;

        case Instrument::clap:
            f1s.set (sr, (800.0 + tn * 1800.0) * tune, 1.6);
            tauN = 0.06 + d * d * 0.6;
            end = 0.04 + tauN * 9.0;
            break;

        case Instrument::rim:
            f0 = 1700.0 * tune; f1 = 470.0 * tune;
            tauA = 0.008 + d * 0.04;
            f1s.set (sr, 3000.0 + tn * 5000.0, 2.0);
            end = tauA * 10.0;
            break;

        case Instrument::closedHat:
        case Instrument::openHat:
        case Instrument::crash:
        {
            static const double hatF[6] = { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 };
            const double scale = (inst == Instrument::crash ? 1.45 : 1.0) * tune;
            for (int k = 0; k < 6; ++k) metalF[(size_t) k] = hatF[k] * scale;
            const bool crash = inst == Instrument::crash;
            f1s.set (sr, (crash ? 5500.0 : 7500.0) + tn * 4500.0, crash ? 0.7 : 1.2);
            f2s.set (sr, crash ? 3500.0 : 6500.0, 0.7);
            tauA = inst == Instrument::closedHat ? 0.012 + d * 0.09
                 : inst == Instrument::openHat   ? 0.10 + d * 0.9
                                                 : 0.5 + d * 2.6;
            end = tauA * (inst == Instrument::crash ? 6.0 : 9.0);
            break;
        }

        case Instrument::ride:
        {
            static const double rideF[6] = { 1.0, 1.4142, 1.83, 2.264, 2.71, 3.17 };
            for (int k = 0; k < 6; ++k) metalF[(size_t) k] = 410.0 * rideF[k] * tune;
            f1s.set (sr, 6000.0 + tn * 5000.0, 0.8);
            f2s.set (sr, 2500.0, 0.7);
            f0 = 1230.0 * tune;                    // bell
            tauA = 0.7 + d * 3.0;
            tauB = 0.12 + sn * 0.5;
            end = tauA * 5.0;
            break;
        }

        case Instrument::lowTom:
        case Instrument::midTom:
        case Instrument::highTom:
        {
            const double base = inst == Instrument::lowTom ? 82.0 : inst == Instrument::midTom ? 118.0 : 165.0;
            f0 = base * tune;
            f1 = f0 * (1.25 + 1.1 * sn);
            tauP = 0.05 + 0.08 * (1.0 - sn);
            tauA = 0.1 + d * d * 0.9;
            f1s.set (sr, f0 * 6.0, 0.8);
            end = tauA * 9.0;
            break;
        }

        case Instrument::cowbell:
            f0 = 545.0 * tune; f1 = 815.0 * tune;
            f1s.set (sr, (2000.0 + tn * 1600.0) * tune, 2.2);
            tauA = 0.06 + d * 0.5;
            end = 0.02 + tauA * 9.0;
            break;
    }
}

void Voice::release (double at) noexcept
{
    if (active && fadeStep == 0.0f && releaseAt < 0.0)
        releaseAt = juce::jmax (0.0, at);
}

float Voice::sampled (double time) noexcept
{
    const auto& a = sample->audio;
    const double pos = time * sample->sampleRate * semis (p.tune);
    const int len = a.getNumSamples() - 4;
    if (pos >= (double) len) { active = false; return 0.0f; }
    const int i = (int) pos;
    const float fr = (float) (pos - i);
    const float* d = a.getReadPointer (0);
    // 4-point Hermite
    const float xm1 = i > 0 ? d[i - 1] : 0.0f, x0 = d[i], x1 = d[i + 1], x2 = d[i + 2];
    const float c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    float v = ((c3 * fr + c2) * fr + c1) * fr + x0;
    lpState += toneCoef * (v - lpState);
    v = lpState;
    const float transient = time < 0.006 ? 1.0f + p.snap * 1.2f * (float) (1.0 - time / 0.006) : 1.0f;
    return v * transient * (float) std::exp (-time / tauA);
}

float Voice::synth (double time) noexcept
{
    float lp, bp, hp;
    const double tn = p.tone, sn = p.snap;

    switch (inst)
    {
        case Instrument::kick:
        {
            // closed-form phase of f(t) = f0 + (f1 - f0) e^(-t/tauP)
            const double ph = f0 * time + (f1 - f0) * tauP * (1.0 - std::exp (-time / tauP));
            float v = (float) (std::sin (twoPi * ph) * std::exp (-time / tauA));
            f1s.process (noise(), lp, bp, hp);
            v += (float) (lp * 0.9 * std::exp (-time / 0.0015) * (0.2 + sn));
            const float drive = 1.0f + (float) tn * 4.0f;
            return std::tanh (v * drive) / std::tanh (drive) * 0.95f;
        }

        case Instrument::snare:
        {
            const float body = (float) ((std::sin (twoPi * f0 * time) * 0.65 + std::sin (twoPi * f1 * time) * 0.35)
                                        * std::exp (-time / tauA) * (1.0 - 0.55 * sn));
            f1s.process (noise(), lp, bp, hp);
            const float n = (float) ((hp * 0.6 + bp * 0.7) * std::exp (-time / tauN) * (0.45 + 0.75 * sn));
            return (body + n) * 0.8f;
        }

        case Instrument::clap:
        {
            f1s.process (noise(), lp, bp, hp);
            const double spacing = 0.0085 + 0.004 * (1.0 - sn);
            double env = 0.0;
            for (int k = 0; k < 3; ++k)
            {
                const double tk = time - k * spacing;
                if (tk >= 0.0) env += std::exp (-tk / 0.0035) * (0.75 + 0.1 * k);
            }
            const double tTail = time - 3 * spacing;
            if (tTail >= 0.0) env += std::exp (-tTail / tauN);
            return (float) (bp * 1.6 * env);
        }

        case Instrument::rim:
        {
            const double e = std::exp (-time / tauA);
            float v = (float) ((std::sin (twoPi * f0 * time) * 0.6 + std::sin (twoPi * f1 * time) * 0.5) * e);
            f1s.process (noise(), lp, bp, hp);
            v += (float) (bp * std::exp (-time / 0.002) * (0.3 + sn * 0.6));
            return v * 0.8f;
        }

        case Instrument::closedHat:
        case Instrument::openHat:
        case Instrument::crash:
        case Instrument::ride:
        {
            float m = 0.0f;
            for (size_t k = 0; k < 6; ++k)
                m += square (metalF[k] * time, metalF[k] * dt);
            m *= 0.18f;
            const bool cymbal = inst == Instrument::crash || inst == Instrument::ride;
            const float n = noise() * (cymbal ? 0.45f : 0.25f + (float) sn * 0.35f);
            f1s.process (m + n, lp, bp, hp);
            float hp2l, hp2b, hp2h;
            f2s.process (bp + hp * 0.3f, hp2l, hp2b, hp2h);
            double env = std::exp (-time / tauA);
            if (! cymbal) env *= 1.0 + 1.5 * sn * std::exp (-time / 0.003);
            if (inst == Instrument::crash) env *= 1.0 - 0.6 * std::exp (-time / 0.01);     // swells in a little
            float v = (float) (hp2h * env) * 0.85f;
            if (inst == Instrument::ride)
                v += (float) ((std::sin (twoPi * f0 * time) * 0.35 + std::sin (twoPi * f0 * 1.52 * time) * 0.2)
                              * std::exp (-time / tauB) * (0.3 + sn));
            return v;
        }

        case Instrument::lowTom:
        case Instrument::midTom:
        case Instrument::highTom:
        {
            const double ph = f0 * time + (f1 - f0) * tauP * (1.0 - std::exp (-time / tauP));
            float v = (float) (std::sin (twoPi * ph) * std::exp (-time / tauA));
            f1s.process (noise(), lp, bp, hp);
            v += (float) (bp * (0.08 + 0.3 * tn) * std::exp (-time / (tauA * 0.25)));
            v += (float) (lp * 0.5 * std::exp (-time / 0.002) * sn);
            return v * 0.9f;
        }

        case Instrument::cowbell:
        {
            const float m = 0.5f * (square (f0 * time, f0 * dt) + square (f1 * time, f1 * dt));
            f1s.process (m, lp, bp, hp);
            const double env = time < 0.003 ? 1.0 : 0.35 * std::exp (-(time - 0.003) / tauA) + 0.65 * std::exp (-(time - 0.003) / 0.012);
            return (float) (bp * env * 1.4);
        }
    }
    return 0.0f;
}

void Voice::render (float* out, int n) noexcept
{
    if (! active)
        return;

    int i = 0;
    const int fadeFrom = releaseAt < 0.0 ? n + 1 : (int) releaseAt;
    if (releaseAt >= 0.0)
        releaseAt = fadeFrom < n ? -1.0 : releaseAt - n;
    if (delay > 0.0)
    {
        const int skip = (int) std::ceil (delay - 1.0e-9);
        if (skip >= n) { delay -= n; if (fadeFrom < n) active = false; return; }
        i = skip;
        t = ((double) skip - delay) * dt;     // sub-sample: the first sample is a fraction past the onset
        delay = 0.0;
    }

    const float g = gain;
    for (; i < n; ++i)
    {
        if (t > end) { active = false; return; }
        float v = sample != nullptr ? sampled (t) : synth (t);
        if (! active) return;
        if (i >= fadeFrom && fadeStep == 0.0f)
            fadeStep = (float) (1.0 / (0.004 * sr));
        if (fadeStep > 0.0f)
        {
            fade -= fadeStep;
            if (fade <= 0.0f) { active = false; return; }
            v *= fade;
        }
        out[i] += v * g;
        t += dt;
    }
}

} // namespace thoreau
