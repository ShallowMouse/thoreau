#include "KitBus.h"

namespace thoreau
{

namespace
{
    constexpr double pi = juce::MathConstants<double>::pi;
    inline float dbToGain (float db) noexcept { return db <= -59.9f ? 0.0f : std::pow (10.0f, db / 20.0f); }
}

//==============================================================================
// Filters and dynamics

void KitBus::Biquad::set (double b0_, double b1_, double b2_, double a0, double a1_, double a2_) noexcept
{
    b0 = (float) (b0_ / a0); b1 = (float) (b1_ / a0); b2 = (float) (b2_ / a0);
    a1 = (float) (a1_ / a0); a2 = (float) (a2_ / a0);
}

void KitBus::Biquad::highpass (double fs, double hz) noexcept
{
    const double w = 2.0 * pi * juce::jlimit (10.0, fs * 0.45, hz) / fs, c = std::cos (w), al = std::sin (w) / (2.0 * 0.7071);
    set ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
}

void KitBus::Biquad::lowpass (double fs, double hz) noexcept
{
    const double w = 2.0 * pi * juce::jlimit (10.0, fs * 0.45, hz) / fs, c = std::cos (w), al = std::sin (w) / (2.0 * 0.7071);
    set ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
}

void KitBus::Biquad::lowShelf (double fs, double hz, double db) noexcept
{
    const double A = std::pow (10.0, db / 40.0), w = 2.0 * pi * hz / fs, c = std::cos (w), al = std::sin (w) / 2.0 * std::sqrt (2.0);
    const double sa = 2.0 * std::sqrt (A) * al;
    set (A * ((A + 1) - (A - 1) * c + sa), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sa),
         (A + 1) + (A - 1) * c + sa, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sa);
}

void KitBus::Biquad::highShelf (double fs, double hz, double db) noexcept
{
    const double A = std::pow (10.0, db / 40.0), w = 2.0 * pi * hz / fs, c = std::cos (w), al = std::sin (w) / 2.0 * std::sqrt (2.0);
    const double sa = 2.0 * std::sqrt (A) * al;
    set (A * ((A + 1) + (A - 1) * c + sa), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sa),
         (A + 1) - (A - 1) * c + sa, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sa);
}

void KitBus::Compressor::set (double fs, double attackMs, double releaseMs) noexcept
{
    att = (float) (1.0 - std::exp (-1.0 / (juce::jmax (0.05, attackMs) * 0.001 * fs)));
    rel = (float) (1.0 - std::exp (-1.0 / (juce::jmax (1.0, releaseMs) * 0.001 * fs)));
}

float KitBus::Compressor::gain (float x, float thr, float ratio, float knee) noexcept
{
    const float db = juce::Decibels::gainToDecibels (x, -100.0f);
    const float over = db - thr;
    float red = 0.0f;                                    // dB of gain reduction wanted
    if (over > knee * 0.5f)        red = over * (1.0f - 1.0f / ratio);
    else if (over > -knee * 0.5f)  { const float o = over + knee * 0.5f; red = (1.0f - 1.0f / ratio) * o * o / (2.0f * knee); }
    env += (red - env) * (red > env ? att : rel);
    return juce::Decibels::decibelsToGain (-env);
}

//==============================================================================
// The room

juce::AudioBuffer<float> KitBus::makeRoom (const ReverbDef& r, double fs, bool monoRoom)
{
    const double pre = juce::jlimit (0.0, 0.2, r.predelayMs / 1000.0);
    const double rt = juce::jlimit (0.1, 8.0, (double) r.rt60);
    const int len = (int) juce::jmin (fs * 6.0, fs * (pre + rt * 1.15 + 0.05));
    juce::AudioBuffer<float> ir (2, juce::jmax (64, len));
    ir.clear();

    const double size = juce::jlimit (0.2, 4.0, (double) r.size);
    const double tL = rt * 1.15, tM = rt, tH = juce::jmax (0.06, rt * (1.0 - 0.75 * r.damp));
    const double ramp = (0.004 + 0.02 * size) * fs;                  // the diffuse field builds up
    const float kLowFilt = (float) (1.0 - std::exp (-2.0 * pi * 400.0 / fs));
    const float kHighFilt = (float) (1.0 - std::exp (-2.0 * pi * 3500.0 / fs));
    const int pd = (int) (pre * fs);

    for (int ch = 0; ch < 2; ++ch)
    {
        juce::Random rnd (0x7e57 + ch * 7919 + (int) (r.size * 1000) + (int) (r.rt60 * 100));
        float* d = ir.getWritePointer (ch);

        // early reflections: walls, floor, ceiling
        const int nEarly = 8 + (int) (16 * size);
        const double span = (0.008 + 0.035 * size) * fs;
        for (int i = 0; i < nEarly; ++i)
        {
            const double u = rnd.nextDouble();
            const int at = pd + (int) ((0.0015 * fs) + u * u * span);
            if (at >= len) continue;
            d[at] += (float) (r.early * 0.55 * (rnd.nextBool() ? 1.0 : -1.0) * (1.0 - 0.6 * u));
        }

        // the diffuse decay, darker as it goes (highs die first)
        float lo = 0, hiLp = 0;
        const double eL = std::exp (-6.9078 / (tL * fs)), eM = std::exp (-6.9078 / (tM * fs)), eH = std::exp (-6.9078 / (tH * fs));
        double gL = 1, gM = 1, gH = 1;
        for (int i = pd; i < len; ++i)
        {
            const float n = rnd.nextFloat() + rnd.nextFloat() - 1.0f;
            lo += (n - lo) * kLowFilt;
            hiLp += (n - hiLp) * kHighFilt;
            const float high = n - hiLp, mid = hiLp - lo;
            const double build = juce::jmin (1.0, (i - pd) / ramp);
            d[i] += (float) ((lo * gL * 1.6 + mid * gM + high * gH * 1.2) * build * 0.3);
            gL *= eL; gM *= eM; gH *= eH;
        }
    }

    if (monoRoom)
        ir.copyFrom (1, 0, ir, 0, 0, ir.getNumSamples());

    // unit energy per channel: the room comes back about as loud as the drums went in
    for (int ch = 0; ch < 2; ++ch)
    {
        double e = 0;
        const float* d = ir.getReadPointer (ch);
        for (int i = 0; i < ir.getNumSamples(); ++i) e += (double) d[i] * d[i];
        if (e > 0) ir.applyGain (ch, 0, ir.getNumSamples(), (float) (1.0 / std::sqrt (e)));
    }
    return ir;
}

//==============================================================================
KitBus::KitBus() {}

void KitBus::prepare (double sampleRate, int maxBlockSize)
{
    sr = sampleRate > 0 ? sampleRate : 48000.0;
    maxBlock = juce::jmax (64, maxBlockSize);
    for (auto* b : { &ohBuf, &roomBuf, &dryBuf })
        b->setSize (2, maxBlock);
    sendBuf.setSize (1, maxBlock);
    fxBuf.setSize (1, maxBlock);
    for (auto& v : ohDelay) v.assign (256, 0.0f);
    ohWrite = 0;
    ohDelaySamples = juce::jlimit (1, 200, (int) std::round (0.0012 * sr));     // overheads ~40 cm further away
    echoLen = (int) (sr * 2.1);
    for (auto& v : echo) v.assign ((size_t) echoLen, 0.0f);
    echoWrite = 0;
    gate = 0; gateKeyPeak = 0; gateHold = 0;
    gateAtt = (float) (1.0 - std::exp (-1.0 / (0.0003 * sr)));
    gateRel = (float) (1.0 - std::exp (-1.0 / (0.012 * sr)));
    keyDecay = (float) std::exp (-1.0 / (1.0 * sr));
    tapeLp = { 0, 0 };
    lastTape = -1.0f;
    busComp.env = roomComp.env = 0;

    convolution.reset();
    convolution.prepare ({ sr, (juce::uint32) maxBlock, 2 });
    configuredKit = -1;
    setKit (kitIndex.load());
}

void KitBus::setKit (int k)
{
    const auto& def = kitAt (k);
    bool m = true;
    for (auto& s : def.slots) m = m && std::abs (s.pan) < 0.01f;       // a mono kit gets a mono room
    convolution.loadImpulseResponse (makeRoom (def.reverb, sr, m), sr,
                                     juce::dsp::Convolution::Stereo::yes, juce::dsp::Convolution::Trim::no,
                                     juce::dsp::Convolution::Normalise::no);
    kitIndex = k;
}

void KitBus::configure (const KitDef& k) noexcept
{
    for (auto& f : ohHp)   f.highpass (sr, 110.0);
    for (auto& f : roomHp) f.highpass (sr, juce::jmax (20.0f, k.reverb.lowCut));
    useLowShelf  = std::abs (k.bus.lowShelfDb) > 0.01f;
    useHighShelf = std::abs (k.bus.highShelfDb) > 0.01f;
    useLowCut    = k.bus.lowCut > 25.0f;
    useHighCut   = k.bus.highCut < 19000.0f;
    for (auto& f : lowShelf)  f.lowShelf (sr, 100.0, k.bus.lowShelfDb);
    for (auto& f : highShelf) f.highShelf (sr, 8000.0, k.bus.highShelfDb);
    for (auto& f : lowCut)    f.highpass (sr, k.bus.lowCut);
    for (auto& f : highCut)   f.lowpass (sr, k.bus.highCut);
    for (auto& f : echoLp)    f.lowpass (sr, juce::jmax (500.0f, k.delay.lowpass));
    busComp.set (sr, k.compressor.attackMs, k.compressor.releaseMs);
    roomComp.set (sr, 1.0, 70.0);
    mono = true;
    for (auto& s : k.slots) mono = mono && std::abs (s.pan) < 0.01f;
}

//==============================================================================
void KitBus::process (const float* const* tracks, const std::array<Send, kTracks>& sends, const Knobs& knobs,
                      float* L, float* R, int n) noexcept
{
    n = juce::jmin (n, maxBlock);
    const int k = kitIndex.load (std::memory_order_relaxed);
    const auto& kit = kitAt (k);
    if (k != configuredKit)
    {
        configure (kit);
        configuredKit = k;
    }

    const float closeG = dbToGain (knobs.closeDb), ohG = dbToGain (knobs.ohDb), roomG = dbToGain (knobs.roomDb);
    float* oL = ohBuf.getWritePointer (0);   float* oR = ohBuf.getWritePointer (1);
    float* rL = roomBuf.getWritePointer (0); float* rR = roomBuf.getWritePointer (1);
    float* snd = sendBuf.getWritePointer (0);
    float* fx = fxBuf.getWritePointer (0);
    juce::FloatVectorOperations::clear (L, n);
    juce::FloatVectorOperations::clear (R, n);
    for (float* b : { oL, oR, rL, rR, snd, fx }) juce::FloatVectorOperations::clear (b, n);

    const bool useOh = ohG > 0.0f, useRoom = roomG > 0.0f;

    //--------------------------------------------------------------------------
    // close mics, and the sends to the overheads, room and echo
    for (int t = 0; t < kTracks; ++t)
    {
        const auto& s = sends[(size_t) t];
        const float* x = tracks[t];
        const float a = (s.pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;          // constant power
        if (s.close > 0.0f)
        {
            juce::FloatVectorOperations::addWithMultiply (L, x, std::cos (a) * s.close * closeG, n);
            juce::FloatVectorOperations::addWithMultiply (R, x, std::sin (a) * s.close * closeG, n);
        }
        if (useOh && s.oh > 0.0f)
        {
            const float b = (juce::jlimit (-1.0f, 1.0f, s.pan * 1.3f) + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
            juce::FloatVectorOperations::addWithMultiply (oL, x, std::cos (b) * s.oh, n);
            juce::FloatVectorOperations::addWithMultiply (oR, x, std::sin (b) * s.oh, n);
        }
        if (useRoom && s.room > 0.0f) juce::FloatVectorOperations::addWithMultiply (snd, x, s.room, n);
        if (useRoom && s.fx > 0.0f)   juce::FloatVectorOperations::addWithMultiply (fx, x, s.fx, n);
    }

    //--------------------------------------------------------------------------
    // overheads: a little later than the close mics, low end cut
    if (useOh)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            float* o = ohBuf.getWritePointer (ch);
            auto& line = ohDelay[(size_t) ch];
            int w = ohWrite;
            for (int i = 0; i < n; ++i)
            {
                line[(size_t) w] = o[i];
                o[i] = ohHp[(size_t) ch].process (line[(size_t) ((w - ohDelaySamples + 256) & 255)]) * ohG;
                w = (w + 1) & 255;
            }
        }
        ohWrite = (ohWrite + n) & 255;
        juce::FloatVectorOperations::add (L, oL, n);
        juce::FloatVectorOperations::add (R, oR, n);
    }

    //--------------------------------------------------------------------------
    // room: convolved, then crushed and gated if the kit says so; plus the echo
    if (useRoom)
    {
        // gate key: the dry send, before the room smears it
        const float gateMs = kit.reverb.gateMs;
        const int holdSamples = (int) (gateMs * 0.001 * sr);
        float* gk = dryBuf.getWritePointer (0);
        for (int i = 0; i < n; ++i)
        {
            const float x = roomHp[0].process (snd[i]);
            gk[i] = x;
            rL[i] = x;
        }
        juce::FloatVectorOperations::copy (rR, rL, n);
        juce::dsp::AudioBlock<float> block (roomBuf.getArrayOfWritePointers(), 2, (size_t) n);
        convolution.process (juce::dsp::ProcessContextReplacing<float> (block));

        if (kit.reverb.roomComp > 0.0f)
        {
            const float rc = kit.reverb.roomComp;
            const float thr = -14.0f - 20.0f * rc, ratio = 2.0f + 18.0f * rc;
            const float makeup = juce::Decibels::decibelsToGain (-thr * (1.0f - 1.0f / ratio) * 0.6f);
            for (int i = 0; i < n; ++i)
            {
                const float g = roomComp.gain (juce::jmax (std::abs (rL[i]), std::abs (rR[i])), thr, ratio, 6.0f) * makeup;
                rL[i] *= g; rR[i] *= g;
            }
        }

        if (gateMs > 0.0f)
        {
            for (int i = 0; i < n; ++i)
            {
                const float key = std::abs (gk[i]);
                gateKeyPeak = juce::jmax (key, gateKeyPeak * keyDecay);
                if (key > juce::jmax (0.004f, gateKeyPeak * 0.3f)) gateHold = holdSamples;
                const float target = gateHold > 0 ? 1.0f : 0.0f;
                if (gateHold > 0) --gateHold;
                gate += (target - gate) * (target > gate ? gateAtt : gateRel);
                rL[i] *= gate; rR[i] *= gate;
            }
        }

        // echo (slapback, digital delay): its own lines, low-passed in the loop
        const auto& dl = kit.delay;
        if (dl.ms > 0.0f)
        {
            const int dL = juce::jlimit (1, echoLen - 1, (int) (dl.ms * 0.001 * sr));
            const int dR = juce::jlimit (1, echoLen - 1, (int) (dl.ms * (1.0f + 0.25f * dl.spread) * 0.001 * sr));
            auto& eL = echo[0]; auto& eR = echo[1];
            for (int i = 0; i < n; ++i)
            {
                const float yl = eL[(size_t) ((echoWrite - dL + echoLen) % echoLen)];
                const float yr = eR[(size_t) ((echoWrite - dR + echoLen) % echoLen)];
                const float fl = echoLp[0].process (yl), fr = echoLp[1].process (yr);
                const float cross = dl.spread * 0.5f;
                eL[(size_t) echoWrite] = fx[i] + dl.feedback * (fl * (1.0f - cross) + fr * cross);
                eR[(size_t) echoWrite] = fx[i] + dl.feedback * (fr * (1.0f - cross) + fl * cross);
                const float w = mono ? 0.5f * (fl + fr) : 0.0f;
                rL[i] += (mono ? w : fl) * 0.8f;
                rR[i] += (mono ? w : fr) * 0.8f;
                echoWrite = (echoWrite + 1) % echoLen;
            }
        }

        juce::FloatVectorOperations::multiply (rL, roomG, n);
        juce::FloatVectorOperations::multiply (rR, roomG, n);
        juce::FloatVectorOperations::add (L, rL, n);
        juce::FloatVectorOperations::add (R, rR, n);
    }

    //--------------------------------------------------------------------------
    // the mix bus
    for (int ch = 0; ch < 2; ++ch)
    {
        float* d = ch == 0 ? L : R;
        if (useLowCut)    for (int i = 0; i < n; ++i) d[i] = lowCut[(size_t) ch].process (d[i]);
        if (useHighCut)   for (int i = 0; i < n; ++i) d[i] = highCut[(size_t) ch].process (d[i]);
        if (useLowShelf)  for (int i = 0; i < n; ++i) d[i] = lowShelf[(size_t) ch].process (d[i]);
        if (useHighShelf) for (int i = 0; i < n; ++i) d[i] = highShelf[(size_t) ch].process (d[i]);
    }

    if (knobs.comp > 0.001f)
    {
        const auto& c = kit.compressor;
        const float thr = -4.0f - 26.0f * knobs.comp;
        const float ratio = juce::jmax (1.0f, c.ratio);
        const float makeup = juce::Decibels::decibelsToGain (-thr * (1.0f - 1.0f / ratio) * 0.45f);
        const float p = juce::jlimit (0.0f, 1.0f, c.parallel);
        for (int i = 0; i < n; ++i)
        {
            const float g = busComp.gain (juce::jmax (std::abs (L[i]), std::abs (R[i])), thr, ratio, 6.0f) * makeup;
            const float cl = L[i] * g, cr = R[i] * g;
            L[i] = cl * (1.0f - p) + (L[i] + cl) * 0.6f * p;
            R[i] = cr * (1.0f - p) + (R[i] + cr) * 0.6f * p;
        }
    }

    if (knobs.tape > 0.001f)
    {
        if (knobs.tape != lastTape)
        {
            lastTape = knobs.tape;
            tapeCoef = (float) (1.0 - std::exp (-2.0 * pi * (19000.0 - 10000.0 * knobs.tape) / sr));
        }
        const float drive = 1.0f + 3.0f * knobs.tape;
        for (int ch = 0; ch < 2; ++ch)
        {
            float* d = ch == 0 ? L : R;
            float& lp = tapeLp[(size_t) ch];
            for (int i = 0; i < n; ++i)
            {
                lp += (d[i] - lp) * tapeCoef;
                d[i] = std::tanh (lp * drive) / drive;
            }
        }
    }

    if (std::abs (kit.bus.outDb) > 0.01f)
    {
        const float g = juce::Decibels::decibelsToGain (kit.bus.outDb);
        juce::FloatVectorOperations::multiply (L, g, n);
        juce::FloatVectorOperations::multiply (R, g, n);
    }
}

} // namespace thoreau
