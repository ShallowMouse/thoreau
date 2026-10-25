#pragma once

#include <juce_core/juce_core.h>
#include <cstdint>
#include <cstring>

namespace hawthorne::net
{

//==============================================================================
/**
    Hawthorne Link: uncompressed multichannel audio between PCs on a LAN.

    Discovery   sender broadcasts  "HWNA1 ANN <id> <ctlPort> <channels> <rate> <session>\n<name>"
                to announcePort every second (and replies the same to "HWNA1 QRY",
                so senders on other subnets / VLANs can be added by IP).
    Subscribe   receiver sends "HWNA1 SUB <id> <receiverId>" to the sender's control port every
                second from its data socket; the sender streams to that address
                until it hasn't heard from it for 5 s ("HWNA1 BYE <id>" ends it at once).
    Audio       UDP datagrams, header + 24-bit little-endian interleaved PCM. Every
                frame carries its exact position in the sender's sample clock, so the
                receiver puts it in exactly the right place and can tell when the two
                machines share a word clock (then nothing is resampled: bit-perfect).
                Redundancy: each packet also carries the previous block, so any single
                lost packet costs nothing.
*/
constexpr int announcePort = 47201;
constexpr int controlPort  = 47202;     // sender (falls back to any free port)
constexpr int dataPort     = 47203;     // receiver (falls back to any free port)
constexpr int maxChannels  = 64;
constexpr int maxDatagram  = 1472;      // fits a standard 1500-byte Ethernet MTU unfragmented
constexpr int subscriptionTimeoutMs = 5000;

constexpr uint32_t magic = 0x414e5748;  // "HWNA" little-endian
constexpr uint8_t  version = 1;

#pragma pack(push, 1)
struct PacketHeader
{
    uint32_t magic;
    uint8_t  version;
    uint8_t  flags;          // bit 0: block B (previous block) present
    uint16_t channels;
    uint32_t senderId;       // random per sender app run
    uint32_t session;        // random per audio-device start: frame numbers restart
    uint32_t sampleRate;
    uint16_t frames;         // frames per block
    uint16_t latency;        // sender's input latency + audio buffer, samples (information)
    uint64_t firstFrame;     // block A
    uint64_t prevFirstFrame; // block B
};
#pragma pack(pop)
static_assert (sizeof (PacketHeader) == 40, "header layout");

constexpr int headerSize = (int) sizeof (PacketHeader);

/** Frames per block that keep a datagram within maxDatagram. */
inline int framesPerPacket (int channels, bool redundant) noexcept
{
    const int bytesPerFrame = juce::jmax (1, channels) * 3 * (redundant ? 2 : 1);
    return juce::jlimit (1, 1024, (maxDatagram - headerSize) / bytesPerFrame);
}

//==============================================================================
// 24-bit PCM. float <-> int24 is exact for anything that came from a 24-bit converter.
inline void writeInt24 (uint8_t* d, float v) noexcept
{
    const double s = juce::jlimit (-8388608.0, 8388607.0, std::round ((double) v * 8388608.0));
    const auto i = (int32_t) s;
    d[0] = (uint8_t) (i & 0xff);
    d[1] = (uint8_t) ((i >> 8) & 0xff);
    d[2] = (uint8_t) ((i >> 16) & 0xff);
}

inline float readInt24 (const uint8_t* d) noexcept
{
    int32_t i = (int32_t) d[0] | ((int32_t) d[1] << 8) | ((int32_t) d[2] << 16);
    if (i & 0x800000) i |= ~0xffffff;
    return (float) i / 8388608.0f;
}

//==============================================================================
// Control messages
struct Announce
{
    uint32_t senderId = 0, session = 0;
    int controlPort = 0, channels = 0;
    double sampleRate = 0.0;
    juce::String name;
};

inline juce::String hex (uint32_t v) { return juce::String::toHexString ((juce::int64) v).paddedLeft ('0', 8); }
inline uint32_t fromHex (const juce::String& s) { return (uint32_t) s.getHexValue64(); }

inline juce::String makeAnnounce (const Announce& a)
{
    return "HWNA1 ANN " + hex (a.senderId) + " " + juce::String (a.controlPort) + " " + juce::String (a.channels) + " "
         + juce::String ((int) a.sampleRate) + " " + hex (a.session) + "\n" + a.name;
}

inline bool parseAnnounce (const juce::String& msg, Announce& a)
{
    if (! msg.startsWith ("HWNA1 ANN "))
        return false;
    auto first = msg.upToFirstOccurrenceOf ("\n", false, false);
    auto t = juce::StringArray::fromTokens (first, " ", {});
    if (t.size() < 7) return false;
    a.senderId    = fromHex (t[2]);
    a.controlPort = t[3].getIntValue();
    a.channels    = juce::jlimit (0, maxChannels, t[4].getIntValue());
    a.sampleRate  = t[5].getDoubleValue();
    a.session     = fromHex (t[6]);
    a.name        = msg.fromFirstOccurrenceOf ("\n", false, false).trim();
    return a.senderId != 0 && a.controlPort > 0;
}

/** Bigger kernel socket buffers than the OS default (Windows' is small). In NetSender.cpp. */
void setSocketBuffers (int socketHandle, int receiveBytes, int sendBytes);

inline juce::String makeSubscribe (uint32_t senderId, uint32_t receiverId) { return "HWNA1 SUB " + hex (senderId) + " " + hex (receiverId); }
inline juce::String makeBye (uint32_t senderId)         { return "HWNA1 BYE " + hex (senderId); }
inline juce::String makeQuery()                         { return "HWNA1 QRY"; }

//==============================================================================
/**
    Hawthorne Sync: tape position and tempo, so a sender (a drum machine) can play
    in time with the tape. Senders that don't know these messages just ignore them.

    POS   receiver -> sender's control port, ~50 times a second while the sender's
          audio is live in Hawthorne, and at once when the transport changes:
            "HWNA1 POS <senderId> <session> <senderFrame> <tapePos> <tapePerFrame> <flags>
                       <bpm> <beatsPerBar> <barOne> <tempoRev>\n<reel name>"
          The sender's frame <senderFrame> (its own sample clock, in <session>) lands on
          tape at <tapePos> (reel samples) - exactly where it is recorded, with
          Hawthorne's latency compensation included - and each further sender frame
          is <tapePerFrame> further along the tape (tape speed / clock drift; 0 when
          stopped). Because the sender's frames are stamped, network delay doesn't
          matter: a sender that renders frame F for tape position
              tapePos + (F - senderFrame) * tapePerFrame
          is recorded sample-exact.
          flags: bit 0 rolling, bit 1 recording, bit 2 a reel is loaded.
          Tempo is stored with the reel: <bpm> 0 = the reel has no tempo yet.
          <barOne> is the tape position (reel samples) of the downbeat of bar 1.
          <tempoRev> goes up every time the reel's tempo changes.

    TMP   sender -> receiver: "HWNA1 TMP <senderId> <bpm> <beatsPerBar> <barOne>"
          sets the reel's tempo (resend until POS shows it).
*/
struct TapeSync
{
    uint32_t senderId = 0, session = 0;
    double senderFrame = 0.0, tapePos = 0.0, tapePerFrame = 0.0;
    int flags = 0;
    double bpm = 0.0;
    int beatsPerBar = 4;
    double barOne = 0.0;
    int tempoRev = 0;
    juce::String reelName;

    enum { rolling = 1, recording = 2, reelLoaded = 4 };
    bool isRolling() const noexcept   { return (flags & rolling) != 0; }
    bool isRecording() const noexcept { return (flags & recording) != 0; }
    bool hasReel() const noexcept     { return (flags & reelLoaded) != 0; }
};

inline juce::String makePos (const TapeSync& s)
{
    return "HWNA1 POS " + hex (s.senderId) + " " + hex (s.session) + " "
         + juce::String (s.senderFrame, 4) + " " + juce::String (s.tapePos, 4) + " "
         + juce::String (s.tapePerFrame, 12) + " " + juce::String (s.flags) + " "
         + juce::String (s.bpm, 4) + " " + juce::String (s.beatsPerBar) + " "
         + juce::String (s.barOne, 4) + " " + juce::String (s.tempoRev) + "\n" + s.reelName;
}

inline bool parsePos (const juce::String& msg, TapeSync& s)
{
    if (! msg.startsWith ("HWNA1 POS "))
        return false;
    auto t = juce::StringArray::fromTokens (msg.upToFirstOccurrenceOf ("\n", false, false), " ", {});
    if (t.size() < 12) return false;
    s.senderId     = fromHex (t[2]);
    s.session      = fromHex (t[3]);
    s.senderFrame  = t[4].getDoubleValue();
    s.tapePos      = t[5].getDoubleValue();
    s.tapePerFrame = t[6].getDoubleValue();
    s.flags        = t[7].getIntValue();
    s.bpm          = t[8].getDoubleValue();
    s.beatsPerBar  = juce::jlimit (1, 32, t[9].getIntValue());
    s.barOne       = t[10].getDoubleValue();
    s.tempoRev     = t[11].getIntValue();
    s.reelName     = msg.fromFirstOccurrenceOf ("\n", false, false).trim();
    return s.senderId != 0;
}

struct TempoRequest { uint32_t senderId = 0; double bpm = 0.0; int beatsPerBar = 4; double barOne = 0.0; };

inline juce::String makeTempo (const TempoRequest& r)
{
    return "HWNA1 TMP " + hex (r.senderId) + " " + juce::String (r.bpm, 4) + " "
         + juce::String (r.beatsPerBar) + " " + juce::String (r.barOne, 4);
}

inline bool parseTempo (const juce::String& msg, TempoRequest& r)
{
    if (! msg.startsWith ("HWNA1 TMP "))
        return false;
    auto t = juce::StringArray::fromTokens (msg.trim(), " ", {});
    if (t.size() < 6) return false;
    r.senderId    = fromHex (t[2]);
    r.bpm         = t[3].getDoubleValue();
    r.beatsPerBar = t[4].getIntValue();
    r.barOne      = t[5].getDoubleValue();
    return r.senderId != 0 && r.bpm >= 20.0 && r.bpm <= 400.0 && r.beatsPerBar >= 1 && r.beatsPerBar <= 32;
}

} // namespace hawthorne::net
