#pragma once

#include "DrumVoices.h"
#include <vector>

namespace thoreau
{

//==============================================================================
/** What a slot's drum is. analog = THOREAU's original synth voices; the rest are
    physically modelled acoustic drums (AcousticDrum). */
enum class Kind : uint8_t
{
    analog,
    kick, snare, rimshot, sidestick, brush, sweep, tom,
    hatClosed, hatOpen, hatPedal, crash, ride, rideBell, china, splash, gong,
    tambourine, claps, syndrum, cowbell
};

inline bool isMembrane (Kind k) noexcept { return k >= Kind::kick && k <= Kind::tom; }
inline bool isMetal (Kind k) noexcept    { return k >= Kind::hatClosed && k <= Kind::gong; }

/** How a drum sounds before the user's TUNE / RING / TONE / ATTACK knobs. */
struct DrumModel
{
    Kind kind = Kind::analog;
    Instrument analog = Instrument::kick;   // for Kind::analog
    float pitch = 100.0f;    // Hz: head fundamental (membranes), lowest partial (metal), bell / cowbell pitch
    float ring = 0.5f;       // s: how long the main resonance rings (T60)
    float bright = 0.5f;     // 0..1
    float hard = 0.5f;       // 0..1 stick / beater hardness
    float damp = 0.4f;       // 0..1 muffling (upper modes die faster)
    float extra = 0.5f;      // kick: beater click; snare: wires; tom: resonant head; china: trash;
                             // tambourine: jingles; claps: how many people; syndrum: sweep
    uint32_t seed = 1;       // cymbals: which cymbal (its partials)

    bool operator== (const DrumModel& o) const noexcept
    {
        return kind == o.kind && analog == o.analog && pitch == o.pitch && ring == o.ring && bright == o.bright
            && hard == o.hard && damp == o.damp && extra == o.extra && seed == o.seed;
    }
};

struct SlotDef
{
    juce::String name;
    DrumModel model;
    float levelDb = 0.0f, pan = 0.0f;    // mixer defaults
    float close = 1.0f;                  // close mic
    float oh = 0.3f;                     // overheads
    float room = 0.3f;                   // room send
    float fx = 0.0f;                     // delay send
    int midi = 36;
    int chokes = -1;                     // slot this one silences (closed hat -> open hat)
};

struct ReverbDef
{
    float size = 1.0f;          // 0.3 .. 3 (room dimensions)
    float rt60 = 0.8f;          // s
    float damp = 0.5f;          // high-frequency damping 0..1
    float predelayMs = 5.0f;
    float early = 0.5f;         // early reflections level
    float lowCut = 150.0f;      // Hz, into the room
    float gateMs = 0.0f;        // > 0: gated (the 80s)
    float roomComp = 0.0f;      // 0..1: crushed room mics
};

struct DelayDef
{
    float ms = 0.0f;            // 0 = no delay
    float feedback = 0.0f;
    float lowpass = 6000.0f;
    float spread = 0.0f;        // 0..1 stereo spread of the repeats
};

struct CompDef
{
    float attackMs = 10.0f, releaseMs = 120.0f, ratio = 4.0f;
    float parallel = 0.0f;      // 0 = in line, 1 = all parallel (New York)
};

struct BusDef
{
    float lowShelfDb = 0.0f, highShelfDb = 0.0f;
    float lowCut = 20.0f, highCut = 20000.0f;
    float outDb = 0.0f;
};

/** A starter beat (A) and fill (B): one string per slot, x accent, o normal, g ghost, . rest. */
struct Groove
{
    int length = 16, stepsPerBeat = 4;
    double bpm = 120.0;
    float swing = 50.0f;
    std::array<const char*, kTracks> beat {}, fill {};
};

struct KitDef
{
    juce::String name, description;
    bool acoustic = true;
    std::array<SlotDef, kTracks> slots;
    ReverbDef reverb;
    DelayDef delay;
    CompDef compressor;
    BusDef bus;
    // the kit knobs' defaults
    float closeDb = 0.0f, ohDb = -6.0f, roomDb = -10.0f, comp = 0.4f, tape = 0.3f;
    Groove groove;
};

/** Every kit. Built once (call from the message thread before the audio starts). */
const std::vector<KitDef>& kits();
int defaultKit();       // for new projects
int analogKit();        // THOREAU's original sounds (old projects)
inline const KitDef& kitAt (int i) { const auto& k = kits(); return k[(size_t) juce::jlimit (0, (int) k.size() - 1, i)]; }

} // namespace thoreau
