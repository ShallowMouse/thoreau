#include "AcousticDrum.h"

namespace thoreau
{

namespace
{
    constexpr double twoPi = juce::MathConstants<double>::twoPi;

    double besselJ (int m, double x)
    {
        double term = 1.0;
        for (int k = 1; k <= m; ++k) term *= (x * 0.5) / k;
        double sum = 0.0;
        for (int k = 0; k < 60; ++k)
        {
            sum += term;
            term *= -(x * x * 0.25) / ((k + 1.0) * (k + 1.0 + m));
            if (std::abs (term) < 1e-14) break;
        }
        return sum;
    }

    // Circular membrane: (angular order m, Bessel zero j_mn), in order of frequency.
    struct MembraneMode { int m; double j; };
    const MembraneMode membraneModes[] = {
        { 0, 2.4048 }, { 1, 3.8317 }, { 2, 5.1356 }, { 0, 5.5201 }, { 3, 6.3802 }, { 1, 7.0156 }, { 4, 7.5883 },
        { 2, 8.4172 }, { 0, 8.6537 }, { 5, 8.7715 }, { 3, 9.7610 }, { 6, 9.9361 }, { 1, 10.1735 }, { 4, 11.0647 },
        { 7, 11.0864 }, { 2, 11.6198 }, { 0, 11.7915 }, { 5, 12.2251 }, { 8, 12.2251 }, { 3, 13.0152 } };
    constexpr int numMembraneModes = (int) (sizeof (membraneModes) / sizeof (membraneModes[0]));

    struct Rng
    {
        uint32_t s;
        explicit Rng (uint32_t seed) : s ((seed * 2654435761u) ^ 0x9e3779b9u) { if (s == 0) s = 1; next(); }
        float next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float) (s & 0xffffff) / 16777216.0f; }
        float jitter (float amount) { return 1.0f + (next() * 2.0f - 1.0f) * amount; }
    };

    inline float clamp01 (float v) noexcept { return juce::jlimit (0.0f, 1.0f, v); }
}

//==============================================================================
void AcousticDrum::Biquad::bandpass (double fs, double hz, double q) noexcept
{
    hz = juce::jlimit (20.0, fs * 0.45, hz);
    const double w = twoPi * hz / fs, alpha = std::sin (w) / (2.0 * q), a0 = 1.0 + alpha;
    b0 = (float) (alpha / a0); b1 = 0.0f; b2 = (float) (-alpha / a0);
    a1 = (float) (-2.0 * std::cos (w) / a0); a2 = (float) ((1.0 - alpha) / a0);
    z1 = z2 = 0.0f;
}

void AcousticDrum::Biquad::highpass (double fs, double hz, double q) noexcept
{
    hz = juce::jlimit (20.0, fs * 0.45, hz);
    const double w = twoPi * hz / fs, alpha = std::sin (w) / (2.0 * q), c = std::cos (w), a0 = 1.0 + alpha;
    b0 = (float) ((1.0 + c) * 0.5 / a0); b1 = (float) (-(1.0 + c) / a0); b2 = b0;
    a1 = (float) (-2.0 * c / a0); a2 = (float) ((1.0 - alpha) / a0);
    z1 = z2 = 0.0f;
}

//==============================================================================
void AcousticDrum::prepare (double sampleRate) noexcept
{
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    active = false;
}

void AcousticDrum::addMode (double hz, double t60, float a, float b, bool high) noexcept
{
    if (numModes >= maxModes || hz >= sr * 0.45 || hz <= 10.0) return;
    if (! high && numHigh > 0) return;          // low modes come first
    const int i = numModes++;
    if (high) ++numHigh;
    const double w = twoPi * hz / sr;
    const double r = std::exp (-6.9078 / (juce::jmax (0.002, t60) * sr));
    w0[i] = (float) w;
    r0[i] = (float) r;
    pr[i] = (float) (r * std::cos (w));
    pi[i] = (float) (r * std::sin (w));
    bA[i] = a;
    bB[i] = b;
    re[i] = im[i] = 0.0f;
}

void AcousticDrum::setupMembrane (float f0, float t60, float dmp, float brt, float radiusA, float radiusB, int count, float stretch)
{
    for (int i = 0; i < juce::jmin (count, numMembraneModes); ++i)
    {
        const auto& mm = membraneModes[i];
        const double ratio = mm.j / membraneModes[0].j;
        const double f = f0 * std::pow (ratio, (double) stretch);
        const double radiate = mm.m == 0 ? 1.0 : mm.m == 1 ? 0.55 : 0.4;
        const double tilt = std::pow (ratio, -(1.0 - brt) * 1.1);
        const double wa = std::abs (besselJ (mm.m, mm.j * radiusA)) * radiate * tilt;
        const double wb = std::abs (besselJ (mm.m, mm.j * radiusB)) * radiate * tilt;
        const double t = t60 * std::pow (ratio, -(0.5 + dmp * 1.8));
        addMode (f, t, (float) wa, (float) wb);
    }
    numGlide = numModes;
}

void AcousticDrum::setupMetal (float fLow, float fHigh, int count, float slope, float ring1k, float decayExp, float swellHz, uint32_t layoutSeed)
{
    Rng L (layoutSeed);
    for (int i = 0; i < count; ++i)
    {
        const double u = (i + L.next() * 0.95) / count;
        const double f = fLow * std::pow ((double) fHigh / fLow, u);
        // the plate's mode shapes have random signs where the stick lands: partials don't all start in phase
        const double a = std::pow (f / fLow, -(double) slope) * (0.45 + L.next() * 0.9) * (L.next() < 0.5f ? -1.0 : 1.0);
        const double t = ring1k * std::pow (f / 1000.0, -(double) decayExp) * (0.7 + 0.6 * L.next());
        const bool high = f > swellHz;
        addMode (f, t, high ? 0.0f : (float) a, high ? (float) a : 0.0f, high);
    }
}

void AcousticDrum::addPulse (double at, double widthSeconds, float a, float b) noexcept
{
    if (numPulses >= (int) pulses.size()) return;
    pulses[(size_t) numPulses++] = { at, juce::jmax (2.0, widthSeconds * sr), a, b };
}

void AcousticDrum::addBurst (double at, float amp, double decaySeconds, double attackSeconds, int filter) noexcept
{
    if (numBursts >= (int) bursts.size() || amp <= 0.0f) return;
    Burst b;
    b.at = at;
    b.amp = amp;
    b.kd = (float) std::exp (-1.0 / (juce::jmax (0.0003, decaySeconds) * sr));
    b.ka = attackSeconds > 0.0 ? (float) (1.0 - std::exp (-1.0 / (attackSeconds * sr))) : 1.0f;
    b.filter = filter;
    bursts[(size_t) numBursts++] = b;
}

//==============================================================================
void AcousticDrum::start (const DrumModel& m, const VoiceParams& p, float velocity, double onset, uint32_t seed) noexcept
{
    model = m;
    params = p;
    active = true;
    now = 0.0;
    lastHit = 0.0;
    numModes = numHigh = numGlide = 0;
    numPulses = numBursts = 0;
    glide = glideAmount = 0.0f;
    glideCountdown = 0;
    swell = swellFloor = 1.0f;
    swellCoef = 0.0f;
    trash = 0.0f;
    rough = roughLp1 = roughLp2 = 0.0f;
    roughLow = false;
    hat = false;
    hatFast = hatSlow = hatSz = 0.0f;
    wobbleDepth = 0.0f;
    roughCoef = (float) (1.0 - std::exp (-twoPi * 900.0 / sr));
    wireLevel = wireEnv = 0.0f;
    chokeGain = 1.0f;
    chokeCoef = 1.0f;
    chokeAt = -1.0;
    lp = hpState = 0.0f;
    quietChunks = 0;
    for (auto& g : filterGain) g = 0.0f;
    rng = (seed * 2246822519u) ^ 0x2545f491u;
    if (rng == 0) rng = 1;

    Rng H (seed ^ 0x51ed27u);
    const float tuneMul = (float) std::pow (2.0, p.tune / 12.0);
    const float ringMul = (float) std::pow (2.0, (p.decay - 0.5) * 3.0);
    pitch  = m.pitch * tuneMul * H.jitter (0.003f);
    ring   = m.ring * ringMul;
    bright = clamp01 (m.bright + (p.tone - 0.5f) * 0.8f);
    hard   = clamp01 (m.hard + (p.snap - 0.5f) * 0.8f);
    extra  = m.extra;
    const float dmp = clamp01 (m.damp - (p.decay - 0.5f) * 0.4f);

    float lpHz = 20000.0f;
    outGain = 1.0f;

    switch (m.kind)
    {
        case Kind::kick:
            setupMembrane (pitch, ring, dmp, bright, 0.12f, 0.35f, 10, 0.9f);
            // resonant (front) head: slightly lower, rings a little longer
            if (extra > 0.0f)
                for (int i = 0; i < 4; ++i)
                {
                    const double ratio = membraneModes[i].j / membraneModes[0].j;
                    addMode (pitch * 0.985 * std::pow (ratio, 0.9), ring * 1.25 * std::pow (ratio, -(0.5 + dmp * 1.8)),
                             (float) (0.3 * std::abs (besselJ (membraneModes[i].m, membraneModes[i].j * 0.05))), 0.0f);
                }
            numGlide = numModes;
            addMode (pitch * 2.3, 0.05, 0.22f, 0.12f);                   // shell
            glideAmount = 0.35f + 0.25f * hard;
            glideCoef = (float) std::exp (-16.0 / (0.022 * sr));
            filters[fTick].bandpass (sr, 2800.0 + 2500.0 * bright, 0.8); filterGain[fTick] = 2.2f;
            lpHz = 3000.0f + bright * bright * 14000.0f;
            outGain = 0.85f;
            break;

        case Kind::snare:
        case Kind::rimshot:
        case Kind::sidestick:
        case Kind::brush:
        case Kind::sweep:
        {
            const bool side = m.kind == Kind::sidestick;
            setupMembrane (pitch, ring, dmp, bright, 0.3f, 0.62f, 16, 1.0f);
            addMode (pitch * 2.85, 0.06, 0.25f, 0.15f);                  // shell
            if (m.kind == Kind::rimshot)
            {
                static const float rim[] = { 930, 1520, 2380, 3350, 4610, 6200, 7900 };
                static const float rimT[] = { 0.10f, 0.08f, 0.065f, 0.05f, 0.04f, 0.03f, 0.025f };
                static const float rimA[] = { 1.0f, 0.8f, 0.7f, 0.55f, 0.45f, 0.35f, 0.25f };
                const float s = std::sqrt (pitch / 200.0f);
                for (int i = 0; i < 7; ++i) addMode (rim[i] * s * H.jitter (0.01f), rimT[i], 0.0f, rimA[i] * 0.55f);
            }
            if (side)
            {
                static const float wood[] = { 560, 1080, 1700, 2550, 3600, 4800 };
                static const float woodT[] = { 0.05f, 0.04f, 0.032f, 0.026f, 0.02f, 0.016f };
                static const float woodA[] = { 1.0f, 0.85f, 0.65f, 0.5f, 0.35f, 0.25f };
                const float s = std::sqrt (pitch / 200.0f);
                for (int i = 0; i < 6; ++i) addMode (wood[i] * s * H.jitter (0.01f), woodT[i], 0.0f, woodA[i] * 0.5f);
                addMode (230.0 * s, 0.07, 0.0f, 0.35f);
            }
            numGlide = juce::jmin (numGlide, numModes);
            glideAmount = 0.07f;
            glideCoef = (float) std::exp (-16.0 / (0.012 * sr));
            wireLevel = extra * (side ? 0.25f : 1.0f);
            wireRef = juce::jmax (0.05f, bA[0]);
            wireAtt = (float) (1.0 - std::exp (-1.0 / (0.0005 * sr)));
            wireRel = (float) (1.0 - std::exp (-1.0 / ((0.018 + 0.03 * ringMul) * sr)));
            filters[fWires].bandpass (sr, 3200.0 + 3500.0 * bright, 0.55); filterGain[fWires] = 2.6f;
            filters[fTick].bandpass (sr, 4500.0, 1.0); filterGain[fTick] = 2.0f;
            filters[fBody].bandpass (sr, 1800.0 + 2500.0 * bright, 0.45); filterGain[fBody] = 2.0f;   // brush bristles
            lpHz = 4500.0f + bright * bright * 15000.0f;
            outGain = m.kind == Kind::rimshot ? 0.55f : side ? 0.6f : m.kind == Kind::sweep ? 0.5f : 0.75f;
            break;
        }

        case Kind::tom:
            setupMembrane (pitch, ring, dmp, bright, 0.3f, 0.6f, 14, 0.92f);
            if (extra > 0.0f)                                            // resonant head, slightly higher: bloom and beat
                for (int i = 0; i < 5; ++i)
                {
                    const double ratio = membraneModes[i].j / membraneModes[0].j;
                    addMode (pitch * 1.045 * std::pow (ratio, 0.92), ring * 1.2 * std::pow (ratio, -(0.5 + dmp * 1.8)),
                             (float) (0.4 * extra * std::abs (besselJ (membraneModes[i].m, membraneModes[i].j * 0.2))), 0.0f);
                }
            numGlide = numModes;
            addMode (pitch * 2.4, 0.05, 0.18f, 0.1f);
            glideAmount = 0.12f + 0.08f * hard;
            glideCoef = (float) std::exp (-16.0 / (0.05 * sr));
            filters[fTick].bandpass (sr, 3500.0, 1.0); filterGain[fTick] = 2.0f;
            lpHz = 3500.0f + bright * bright * 15000.0f;
            outGain = 0.8f;
            break;

        case Kind::hatClosed:
        case Kind::hatOpen:
        case Kind::hatPedal:
        {
            hat = true;
            const float s = pitch / 100.0f;
            const bool closed = m.kind == Kind::hatClosed, pedal = m.kind == Kind::hatPedal;
            Rng V (m.seed * 7919u + 17u);           // this pair of hats' own colour: the same every hit

            // eight broad resonances across the top, two low "body" ones (the pedal chick lives there)
            double var = 0.0;
            const float tilt = (bright - 0.5f) * 1.4f;
            for (int k = 0; k < hatBands; ++k)
            {
                double f, q, g;
                if (k < 8)
                {
                    const double u = (k + 0.15 + 0.7 * V.next()) / 8.0;
                    f = s * 2800.0 * std::pow (13000.0 / 2800.0, u);
                    q = 1.8 + 2.6 * V.next() + (1.0f - bright) * 1.2;
                    g = std::pow (f / 5000.0, (double) tilt) * (0.6 + 0.8 * V.next());
                }
                else
                {
                    f = s * (k == 8 ? 650.0 + 350.0 * V.next() : 1700.0 + 600.0 * V.next());
                    q = k == 8 ? 1.3 : 1.6;
                    g = (k == 8 ? 0.07 : 0.14) * (1.2f - bright * 0.6f) * (pedal ? 5.0 : 1.0);
                }
                f = juce::jmin (f, sr * 0.42);
                hatBand[(size_t) k].bandpass (sr, f, q);
                hatGain[(size_t) k] = (float) g;
                // white noise in [-1, 1] (variance 1/3) through a 0 dB-peak band: noise bandwidth ~ (pi / 2) f / Q
                var += g * g * (1.0 / 3.0) * (juce::MathConstants<double>::halfPi * f / q) / (sr * 0.5);
            }
            hatNorm = (float) (1.0 / std::sqrt (juce::jmax (1.0e-9, var)));

            const double fc = juce::jmin (sr * 0.42, s * (6500.0 + 2500.0 * bright));
            hatHp1.highpass (sr, fc, 0.7);
            hatHp2.highpass (sr, fc, 0.7);
            szNorm = (float) (1.0 / std::sqrt ((1.0 / 3.0) * juce::jmax (0.05, 1.0 - fc / (sr * 0.5))));

            // the stick hitting the top plate: a click, lower when the stick is softer
            filters[fTick].bandpass (sr, 3200.0 + 2600.0 * hard, 0.9); filterGain[fTick] = 2.0f;
            filters[fBody].bandpass (sr, 900.0 * s, 1.4); filterGain[fBody] = 2.0f;

            const double fastT = pedal ? 0.008 : closed ? 0.0035 + 0.002 * (1.0f - hard) : 0.006;
            const double slowT = pedal ? 0.025 : juce::jmax (0.02, (double) ring) / 6.9;
            hatFastK = (float) std::exp (-1.0 / (fastT * sr));
            hatSlowK = (float) std::exp (-1.0 / (slowT * sr));
            hatSzK   = (float) std::exp (-1.0 / (slowT * (closed ? 0.8 : 0.9) * sr));
            hatSlowShare = pedal ? 0.25f : closed ? 0.45f : 0.7f;
            hatSzMix = pedal ? 0.15f : 0.3f + 0.45f * bright;
            if (m.kind == Kind::hatOpen)
            {
                wobbleDepth = 0.12f;                                    // the loose plates beat against each other
                wobbleInc = (float) (twoPi * (5.5 + 2.5 * V.next()) / sr);
                wobblePhase = (float) (twoPi * V.next());
            }
            lpHz = 7500.0f + 6500.0f * bright;          // recorded hats roll off up top
            outGain = 1.0f;
            break;
        }

        case Kind::crash:
        case Kind::splash:
        case Kind::china:
        {
            const float s = pitch / 100.0f;
            const bool sp = m.kind == Kind::splash;
            setupMetal ((sp ? 420.0f : 220.0f) * s, 16000.0f, sp ? 110 : 160, -0.35f + (1.0f - bright) * 0.45f, ring,
                        0.35f, 2600.0f, m.seed);
            rough = 0.75f;
            swellFloor = sp ? 0.45f : 0.3f;
            swellCoef = (float) (1.0 - std::exp (-1.0 / ((sp ? 0.025 : 0.06) * sr)));
            filters[fTick].bandpass (sr, 6000.0, 0.9); filterGain[fTick] = 2.0f;
            filters[fSizzle].highpass (sr, 4200.0, 0.7); filterGain[fSizzle] = 1.0f;
            filters[fBody].bandpass (sr, 3000.0, 0.6); filterGain[fBody] = 1.5f;
            if (m.kind == Kind::china) trash = extra;
            outGain = sp ? 0.16f : 0.13f;
            break;
        }

        case Kind::ride:
        {
            const float s = pitch / 100.0f;
            setupMetal (300.0f * s, 14000.0f, 150, -0.1f + (1.0f - bright) * 0.4f, ring, 0.3f, 3200.0f, m.seed);
            rough = 0.7f;
            swellFloor = 0.55f;
            swellCoef = (float) (1.0 - std::exp (-1.0 / (0.15 * sr)));
            filters[fTick].bandpass (sr, 4200.0 + 2000.0 * bright, 1.8); filterGain[fTick] = 3.0f;   // the ping
            filters[fSizzle].highpass (sr, 5000.0, 0.7); filterGain[fSizzle] = 0.8f;
            outGain = 0.16f;
            break;
        }

        case Kind::rideBell:
        {
            static const float ratio[] = { 1.0f, 1.47f, 2.09f, 2.74f, 3.42f, 4.06f, 4.83f, 5.61f };
            static const float bt[] = { 1.0f, 0.88f, 0.75f, 0.65f, 0.55f, 0.48f, 0.4f, 0.35f };
            static const float ba[] = { 1.0f, 0.8f, 0.7f, 0.55f, 0.45f, 0.35f, 0.3f, 0.25f };
            for (int i = 0; i < 8; ++i) addMode (pitch * ratio[i] * H.jitter (0.002f), ring * bt[i], ba[i], ba[i]);
            setupMetal (pitch * 0.45f, 13000.0f, 90, 0.5f, ring * 0.6f, 0.3f, 3500.0f, m.seed);
            for (int i = 8; i < numModes; ++i) { bA[i] *= 0.35f; bB[i] *= 0.35f; }
            filters[fTick].bandpass (sr, 5000.0, 1.5); filterGain[fTick] = 2.0f;
            outGain = 0.2f;
            break;
        }

        case Kind::gong:
        {
            const float s = pitch / 70.0f;
            setupMetal (70.0f * s, 7500.0f, 100, 0.5f, ring, 0.45f, 700.0f, m.seed);
            swellFloor = 0.06f;
            swellCoef = (float) (1.0 - std::exp (-1.0 / (0.5 * sr)));
            outGain = 0.1f;
            lpHz = 9000.0f;
            break;
        }

        case Kind::tambourine:
        {
            addMode (310.0 * pitch / 100.0, 0.07, 0.25f, 0.0f);       // head / frame
            addMode (520.0 * pitch / 100.0, 0.05, 0.15f, 0.0f);
            setupMetal (2800.0f * pitch / 100.0f, 14000.0f, 40, 0.1f, ring, 0.2f, 1.0f, m.seed);   // jingles (all "high": stream B)
            filters[fSizzle].highpass (sr, 6000.0, 0.7); filterGain[fSizzle] = 1.0f;
            outGain = 0.3f;
            break;
        }

        case Kind::claps:
        {
            const int people = 3 + juce::roundToInt (extra * 3.0f);
            for (int i = 0; i < juce::jmin (people, 4); ++i)
            {
                filters[(size_t) (fClap + i)].bandpass (sr, (850.0 + 900.0 * H.next()) * pitch / 100.0, 1.3 + 0.4 * H.next());
                filterGain[(size_t) (fClap + i)] = 3.2f;
            }
            filters[fBody].bandpass (sr, 1300.0 * pitch / 100.0, 0.7); filterGain[fBody] = 1.6f;      // the room around them
            outGain = 0.55f;
            lpHz = 6000.0f + bright * 9000.0f;
            break;
        }

        case Kind::syndrum:
            addMode (pitch, ring, 1.0f, 0.0f);
            addMode (pitch * 1.5, ring * 0.5, 0.18f, 0.0f);
            numGlide = numModes;
            glideAmount = 1.5f + 2.5f * extra;
            glideCoef = (float) std::exp (-16.0 / (0.13 * sr));
            filters[fTick].highpass (sr, 2500.0, 0.7); filterGain[fTick] = 1.0f;
            outGain = 0.55f;
            lpHz = 2500.0f + bright * 12000.0f;
            break;

        case Kind::cowbell:
        {
            static const float ratio[] = { 1.0f, 1.505f, 2.37f, 3.36f, 4.64f, 6.1f };
            static const float bt[] = { 1.0f, 0.85f, 0.6f, 0.45f, 0.3f, 0.22f };
            static const float ba[] = { 1.0f, 0.9f, 0.5f, 0.35f, 0.2f, 0.15f };
            for (int i = 0; i < 6; ++i) addMode (pitch * ratio[i], ring * bt[i], ba[i], ba[i]);
            filters[fTick].bandpass (sr, 3000.0, 1.0); filterGain[fTick] = 1.5f;
            outGain = 0.35f;
            break;
        }

        case Kind::analog:
            active = false;
            return;
    }

    // Level calibration per kind of drum (measured at full velocity, through the close mic):
    // drums peak around 0 dBFS, hats and cymbals sit where they would on a real kit.
    {
        float trimDb = 0.0f;
        switch (m.kind)
        {
            case Kind::kick:       trimDb = -3.0f;  break;
            case Kind::snare:      trimDb = -1.0f;  break;
            case Kind::rimshot:    trimDb = -8.0f;  break;
            case Kind::sidestick:  trimDb = -6.0f;  break;
            case Kind::brush:      trimDb = 0.0f;   break;
            case Kind::sweep:      trimDb = 3.0f;   break;
            case Kind::tom:        trimDb = -2.0f;  break;
            case Kind::hatClosed:  trimDb = -2.0f;   break;
            case Kind::hatOpen:    trimDb = -6.0f;  break;
            case Kind::hatPedal:   trimDb = -3.0f;   break;
            case Kind::crash:      trimDb = -17.0f; break;
            case Kind::splash:     trimDb = -17.0f; break;
            case Kind::ride:       trimDb = -18.5f; break;
            case Kind::rideBell:   trimDb = -10.0f;  break;
            case Kind::china:      trimDb = -13.5f;  break;
            case Kind::gong:       trimDb = 0.0f;   break;
            case Kind::tambourine: trimDb = -16.0f; break;
            case Kind::claps:      trimDb = -5.0f;  break;
            case Kind::syndrum:    trimDb = 2.0f;   break;
            case Kind::cowbell:    trimDb = -8.0f;  break;
            case Kind::analog:     break;
        }
        outGain *= std::pow (10.0f, trimDb / 20.0f);
    }

    lpCoef = (float) (1.0 - std::exp (-twoPi * lpHz / sr));
    hpCoef = (float) (1.0 - std::exp (-twoPi * 25.0 / sr));
    swell = swellFloor;

    if (onset >= 0.0)
    {
        excite (velocity, onset, seed);
        return;
    }

    // Catching up: hit it `-onset` samples ago and let it ring up to now, silently.
    const double back = -onset;
    const int pre = (int) std::ceil (back);
    excite (velocity, (double) pre - back, seed);
    float scratch[256];
    for (int done = 0; done < pre && active;)
    {
        const int n = juce::jmin (256, pre - done);
        std::fill (scratch, scratch + n, 0.0f);
        renderChunk (scratch, n);
        done += n;
    }
}

//==============================================================================
void AcousticDrum::excite (float velocity, double onset, uint32_t seed) noexcept
{
    if (model.kind == Kind::analog) return;
    active = true;
    quietChunks = 0;
    Rng H (seed);
    const double at = now + juce::jmax (0.0, onset);
    const float vel = juce::jlimit (0.05f, 1.0f, velocity);
    const float amp = std::pow (vel, 1.4f) * H.jitter (0.06f);
    const float wmul = (1.0f + 1.1f * (1.0f - vel)) * H.jitter (0.1f);
    lastHit = at;
    glide = juce::jmax (glide, glideAmount * vel * vel * H.jitter (0.1f));
    glideCountdown = 0;

    auto strike = [&] (double width, float edge)   // edge: how much of the hit is away from the usual spot
    {
        edge = clamp01 (edge * H.jitter (0.5f));
        addPulse (at, width * wmul, amp * (1.0f - edge), amp * edge);
    };

    switch (model.kind)
    {
        case Kind::kick:
            strike (0.0045 - 0.0033 * hard, 0.15f);
            addBurst (at, amp * extra * (0.25f + 0.75f * hard) * (0.6f + 0.4f * vel), 0.0022, 0.0, fTick);
            break;

        case Kind::snare:
            strike (0.0017 - 0.0012 * hard, 0.25f + 0.25f * bright);
            addBurst (at, amp * 0.3f * hard, 0.0015, 0.0, fTick);
            break;

        case Kind::rimshot:
            addPulse (at, (0.0007 - 0.0004 * hard) * wmul, amp * 1.25f, amp * 1.1f);
            addBurst (at, amp * 0.5f, 0.002, 0.0, fTick);
            break;

        case Kind::sidestick:
            addPulse (at, 0.00035 * wmul, amp * 0.22f, amp * 1.2f);
            break;

        case Kind::brush:
            addPulse (at, 0.006 * wmul, amp * 0.45f, amp * 0.1f);
            addBurst (at, amp * 0.9f, 0.05 + 0.05 * ring, 0.004, fBody);
            break;

        case Kind::sweep:          // the brush dragged round the head: swells, then fades
            addPulse (at, 0.02, amp * 0.12f, 0.0f);
            addBurst (at, amp * 0.55f, 0.22 + 0.5 * ring, 0.09 + 0.06 * (1.0f - hard), fBody);
            break;

        case Kind::tom:
            strike (0.0022 - 0.0015 * hard, 0.2f + 0.2f * bright);
            addBurst (at, amp * 0.22f * hard, 0.0018, 0.0, fTick);
            break;

        case Kind::hatClosed:
        case Kind::hatOpen:
        case Kind::hatPedal:
        {
            // a hit adds to whatever's still sounding (a re-hit on a ringing open hat swells it)
            const bool pedal = model.kind == Kind::hatPedal;
            hatFast += amp * (1.0f - hatSlowShare);
            hatSlow += amp * hatSlowShare;
            hatSz   += amp * hatSzMix * (0.4f + 0.6f * vel);             // harder = more sizzle
            addBurst (at, amp * (pedal ? 0.15f : 0.25f + 0.35f * hard) * vel, 0.0009 + 0.0006 * (1.0f - hard), 0.0, fTick);
            if (pedal) addBurst (at, amp * 0.8f, 0.007, 0.0008, fBody);
            break;
        }

        case Kind::crash:
        case Kind::splash:
        case Kind::china:
            addPulse (at, (0.00028 - 0.00016 * hard) * wmul, amp, amp);
            addBurst (at, amp * 0.25f, 0.0015, 0.0, fTick);
            addBurst (at, amp * (0.35f + 0.35f * bright + 0.5f * trash), ring * 0.25, 0.004, fSizzle);
            if (trash > 0.0f) addBurst (at, amp * trash * 0.8f, ring * 0.12, 0.002, fBody);
            break;

        case Kind::ride:
            addPulse (at, (0.0002 - 0.0001 * hard) * wmul, amp, amp);
            addBurst (at, amp * (0.3f + 0.4f * hard), 0.007, 0.0, fTick);
            addBurst (at, amp * (0.25f + 0.3f * extra), ring * (0.12 + 0.1 * extra), 0.003, fSizzle);   // extra: rivets
            break;

        case Kind::rideBell:
            addPulse (at, 0.0002 * wmul, amp, amp);
            addBurst (at, amp * 0.25f, 0.003, 0.0, fTick);
            break;

        case Kind::gong:
            addPulse (at, 0.009 * wmul, amp, 0.0f);                      // the mallet: low partials
            addPulse (at + 0.002 * sr, 0.0004, 0.0f, amp * 0.5f);         // energy into the high ones (they bloom)
            break;

        case Kind::tambourine:
        {
            double t = at;
            float a = amp;
            const int shakes = 3 + juce::roundToInt (extra * 4.0f);
            for (int i = 0; i < shakes; ++i)
            {
                addPulse (t, 0.00015, i == 0 ? a : 0.0f, a);
                t += (0.004 + 0.005 * H.next()) * sr;
                a *= 0.55f + 0.15f * H.next();
            }
            addBurst (at, amp * 0.3f, 0.05 + 0.1 * ring, 0.001, fSizzle);
            break;
        }

        case Kind::claps:
        {
            const int people = 3 + juce::roundToInt (extra * 3.0f);
            const double spread = (0.006 + 0.012 * extra) * sr;
            for (int i = 0; i < people; ++i)
            {
                const double t = at + (i == 0 ? 0.0 : spread * H.next());
                const int f = fClap + i % 4;
                const float a = amp * (0.6f + 0.4f * H.next());
                addBurst (t, a, 0.007 + 0.004 * H.next(), 0.0004, f);
                addBurst (t + 0.0011 * sr, a * 0.45f, 0.006, 0.0003, f);          // the flesh slap after the clap
            }
            addBurst (at + 0.006 * sr, amp * 0.3f, 0.045 * (0.5 + ring * 4.0), 0.004, fBody);
            break;
        }

        case Kind::syndrum:
            addPulse (at, 0.0006 * wmul, amp, 0.0f);
            addBurst (at, amp * 0.15f, 0.004, 0.0, fTick);
            break;

        case Kind::cowbell:
            addPulse (at, 0.0003 * wmul, amp, amp);
            addBurst (at, amp * 0.2f, 0.002, 0.0, fTick);
            break;

        case Kind::analog:
            break;
    }
}

void AcousticDrum::choke (double at, double seconds) noexcept
{
    if (! active || (chokeAt >= 0.0 && chokeAt <= now + at)) return;
    chokeAt = now + juce::jmax (0.0, at);
    chokeCoef = (float) std::exp (-6.9078 / (juce::jmax (0.002, seconds) * sr));
}

//==============================================================================
void AcousticDrum::updateGlide() noexcept
{
    glide *= glideCoef;
    if (glide < 1.0e-5f) glide = 0.0f;
    for (int i = 0; i < numGlide; ++i)
    {
        const float w = w0[i] * (1.0f + glide);
        pr[i] = r0[i] * std::cos (w);
        pi[i] = r0[i] * std::sin (w);
    }
}

bool AcousticDrum::render (float* out, int n) noexcept
{
    if (! active) return false;
    for (int done = 0; done < n && active;)
    {
        const int m = juce::jmin (256, n - done);
        renderChunk (out + done, m);
        done += m;
    }
    return active;
}

void AcousticDrum::renderChunk (float* out, int n) noexcept
{
    const int nLow = numModes - numHigh;
    float chunkPeak = 0.0f;

    for (int k = 0; k < n; ++k, now += 1.0)
    {
        // --- the force of the stick
        float xa = 0.0f, xb = 0.0f;
        for (int i = 0; i < numPulses;)
        {
            auto& p = pulses[(size_t) i];
            const double tau = now - p.at;
            if (tau >= p.width)
            {
                pulses[(size_t) i] = pulses[(size_t) --numPulses];
                continue;
            }
            if (tau >= 0.0)
            {
                const float f = (float) ((1.0 - std::cos (twoPi * tau / p.width)) / p.width);
                xa += p.a * f;
                xb += p.b * f;
            }
            ++i;
        }

        // --- the modes ring
        if (numGlide > 0 && glide > 0.0f && --glideCountdown <= 0)
        {
            updateGlide();
            glideCountdown = 16;
        }

        float yl = 0.0f, yh = 0.0f;
        if (xa != 0.0f || xb != 0.0f)
        {
            for (int i = 0; i < numModes; ++i)
            {
                const float nr = pr[i] * re[i] - pi[i] * im[i] + bA[i] * xa + bB[i] * xb;
                const float ni = pr[i] * im[i] + pi[i] * re[i];
                re[i] = nr; im[i] = ni;
                if (i < nLow) yl += ni; else yh += ni;
            }
        }
        else
        {
            for (int i = 0; i < nLow; ++i)
            {
                const float nr = pr[i] * re[i] - pi[i] * im[i];
                const float ni = pr[i] * im[i] + pi[i] * re[i];
                re[i] = nr; im[i] = ni;
                yl += ni;
            }
            for (int i = nLow; i < numModes; ++i)
            {
                const float nr = pr[i] * re[i] - pi[i] * im[i];
                const float ni = pr[i] * im[i] + pi[i] * re[i];
                re[i] = nr; im[i] = ni;
                yh += ni;
            }
        }
        if (swellCoef > 0.0f) swell += (1.0f - swell) * swellCoef;
        if (rough > 0.0f)
        {
            roughLp1 += (noise() - roughLp1) * roughCoef;
            roughLp2 += (roughLp1 - roughLp2) * roughCoef;
            const float w = 1.0f + rough * 10.0f * roughLp2;      // roughLp2 is ~0.1 rms: level unchanged on average
            if (roughLow) yl *= w; else yh *= w;
        }
        float y = yl + swell * yh;

        // --- hi-hat plates
        if (hat && (hatFast > 1.0e-7f || hatSlow > 1.0e-7f || hatSz > 1.0e-7f))
        {
            const float n = noise();
            float b = 0.0f;
            for (int i = 0; i < hatBands; ++i)
                b += hatBand[(size_t) i].process (n) * hatGain[(size_t) i];
            float slow = hatSlow;
            if (wobbleDepth > 0.0f)
            {
                wobblePhase += wobbleInc;
                if (wobblePhase > 6.2831853f) wobblePhase -= 6.2831853f;
                slow *= 1.0f + wobbleDepth * std::sin (wobblePhase);
            }
            const float sz = hatHp2.process (hatHp1.process (noise()));
            y += (hatFast + slow) * b * hatNorm * 0.35f + hatSz * sz * szNorm * 0.35f;
            hatFast *= hatFastK;
            hatSlow *= hatSlowK;
            hatSz   *= hatSzK;
        }

        // --- noise: stick tick, sizzle, brush, claps
        float in[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
        for (int i = 0; i < numBursts;)
        {
            auto& b = bursts[(size_t) i];
            if (now < b.at) { ++i; continue; }
            if (! b.started) { b.started = true; b.d = b.amp; b.att = b.ka >= 1.0f ? 1.0f : 0.0f; }
            b.att += (1.0f - b.att) * b.ka;
            in[b.filter] += b.d * b.att;
            b.d *= b.kd;
            if (b.d < 1.0e-6f) { bursts[(size_t) i] = bursts[(size_t) --numBursts]; continue; }
            ++i;
        }

        // --- snare wires, driven by the head
        if (wireLevel > 0.0f)
        {
            const float a = std::sqrt (re[0] * re[0] + im[0] * im[0]) / wireRef;
            const float target = juce::jmax (0.0f, a - 0.012f);
            wireEnv += (target - wireEnv) * (target > wireEnv ? wireAtt : wireRel);
            const float rattle = a > 1.0e-6f ? 0.6f + 0.4f * std::abs (im[0]) / (a * wireRef) : 1.0f;
            in[fWires] += wireEnv * rattle * wireLevel * 0.9f;
        }

        for (int f = 0; f < 8; ++f)
            if (filterGain[(size_t) f] != 0.0f)
                y += filters[(size_t) f].process (noise() * in[f]) * filterGain[(size_t) f];

        if (trash > 0.0f)
            y = y + trash * (std::tanh (y * 3.0f) * 0.33f - y * 0.5f) * 1.5f;

        // --- through the mic
        lp += (y - lp) * lpCoef;
        hpState += (lp - hpState) * hpCoef;
        float o = (lp - hpState) * outGain;

        if (chokeAt >= 0.0 && now >= chokeAt) chokeGain *= chokeCoef;
        o *= chokeGain;

        out[k] += o;
        chunkPeak = juce::jmax (chunkPeak, std::abs (o));
    }

    // --- has it died away?
    if (chokeGain < 1.0e-4f
        || (numPulses == 0 && numBursts == 0 && now - lastHit > 0.03 * sr && chunkPeak < 1.5e-5f && wireEnv < 1.0e-4f
            && hatSlow < 1.0e-5f && hatSz < 1.0e-5f
            && ++quietChunks >= 3))
        active = false;
    else if (chunkPeak >= 1.5e-5f)
        quietChunks = 0;
}

} // namespace thoreau
