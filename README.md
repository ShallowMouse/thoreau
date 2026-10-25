# THOREAU

A drum machine for the Hawthorne tape machine, in the PEQUOD family. *"If a man does not keep pace with his companions, perhaps it is because he hears a different drummer."*

* **12 drums** - kick, snare, clap, rim, closed and open hat, three toms, crash, ride, cowbell - each an analogue-style synth voice (TUNE, DECAY, TONE, SNAP), or any WAV / AIFF / FLAC sample you load onto its pad.
* **16 patterns** (A-P), up to 64 steps, in 8ths, 8th triplets, 16ths, 16th triplets or 32nds. Soft / normal / accent per step, swing, mute and solo.
* **Song mode**: chain patterns, each repeated as often as you like, optionally looping.
* **Mixer**: level and pan per drum, master level with a soft limiter.
* **MIDI in**: any MIDI keyboard or pad controller plays the drums (General MIDI drum notes, changeable per drum). With **REC** on, what you play goes into the pattern on the nearest step.
* **Hawthorne Link**: the stereo mix - and optionally each drum on its own channel (2 + 12) - streamed to Hawthorne over the network, exactly like PEQUOD Host.
* **Tape sync with Hawthorne** (below).

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
