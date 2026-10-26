# THOREAU

A drum machine for the Hawthorne tape machine, in the PEQUOD family. *"If a man does not keep pace with his companions, perhaps it is because he hears a different drummer."*

* **Acoustic kits**, Slate-style: close mics, overheads, a room and the kit's bus, each kit voiced after a sound you know (below). Every drum is physically modelled (struck heads, snare wires, cymbal wash), so every hit is a little different, like a real stroke.
* **Mix and match**: any pad can play its drum from another kit ("FROM ..." under the drum's name), or any WAV / AIFF / FLAC sample.
* **THOREAU ANALOGUE**, the original analogue-style synth kit (TUNE, DECAY, TONE, SNAP).
* **16 patterns** (A-P), up to 64 steps, in 8ths, 8th triplets, 16ths, 16th triplets or 32nds. Soft / normal / accent per step, swing, mute and solo.
* **Song mode**: chain patterns, each repeated as often as you like, optionally looping.
* **Mixer**: level and pan per drum, master level with a soft limiter.
* **MIDI in**: any MIDI keyboard or pad controller plays the drums (General MIDI drum notes, changeable per drum). With **REC** on, what you play goes into the pattern on the nearest step.
* **Hawthorne Link**: the stereo mix - and optionally every drum and mic on its own channel (18) - streamed to Hawthorne over the network, exactly like PEQUOD Host.
* **Tape sync with Hawthorne** (below).

## The kits

| Kit | After | Character |
|---|---|---|
| STUDIO TWO '66 | Ringo, Abbey Road | Ludwig kit with tea towels, close mics squashed hard, narrow; handclaps and tambourine |
| DARK SIDE '73 | Nick Mason | Deep, damped toms that still sing, fat snare, dry; Rototom |
| STADIUM '77 | Roger Taylor | Big open kit, ringing toms, bright cymbals, large room; gong |
| WALL OF SOUND '63 | Spector, Joe Meek | Mono, drowned in the echo chamber, pumping compression; claps, tambourine |
| FACTORY '79 | Martin Hannett, Stephen Morris | Dry and clipped, metallic digital room, snare into the delay; Synare |
| GATED '82 | Padgham and Lillywhite | Huge room, crushed and gated; single-headed toms, claps, cowbell |
| ELECTRICAL '93 | Steve Albini | Open, natural kit in a big live room; no compression |
| ROCKABILLY | Stray Cats, The Living End | Boomy kick, high ringing snare for rimshot backbeats, slapback |
| GARAGE '01 | The Strokes, Arctic Monkeys | Tight, dry, compressed, mid-forward, a little lo-fi |
| POP PUNK '99 | blink-182 | Cracking high snare, clicky kick, big bright cymbals, china |
| BRUSHES | Western swing, jazz | Brush taps and sweeps, feathered felt kick, riveted ride |
| MODERN '24 | Antonoff, sombr | Dead subby kick, fat snare, claps, tight hats, parallel compression |

The KIT panel's knobs: **CLOSE** (the close mics), **OVERHEADS**, **ROOM** (room mics and the kit's echo), **COMP** and **TAPE**. For acoustic drums the drum knobs are TUNE, RING, TONE and ATTACK (stick or beater). FILE > "Put ... starter beat in A" loads a groove in the kit's style.

With "each drum and mic on its own channel" on, Hawthorne gets 18 inputs: the mix (1-2), every drum's close mic (3-14), the overheads (15-16) and the room (17-18).

## Following Hawthorne's tape

Set the clock to **HAWTHORNE**. THOREAU then plays wherever the tape is: press play in Hawthorne and the drums come in at the right bar; locate, and they follow; change varispeed and the tempo follows the tape. Like a drum machine locked to tape, but sample-exact.

* Hawthorne tells THOREAU exactly where each of THOREAU's audio frames lands on tape (with Hawthorne's latency compensation and varispeed included), so hits are recorded precisely on the beat however long the network takes.
* The **tempo lives on the reel**. The first time THOREAU meets a reel with no tempo, it puts its own on it (bar 1 at the start of the tape). After that the reel's tempo is the tempo: change it in THOREAU and Hawthorne stores the new one with the reel. Hawthorne's counter shows BAR.BEAT for any reel with a tempo.
* **BAR 1 = TAPE POSITION** moves bar 1 to wherever the tape is parked; **< 1 ms / 1 ms >** nudge every beat.
* With **Follow Hawthorne's reel** on (SETTINGS > HAWTHORNE), THOREAU keeps a project per reel and switches when Hawthorne loads a different one.
* Monitored live, the drums are one interface round trip behind the tape, like any outboard gear; on tape they're exact.

Setup: in Hawthorne open **NETWORK** and connect THOREAU; patch its inputs to tracks. Both must run at the same sample rate.

Projects are saved automatically in `Documents/THOREAU`.

## Building

JUCE app, CMake. GitHub Actions builds the Windows installer on every push to `main` (see `Installer/README.md`).

```
cmake -B build -DTHOREAU_BUILD_TESTS=ON [-DHAWTHORNE_SOURCE=path/to/hawthorne]
cmake --build build --config Release
```

`Source/HawthorneLink/` is copied unchanged from Hawthorne's `Source/Network/` (NetProtocol.h, NetSender.h/.cpp) - copy it again if the protocol changes. With `HAWTHORNE_SOURCE` set, the tests also run THOREAU against Hawthorne's own receiver over real sockets.
