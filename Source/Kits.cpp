#include "Kits.h"

namespace thoreau
{

namespace
{
    // Where each slot sits on a right-handed kit, from the drummer's seat (- left, + right).
    const float seatPan[kTracks] = { 0.0f, 0.04f, 0.08f, 0.04f, -0.42f, -0.42f, 0.55f, 0.18f, -0.2f, -0.55f, 0.6f, 0.38f };

    DrumModel model (Kind k, float pitch, float ring, float bright, float hard, float damp, float extra, uint32_t seed = 1)
    {
        DrumModel m;
        m.kind = k; m.pitch = pitch; m.ring = ring; m.bright = bright; m.hard = hard; m.damp = damp; m.extra = extra; m.seed = seed;
        return m;
    }

    /** A slot, with the usual mic sends for what it is. */
    SlotDef slot (const char* name, DrumModel m, float levelDb, int midi)
    {
        SlotDef s;
        s.name = name;
        s.model = m;
        s.levelDb = levelDb;
        s.midi = midi;
        switch (m.kind)
        {
            case Kind::kick:                                      s.close = 1.0f; s.oh = 0.30f; s.room = 0.45f; break;
            case Kind::snare: case Kind::rimshot:
            case Kind::brush: case Kind::sweep:                   s.close = 1.0f; s.oh = 0.55f; s.room = 0.85f; break;
            case Kind::sidestick:                                 s.close = 1.0f; s.oh = 0.45f; s.room = 0.5f;  break;
            case Kind::tom:                                       s.close = 1.0f; s.oh = 0.5f;  s.room = 0.75f; break;
            case Kind::hatClosed: case Kind::hatOpen:
            case Kind::hatPedal:                                  s.close = 0.7f; s.oh = 0.9f;  s.room = 0.3f;  break;
            case Kind::crash: case Kind::ride: case Kind::rideBell:
            case Kind::china: case Kind::splash: case Kind::gong: s.close = 0.3f; s.oh = 1.0f;  s.room = 0.55f; break;
            case Kind::analog: case Kind::tambourine: case Kind::claps:
            case Kind::syndrum: case Kind::cowbell:               s.close = 0.9f; s.oh = 0.6f;  s.room = 0.6f;  break;
        }
        return s;
    }

    SlotDef xstick (float pitch, float levelDb = -6.0f) { return slot ("X-STICK", model (Kind::sidestick, pitch, 0.12f, 0.5f, 0.6f, 0.5f, 0.25f), levelDb, 37); }
    SlotDef rimshot (float pitch, float ring, float bright, float levelDb = -4.0f) { return slot ("RIMSHOT", model (Kind::rimshot, pitch, ring, bright, 0.85f, 0.5f, 0.5f), levelDb, 40); }
    SlotDef closedHat (float ring, float bright, uint32_t seed, float levelDb) { return slot ("CL HAT", model (Kind::hatClosed, 100.0f, ring, bright, 0.55f, 0.0f, 0.0f, seed), levelDb, 42); }
    SlotDef openHat (float ring, float bright, uint32_t seed, float levelDb)   { return slot ("OP HAT", model (Kind::hatOpen, 100.0f, ring, bright, 0.55f, 0.0f, 0.0f, seed), levelDb, 46); }
    SlotDef crash (float ring, float bright, uint32_t seed, float levelDb = -9.0f, float pitch = 100.0f, const char* name = "CRASH", int midi = 49)
    {
        return slot (name, model (Kind::crash, pitch, ring, bright, 0.6f, 0.0f, 0.0f, seed), levelDb, midi);
    }
    SlotDef ride (float ring, float bright, uint32_t seed, float levelDb = -10.0f, float rivets = 0.0f, const char* name = "RIDE")
    {
        return slot (name, model (Kind::ride, 100.0f, ring, bright, 0.6f, 0.0f, rivets, seed), levelDb, 51);
    }
    SlotDef tom (const char* name, float pitch, float ring, float bright, float hard, float damp, float reso, float levelDb, int midi)
    {
        return slot (name, model (Kind::tom, pitch, ring, bright, hard, damp, reso), levelDb, midi);
    }
    SlotDef tambourine (float jingles = 0.6f, float levelDb = -10.0f)
    {
        return slot ("TAMBOURINE", model (Kind::tambourine, 100.0f, 0.18f, 0.6f, 0.6f, 0.0f, jingles, 61), levelDb, 54);
    }
    SlotDef claps (float people = 0.6f, float levelDb = -6.0f)
    {
        return slot ("HANDCLAPS", model (Kind::claps, 100.0f, 0.2f, 0.55f, 0.5f, 0.0f, people, 71), levelDb, 39);
    }

    /** The common shape of a kit: kick, snare, (slot 2), cross-stick, hats, three toms, crash, ride, (slot 11). */
    KitDef kit (const char* name, const char* description, float width, std::array<SlotDef, kTracks> slots)
    {
        KitDef k;
        k.name = name;
        k.description = description;
        k.slots = std::move (slots);
        for (int i = 0; i < kTracks; ++i)
            k.slots[(size_t) i].pan = seatPan[i] * width;
        k.slots[4].chokes = 5;                              // closing the hats silences the open hat
        return k;
    }

    ReverbDef room (float size, float rt60, float damp, float predelayMs, float early, float lowCut, float gateMs = 0.0f, float roomComp = 0.0f)
    {
        ReverbDef r;
        r.size = size; r.rt60 = rt60; r.damp = damp; r.predelayMs = predelayMs; r.early = early; r.lowCut = lowCut;
        r.gateMs = gateMs; r.roomComp = roomComp;
        return r;
    }

    CompDef compressor (float attackMs, float releaseMs, float ratio, float parallel = 0.0f)
    {
        CompDef c;
        c.attackMs = attackMs; c.releaseMs = releaseMs; c.ratio = ratio; c.parallel = parallel;
        return c;
    }

    BusDef bus (float lowShelfDb, float highShelfDb, float lowCut, float highCut, float outDb = 0.0f)
    {
        BusDef b;
        b.lowShelfDb = lowShelfDb; b.highShelfDb = highShelfDb; b.lowCut = lowCut; b.highCut = highCut; b.outDb = outDb;
        return b;
    }

    void knobs (KitDef& k, float closeDb, float ohDb, float roomDb, float comp, float tape)
    {
        k.closeDb = closeDb; k.ohDb = ohDb; k.roomDb = roomDb; k.comp = comp; k.tape = tape;
    }

    Groove groove (double bpm, float swing, std::array<const char*, kTracks> beat, std::array<const char*, kTracks> fill,
                   int length = 16, int stepsPerBeat = 4)
    {
        Groove g;
        g.bpm = bpm; g.swing = swing; g.beat = beat; g.fill = fill; g.length = length; g.stepsPerBeat = stepsPerBeat;
        return g;
    }

    using Rows = std::array<const char*, kTracks>;
    constexpr const char* _ = nullptr;

    //==========================================================================
    std::vector<KitDef> build()
    {
        std::vector<KitDef> all;

        //----------------------------------------------------------------------
        // THOREAU's original synth kit (and what old projects open with)
        {
            KitDef k;
            k.name = "THOREAU ANALOGUE";
            k.description = "THOREAU's own analogue-style drum synth.";
            k.acoustic = false;
            const float levels[kTracks] = { 0, -2, -4, -6, -7, -8, -4, -4, -4, -9, -10, -9 };
            const float pans[kTracks]   = { 0, 0, 0.1f, -0.15f, 0.25f, 0.25f, -0.35f, 0.0f, 0.35f, -0.3f, 0.3f, -0.2f };
            for (int t = 0; t < kTracks; ++t)
            {
                auto& s = k.slots[(size_t) t];
                s.name = instrumentName (t);
                s.model.kind = Kind::analog;
                s.model.analog = instrumentOf (t);
                s.levelDb = levels[t];
                s.pan = pans[t];
                s.close = 1.0f; s.oh = 0.0f; s.room = 0.35f; s.fx = 0.3f;
                s.midi = defaultMidiNote (t);
            }
            k.slots[4].chokes = 5;
            k.reverb = room (1.1f, 1.3f, 0.45f, 12.0f, 0.4f, 120.0f);
            k.delay = { 375.0f, 0.3f, 5000.0f, 0.7f };
            k.compressor = compressor (5, 100, 4);
            knobs (k, 0.0f, -60.0f, -60.0f, 0.0f, 0.0f);
            k.groove = groove (120.0, 50.0f,
                Rows { "x.......x.x.....", "....x.......x...", _, _, "x.g.x.g.x.g.x.g.", "..............o.", _, _, _, _, _, _ },
                Rows { "x.......x.x.....", "....x.......x.go", _, _, "x.g.x.g.x.g.x...", _, "..............o.", "............o...", "..........o.....", _, _, _ });
            all.push_back (k);
        }

        //----------------------------------------------------------------------
        // Ringo, Abbey Road Studio Two, mid-60s (Emerick's close miking, Fairchild squash)
        {
            auto k = kit ("STUDIO TWO '66",
                          "Ringo at Abbey Road: Ludwig kit, tea towels on the toms, mics in close and squashed hard. Narrow, dead, thumpy.",
                          0.35f,
                          { slot ("KICK", model (Kind::kick, 56, 0.3f, 0.35f, 0.3f, 0.75f, 0.35f), 0, 36),
                            slot ("SNARE", model (Kind::snare, 205, 0.2f, 0.4f, 0.55f, 0.72f, 0.55f), -1, 38),
                            claps (0.5f, -7),
                            xstick (205),
                            closedHat (0.13f, 0.35f, 11, -9), openHat (1.4f, 0.35f, 11, -11),
                            tom ("FLOOR TOM", 82, 0.5f, 0.35f, 0.45f, 0.85f, 0.25f, -3, 41),
                            tom ("TOM 2", 108, 0.45f, 0.35f, 0.45f, 0.85f, 0.25f, -3, 45),
                            tom ("TOM 1", 136, 0.42f, 0.38f, 0.45f, 0.85f, 0.25f, -3, 48),
                            crash (2.6f, 0.32f, 21, -10), ride (4.0f, 0.38f, 31, -11),
                            tambourine (0.6f, -9) });
            k.slots[2].room = 0.9f;
            k.reverb = room (0.9f, 0.95f, 0.6f, 8.0f, 0.4f, 200.0f);
            k.compressor = compressor (8, 300, 6);
            k.bus = bus (1.0f, -3.0f, 40.0f, 14000.0f, 5.5f);
            knobs (k, 0.0f, -8.0f, -16.0f, 0.75f, 0.55f);
            k.groove = groove (124.0, 50.0f,
                Rows { "x.....x.x.....x.", "....x.......x...", "....x.......x...", _, "o.o.o.o.o.o.o.o.", _, _, _, _, _, _, "o.o.o.o.o.o.o.o." },
                Rows { "x.....x.x.......", "....x...........", _, _, "o.o.o.o.........", _, "............o.oo", "..........o.o...", "........o.o.....", _, _, "o.o.o.o.o.o....." });
            all.push_back (k);
        }

        //----------------------------------------------------------------------
        // Nick Mason, Abbey Road, 1973: deep, damped toms; Rototoms on "Time"
        {
            auto k = kit ("DARK SIDE '73",
                          "Nick Mason, 1973: deep, damped toms that still sing, fat snare, dry room. Rototoms in place of percussion.",
                          1.0f,
                          { slot ("KICK", model (Kind::kick, 50, 0.38f, 0.3f, 0.3f, 0.7f, 0.3f), 0, 36),
                            slot ("SNARE", model (Kind::snare, 185, 0.32f, 0.35f, 0.5f, 0.6f, 0.5f), -1, 38),
                            rimshot (188, 0.3f, 0.4f),
                            xstick (190),
                            closedHat (0.11f, 0.4f, 12, -10), openHat (1.6f, 0.4f, 12, -12),
                            tom ("FLOOR TOM", 66, 1.0f, 0.35f, 0.4f, 0.55f, 0.6f, -2, 41),
                            tom ("TOM 2", 90, 0.85f, 0.35f, 0.4f, 0.55f, 0.6f, -2, 45),
                            tom ("TOM 1", 118, 0.75f, 0.38f, 0.4f, 0.55f, 0.6f, -2, 48),
                            crash (3.0f, 0.45f, 22, -10), ride (4.5f, 0.4f, 32, -11),
                            tom ("ROTOTOM", 235, 0.5f, 0.55f, 0.6f, 0.3f, 0.0f, -5, 50) });
            k.reverb = room (1.0f, 0.7f, 0.7f, 10.0f, 0.3f, 200.0f);
            k.compressor = compressor (15, 200, 3);
            k.bus = bus (2.0f, -1.0f, 30.0f, 16000.0f, 3.5f);
            knobs (k, 0.0f, -7.0f, -20.0f, 0.45f, 0.35f);
            k.groove = groove (110.0, 50.0f,
                Rows { "x.......x.x.....", "....x.......x...", _, _, "o.o.o.o.o.o.o.o.", _, _, _, _, _, _, _ },
                Rows { "x.......x.......", "....x...........", _, _, "o.o.o.o.........", _, "..............oo", "............oo..", "..........oo....", _, _, "........oooo...." });
            all.push_back (k);
        }

        //----------------------------------------------------------------------
        // Roger Taylor, mid-70s: big open kit, ringing toms, the gong
        {
            auto k = kit ("STADIUM '77",
                          "Roger Taylor's big 70s kit: open toms that ring on, a deep snare, bright cymbals, a large room. And the gong.",
                          1.0f,
                          { slot ("KICK", model (Kind::kick, 48, 0.5f, 0.45f, 0.45f, 0.4f, 0.6f), 0, 36),
                            slot ("SNARE", model (Kind::snare, 195, 0.42f, 0.55f, 0.6f, 0.4f, 0.6f), -1, 38),
                            rimshot (198, 0.4f, 0.6f),
                            xstick (200),
                            closedHat (0.14f, 0.6f, 13, -10), openHat (1.8f, 0.6f, 13, -12),
                            tom ("FLOOR TOM", 74, 1.4f, 0.5f, 0.5f, 0.3f, 0.8f, -2, 41),
                            tom ("TOM 2", 104, 1.2f, 0.5f, 0.5f, 0.3f, 0.8f, -2, 45),
                            tom ("TOM 1", 134, 1.05f, 0.52f, 0.5f, 0.3f, 0.8f, -2, 48),
                            crash (3.6f, 0.65f, 23, -9, 110.0f), ride (5.0f, 0.55f, 33, -10),
                            slot ("GONG", model (Kind::gong, 70, 9.0f, 0.5f, 0.5f, 0.0f, 0.0f, 41), -6, 52) });
            k.reverb = room (2.0f, 1.8f, 0.4f, 18.0f, 0.6f, 120.0f);
            k.compressor = compressor (10, 150, 2.5f, 0.3f);
            k.bus = bus (1.5f, 2.0f, 30.0f, 20000.0f, 2.8f);
            knobs (k, 0.0f, -3.0f, -9.0f, 0.35f, 0.3f);
            k.groove = groove (114.0, 50.0f,
                Rows { "x.....x.x.......", "....x.......x...", _, _, "o.o.o.o.o.o.o.o.", _, _, _, _, _, _, _ },
                Rows { "x.......x.......", "....x...........", _, _, "o.o.o.o.........", _, "............oooo", "..........oo....", "........oo......", _, _, _ });
            all.push_back (k);
        }

        //----------------------------------------------------------------------
        // Spector / Hal Blaine at Gold Star, Joe Meek: mono, echo chamber, compressed
        {
            auto k = kit ("WALL OF SOUND '63",
                          "Phil Spector and Joe Meek: one mono kit drowned in the echo chamber, pumping compression, handclaps and tambourine.",
                          0.0f,
                          { slot ("KICK", model (Kind::kick, 55, 0.32f, 0.25f, 0.3f, 0.6f, 0.2f), -2, 36),
                            slot ("SNARE", model (Kind::snare, 200, 0.35f, 0.4f, 0.55f, 0.5f, 0.6f), 0, 38),
                            claps (0.8f, -6),
                            xstick (200),
                            closedHat (0.12f, 0.35f, 14, -11), openHat (1.4f, 0.35f, 14, -13),
                            tom ("FLOOR TOM", 78, 1.0f, 0.35f, 0.45f, 0.35f, 0.6f, -2, 41),
                            tom ("TOM 2", 108, 0.9f, 0.35f, 0.45f, 0.35f, 0.6f, -2, 45),
                            tom ("TOM 1", 140, 0.8f, 0.38f, 0.45f, 0.35f, 0.6f, -2, 48),
                            crash (2.8f, 0.4f, 24, -12), ride (4.0f, 0.4f, 34, -12),
                            tambourine (0.8f, -6) });
            k.slots[0].room = 0.3f;
            k.slots[1].room = 1.0f; k.slots[2].room = 1.0f; k.slots[11].room = 1.0f;
            k.reverb = room (2.4f, 2.5f, 0.65f, 25.0f, 0.3f, 250.0f);
            k.compressor = compressor (3, 120, 8);
            k.bus = bus (-2.0f, -4.0f, 70.0f, 11000.0f, 6.0f);
            knobs (k, -2.0f, -6.0f, -3.0f, 0.7f, 0.6f);
            k.groove = groove (132.0, 50.0f,
                Rows { "x.......x.......", "....x.......x...", "....x.......x...", _, "o.o.o.o.o.o.o.o.", _, _, _, _, _, _, "..o...o...o...o." },
                Rows { "x.......x.......", "....x.......x.ox", _, _, "o.o.o.o.o.o.....", _, ".............o..", "..........oo....", "........oo......", _, _, "..o...o...o....." });
            all.push_back (k);
        }

        //----------------------------------------------------------------------
        // Martin Hannett / Stephen Morris: dry, clipped, AMS delay, Synare
        {
            auto k = kit ("FACTORY '79",
                          "Martin Hannett and Stephen Morris: dry, tight, clipped drums in a metallic digital room, snare into the delay, a Synare.",
                          0.6f,
                          { slot ("KICK", model (Kind::kick, 58, 0.2f, 0.5f, 0.6f, 0.85f, 0.5f), 0, 36),
                            slot ("SNARE", model (Kind::snare, 250, 0.2f, 0.65f, 0.75f, 0.8f, 0.45f), -1, 38),
                            rimshot (250, 0.2f, 0.65f),
                            xstick (240),
                            closedHat (0.08f, 0.7f, 15, -9), openHat (1.0f, 0.7f, 15, -12),
                            tom ("FLOOR TOM", 84, 0.4f, 0.45f, 0.6f, 0.8f, 0.2f, -3, 41),
                            tom ("TOM 2", 114, 0.36f, 0.45f, 0.6f, 0.8f, 0.2f, -3, 45),
                            tom ("TOM 1", 148, 0.32f, 0.48f, 0.6f, 0.8f, 0.2f, -3, 48),
                            crash (2.4f, 0.6f, 25, -12), ride (3.5f, 0.55f, 35, -12),
                            slot ("SYNDRUM", model (Kind::syndrum, 160, 0.7f, 0.6f, 0.6f, 0.0f, 0.7f), -7, 47) });
            k.slots[1].fx = 0.35f; k.slots[2].fx = 0.35f; k.slots[11].fx = 0.5f;
            k.reverb = room (0.5f, 0.7f, 0.2f, 30.0f, 0.9f, 300.0f);
            k.delay = { 85.0f, 0.25f, 7000.0f, 0.8f };
            k.compressor = compressor (1, 80, 4);
            k.bus = bus (-2.0f, 2.0f, 50.0f, 18000.0f, 1.0f);
            knobs (k, 0.0f, -10.0f, -9.0f, 0.5f, 0.1f);
            k.groove = groove (126.0, 50.0f,
                Rows { "x.......x.......", "....x.......x...", _, _, "o.o.o.o.o.o.o.o.", _, _, _, _, _, _, _ },
                Rows { "x.......x.......", "....x.......x...", _, _, "o.o.o.o.o.o.o.o.", _, _, "..........o.....", "........o.......", _, _, "..............o." });
            all.push_back (k);
        }

        //----------------------------------------------------------------------
        // Hugh Padgham & Steve Lillywhite: the gated room
        {
            auto k = kit ("GATED '82",
                          "Padgham and Lillywhite: a huge live room, crushed and gated. Single-headed toms, big snare, claps and cowbell.",
                          1.0f,
                          { slot ("KICK", model (Kind::kick, 52, 0.4f, 0.5f, 0.55f, 0.5f, 0.5f), 0, 36),
                            slot ("SNARE", model (Kind::snare, 180, 0.36f, 0.55f, 0.6f, 0.4f, 0.65f), -1, 38),
                            claps (0.7f, -6),
                            xstick (190),
                            closedHat (0.12f, 0.55f, 16, -11), openHat (1.5f, 0.55f, 16, -13),
                            tom ("FLOOR TOM", 72, 1.2f, 0.55f, 0.55f, 0.3f, 0.0f, -1, 41),
                            tom ("TOM 2", 98, 1.0f, 0.55f, 0.55f, 0.3f, 0.0f, -1, 45),
                            tom ("TOM 1", 128, 0.9f, 0.58f, 0.55f, 0.3f, 0.0f, -1, 48),
                            crash (3.0f, 0.6f, 26, -12), ride (4.0f, 0.55f, 36, -12),
                            slot ("COWBELL", model (Kind::cowbell, 560, 0.4f, 0.6f, 0.6f, 0.0f, 0.0f), -9, 56) });
            for (int t : { 1, 2, 6, 7, 8 }) k.slots[(size_t) t].room = 1.0f;
            for (int t : { 4, 5, 9, 10 })   k.slots[(size_t) t].room = 0.15f;
            k.reverb = room (2.6f, 2.2f, 0.35f, 6.0f, 0.7f, 150.0f, 230.0f, 0.9f);
            k.compressor = compressor (3, 100, 4, 0.25f);
            k.bus = bus (2.0f, 3.0f, 30.0f, 20000.0f, 0.8f);
            knobs (k, 0.0f, -6.0f, -1.0f, 0.45f, 0.15f);
            k.groove = groove (96.0, 50.0f,
                Rows { "x.......x.x.....", "....x.......x...", _, _, "o.o.o.o.o.o.o.o.", _, _, _, _, _, _, _ },
                Rows { "x...............", "................", _, _, "................", _, "............o.oo", "........o.oo....", "....o.oo........", _, _, _ });
            all.push_back (k);
        }

        //----------------------------------------------------------------------
        // Steve Albini: the kit in a big live room, no tricks
        {
            auto k = kit ("ELECTRICAL '93",
                          "Steve Albini: an open, natural kit in a big live room. Room mics up, no compression, nothing hidden.",
                          1.0f,
                          { slot ("KICK", model (Kind::kick, 50, 0.6f, 0.55f, 0.6f, 0.3f, 0.7f), 0, 36),
                            slot ("SNARE", model (Kind::snare, 225, 0.45f, 0.65f, 0.7f, 0.3f, 0.55f), -1, 38),
                            rimshot (228, 0.45f, 0.65f),
                            xstick (225),
                            closedHat (0.15f, 0.6f, 17, -10), openHat (1.8f, 0.6f, 17, -12),
                            tom ("FLOOR TOM", 78, 1.4f, 0.6f, 0.6f, 0.25f, 0.8f, -2, 41),
                            tom ("TOM 2", 105, 1.2f, 0.6f, 0.6f, 0.25f, 0.8f, -2, 45),
                            tom ("TOM 1", 138, 1.1f, 0.62f, 0.6f, 0.25f, 0.8f, -2, 48),
                            crash (3.8f, 0.7f, 27, -9), ride (5.0f, 0.6f, 37, -10),
                            crash (3.0f, 0.7f, 28, -10, 125.0f, "CRASH 2", 57) });
            k.reverb = room (2.2f, 1.4f, 0.3f, 12.0f, 0.8f, 80.0f);
            k.compressor = compressor (20, 200, 1.5f);
            k.bus = bus (0.0f, 0.0f, 25.0f, 20000.0f, 1.2f);
            knobs (k, -1.0f, 0.0f, -5.0f, 0.1f, 0.15f);
            k.groove = groove (122.0, 50.0f,
                Rows { "x.....x.x.......", "....x.......x...", _, _, "o.o.o.o.o.o.o.o.", _, _, _, _, _, _, _ },
                Rows { "x.......x.......", "....x...........", _, _, "o.o.o.o.........", _, "............oooo", "..........oo....", "........oo......", _, _, _ });
            all.push_back (k);
        }

        //----------------------------------------------------------------------
        // Slim Jim Phantom, Brian Setzer, The Living End: stand-up kit, slapback
        {
            auto k = kit ("ROCKABILLY",
                          "Stray Cats and The Living End: a small kit with an open, boomy kick, a high ringing snare for rimshot backbeats, slapback echo.",
                          0.5f,
                          { slot ("KICK", model (Kind::kick, 60, 0.55f, 0.35f, 0.35f, 0.25f, 0.3f), 0, 36),
                            slot ("SNARE", model (Kind::snare, 270, 0.5f, 0.55f, 0.6f, 0.3f, 0.4f), -2, 38),
                            rimshot (270, 0.5f, 0.6f, -2),
                            xstick (260),
                            closedHat (0.13f, 0.5f, 18, -11), openHat (1.4f, 0.5f, 18, -13),
                            tom ("FLOOR TOM", 92, 1.2f, 0.45f, 0.5f, 0.3f, 0.6f, -2, 41),
                            tom ("TOM 2", 120, 1.0f, 0.45f, 0.5f, 0.3f, 0.6f, -2, 45),
                            tom ("TOM 1", 150, 0.9f, 0.48f, 0.5f, 0.3f, 0.6f, -2, 48),
                            crash (2.8f, 0.5f, 29, -11), ride (4.0f, 0.5f, 38, -11),
                            slot ("RIDE BELL", model (Kind::rideBell, 720, 2.5f, 0.6f, 0.6f, 0.0f, 0.0f, 39), -11, 53) });
            k.slots[1].fx = 0.4f; k.slots[2].fx = 0.5f; k.slots[3].fx = 0.3f;
            k.reverb = room (0.8f, 0.8f, 0.5f, 8.0f, 0.5f, 150.0f);
            k.delay = { 105.0f, 0.12f, 4500.0f, 0.2f };
            k.compressor = compressor (10, 150, 2);
            k.bus = bus (0.0f, -2.0f, 50.0f, 12000.0f, 5.5f);
            knobs (k, 0.0f, -4.0f, -12.0f, 0.3f, 0.55f);
            k.groove = groove (168.0, 50.0f,
                Rows { "x.....x.x.....x.", _, "....x.......x...", _, _, _, "o.o.o.o.o.o.o.o.", _, _, _, _, _ },
                Rows { "x.......x.......", "....o.o.o.o.oooo", _, _, _, _, "o.o.o.o.........", _, _, _, _, _ });
            all.push_back (k);
        }

        //----------------------------------------------------------------------
        // The Strokes, Arctic Monkeys (James Ford): dry, tight, mid-heavy
        {
            auto k = kit ("GARAGE '01",
                          "The Strokes and Arctic Monkeys: a tight, dry kit in a small room, compressed, mid-forward and a little lo-fi.",
                          0.6f,
                          { slot ("KICK", model (Kind::kick, 60, 0.22f, 0.5f, 0.6f, 0.8f, 0.7f), 0, 36),
                            slot ("SNARE", model (Kind::snare, 215, 0.25f, 0.5f, 0.65f, 0.7f, 0.6f), -1, 38),
                            rimshot (218, 0.25f, 0.5f),
                            xstick (215),
                            closedHat (0.1f, 0.55f, 19, -9), openHat (1.2f, 0.55f, 19, -11),
                            tom ("FLOOR TOM", 88, 0.5f, 0.45f, 0.6f, 0.65f, 0.3f, -2, 41),
                            tom ("TOM 2", 118, 0.45f, 0.45f, 0.6f, 0.65f, 0.3f, -2, 45),
                            tom ("TOM 1", 150, 0.4f, 0.48f, 0.6f, 0.65f, 0.3f, -2, 48),
                            crash (2.6f, 0.5f, 30, -11), ride (3.8f, 0.5f, 40, -11),
                            tambourine (0.6f, -9) });
            k.reverb = room (0.45f, 0.45f, 0.5f, 3.0f, 0.8f, 200.0f);
            k.compressor = compressor (4, 90, 6);
            k.bus = bus (-2.0f, -3.0f, 60.0f, 9500.0f, 6.0f);
            knobs (k, 0.0f, -6.0f, -12.0f, 0.65f, 0.6f);
            k.groove = groove (138.0, 50.0f,
                Rows { "x.....x.x.....x.", "....x.......x...", _, _, "o.o.o.o.o.o.o.o.", _, _, _, _, _, _, "o.o.o.o.o.o.o.o." },
                Rows { "x.....x.x.......", "....x.......o.o.", _, _, "o.o.o.o.o.o.....", _, "..............oo", _, _, _, _, "o.o.o.o.o.o.o.o." });
            all.push_back (k);
        }

        //----------------------------------------------------------------------
        // Travis Barker (blink-182, Jerry Finn), The Living End: cracking, clicky, bright
        {
            auto k = kit ("POP PUNK '99",
                          "blink-182 era: a high, cracking snare, a clicky kick that cuts through guitars, big bright cymbals and a china.",
                          1.0f,
                          { slot ("KICK", model (Kind::kick, 56, 0.35f, 0.7f, 0.85f, 0.6f, 1.0f), 0, 36),
                            slot ("SNARE", model (Kind::snare, 290, 0.35f, 0.75f, 0.85f, 0.5f, 0.5f), -1, 38),
                            rimshot (292, 0.35f, 0.75f),
                            xstick (280),
                            closedHat (0.12f, 0.75f, 20, -10), openHat (1.6f, 0.75f, 20, -12),
                            tom ("FLOOR TOM", 90, 0.9f, 0.65f, 0.75f, 0.4f, 0.6f, -2, 41),
                            tom ("TOM 2", 120, 0.8f, 0.65f, 0.75f, 0.4f, 0.6f, -2, 45),
                            tom ("TOM 1", 155, 0.7f, 0.68f, 0.75f, 0.4f, 0.6f, -2, 48),
                            crash (3.2f, 0.8f, 31, -9, 115.0f), ride (4.5f, 0.7f, 41, -10),
                            slot ("CHINA", model (Kind::china, 105, 2.4f, 0.75f, 0.7f, 0.0f, 0.6f, 51), -11, 52) });
            k.reverb = room (1.3f, 1.0f, 0.4f, 10.0f, 0.6f, 150.0f);
            k.compressor = compressor (5, 120, 4, 0.2f);
            k.bus = bus (2.0f, 3.0f, 30.0f, 20000.0f, 1.3f);
            knobs (k, 0.0f, -3.0f, -11.0f, 0.5f, 0.2f);
            k.groove = groove (190.0, 50.0f,
                Rows { "x.....x.x.....x.", "....x.......x...", _, _, "o.o.o.o.o.o.o.o.", _, _, _, _, _, _, _ },
                Rows { "x.x.x.x.x.......", "....x...oooooooo", _, _, "o.o.o.o.........", _, _, _, _, _, _, _ });
            all.push_back (k);
        }

        //----------------------------------------------------------------------
        // Western swing and jazz: brushes, felt kick, riveted ride (Roy Lanham)
        {
            auto k = kit ("BRUSHES",
                          "Western swing and jazz: brushes on the snare (taps and sweeps), a feathered felt kick, small open toms, a riveted ride.",
                          0.7f,
                          { slot ("KICK", model (Kind::kick, 68, 0.35f, 0.3f, 0.2f, 0.5f, 0.2f), -3, 36),
                            slot ("BRUSH TAP", model (Kind::brush, 230, 0.3f, 0.5f, 0.3f, 0.4f, 0.7f), -2, 38),
                            slot ("BRUSH SWEEP", model (Kind::sweep, 230, 0.3f, 0.5f, 0.2f, 0.4f, 0.6f), -4, 40),
                            xstick (230, -7),
                            closedHat (0.14f, 0.45f, 22, -12), openHat (1.4f, 0.45f, 22, -14),
                            tom ("FLOOR TOM", 110, 0.9f, 0.5f, 0.45f, 0.3f, 0.5f, -4, 41),
                            tom ("TOM 2", 155, 0.8f, 0.5f, 0.45f, 0.3f, 0.5f, -4, 45),
                            tom ("TOM 1", 205, 0.7f, 0.52f, 0.45f, 0.3f, 0.5f, -4, 48),
                            crash (2.5f, 0.5f, 32, -12, 120.0f), ride (6.0f, 0.5f, 42, -9, 0.7f, "SIZZLE RIDE"),
                            tambourine (0.35f, -14) });
            k.reverb = room (1.2f, 1.0f, 0.55f, 10.0f, 0.5f, 150.0f);
            k.compressor = compressor (20, 250, 1.5f);
            k.bus = bus (0.0f, -1.0f, 40.0f, 15000.0f, 4.4f);
            knobs (k, -2.0f, 0.0f, -10.0f, 0.15f, 0.4f);
            // In 8th-note triplets (3 steps a beat): the ride's "spang-a-lang", sweeps on 1 and 3, taps on 2 and 4.
            k.groove = groove (150.0, 50.0f,
                Rows { "g..g..g..g..", "...o.....o..", "x.....x.....", _, "...o.....o..", _, _, _, _, _, "x..x.ox..x.o", _ },
                Rows { "g..g..g..g..", "...o..o.oo.o", "x...........", _, "...o.....o..", _, ".........o.o", "......o.o...", _, _, "x..x.ox.....", _ },
                12, 3);
            all.push_back (k);
        }

        //----------------------------------------------------------------------
        // Jack Antonoff, sombr: tight, processed indie pop
        {
            auto k = kit ("MODERN '24",
                          "Antonoff and sombr-style indie pop: a dead, subby kick, fat snare, claps, tight hats, parallel compression and a short plate.",
                          1.0f,
                          { slot ("KICK", model (Kind::kick, 50, 0.25f, 0.6f, 0.8f, 0.75f, 0.8f), 0, 36),
                            slot ("SNARE", model (Kind::snare, 200, 0.3f, 0.6f, 0.7f, 0.6f, 0.55f), -1, 38),
                            claps (0.5f, -5),
                            xstick (200),
                            closedHat (0.07f, 0.65f, 23, -10), openHat (1.2f, 0.65f, 23, -12),
                            tom ("FLOOR TOM", 85, 0.5f, 0.55f, 0.7f, 0.7f, 0.4f, -3, 41),
                            tom ("TOM 2", 112, 0.45f, 0.55f, 0.7f, 0.7f, 0.4f, -3, 45),
                            tom ("TOM 1", 145, 0.4f, 0.58f, 0.7f, 0.7f, 0.4f, -3, 48),
                            crash (2.8f, 0.6f, 33, -11), ride (4.0f, 0.6f, 43, -11),
                            tambourine (0.6f, -10) });
            k.reverb = room (1.0f, 0.9f, 0.4f, 20.0f, 0.4f, 250.0f);
            k.compressor = compressor (10, 80, 4, 0.5f);
            k.bus = bus (3.0f, 3.0f, 30.0f, 20000.0f, 1.3f);
            knobs (k, 0.0f, -8.0f, -12.0f, 0.6f, 0.2f);
            k.groove = groove (100.0, 50.0f,
                Rows { "x.......x.x.....", "....x.......x...", "....x.......x...", _, "x.o.x.o.x.o.x.o.", _, _, _, _, _, _, "..o...o...o...o." },
                Rows { "x.......x.x.....", "....x.......x...", "....x.......x.x.", _, "x.o.x.o.x.o.....", "..............o.", _, _, _, _, _, "..o...o...o...o." });
            all.push_back (k);
        }

        return all;
    }
}

const std::vector<KitDef>& kits()
{
    static const std::vector<KitDef> all = build();
    return all;
}

int analogKit()  { return 0; }
int defaultKit() { return 7; }      // ELECTRICAL '93: a natural kit to start from

} // namespace thoreau
