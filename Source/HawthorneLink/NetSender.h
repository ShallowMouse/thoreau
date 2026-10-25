#pragma once

#include "NetProtocol.h"
#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <vector>

namespace hawthorne
{

//==============================================================================
/** Turns a stream of audio into Hawthorne Link datagrams. Audio thread safe
    (no allocation after prepare). */
class Packetizer
{
public:
    void prepare (uint32_t senderId, uint32_t session, double sampleRate, int channels, bool redundant, int latencySamples = 0);

    /** Calls emit (data, size) for every completed datagram. */
    template <typename Emit>
    void push (const float* const* in, int numChannels, int n, Emit&& emit) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            uint8_t* frame = blockA.data() + (size_t) filled * (size_t) channels * 3;
            for (int c = 0; c < channels; ++c)
                net::writeInt24 (frame + c * 3, (c < numChannels && in[c] != nullptr) ? in[c][i] : 0.0f);

            if (++filled == fpp)
            {
                finishPacket();
                emit (datagram.data(), datagramSize);
            }
        }
    }

    int getChannels() const noexcept        { return channels; }
    int getFramesPerPacket() const noexcept { return fpp; }
    juce::uint64 getFramePosition() const noexcept { return nextFrame + (juce::uint64) filled; }

private:
    void finishPacket() noexcept;

    net::PacketHeader header {};
    int channels = 0, fpp = 1, filled = 0, datagramSize = 0;
    bool redundant = true, havePrev = false;
    juce::uint64 nextFrame = 0;
    std::vector<uint8_t> blockA, blockB, datagram;
};

//==============================================================================
/**
    Hawthorne Link sender: announces itself, accepts subscriptions and streams
    whatever is pushed from the audio callback to every subscriber.
*/
class NetSender
{
public:
    NetSender();
    ~NetSender();

    bool start (const juce::String& name, juce::String& error);
    void stop();
    void setName (const juce::String&);
    juce::String getName() const;
    void setRedundancy (bool on) noexcept { redundancy = on; }
    bool getRedundancy() const noexcept   { return redundancy.load(); }

    /** Before the audio starts (device about to start): begins a new session. */
    void prepare (double sampleRate, int channels, int latencySamples = 0);
    /** Audio thread. */
    void push (const float* const* in, int numChannels, int n) noexcept;

    //==========================================================================
    // Hawthorne Sync (see NetProtocol.h)

    /** The latest tape position Hawthorne sent (numbers only; safe on the audio thread,
        lock-free). receivedMs = juce::Time::getMillisecondCounter() when it arrived. */
    struct TapeClock
    {
        uint32_t session = 0;
        double senderFrame = 0.0, tapePos = 0.0, tapePerFrame = 0.0;
        int flags = 0;
        double bpm = 0.0;
        int beatsPerBar = 4;
        double barOne = 0.0;
        int tempoRev = 0;
        juce::uint32 receivedMs = 0;
        juce::uint64 count = 0;          // messages received so far
    };
    bool getTapeClock (TapeClock&) const noexcept;
    /** Message thread: name of the reel on Hawthorne's deck (empty = none / not synced). */
    juce::String getReelName() const;
    /** Message thread: asks Hawthorne to set the reel's tempo (sent with the next packets). */
    void sendTempo (double bpm, int beatsPerBar, double barOne);
    /** For the TapeClock: the session our current frames belong to. */
    uint32_t getSession() const noexcept { return session.load(); }
    /** Audio thread: the frame number the next pushed sample will have. */
    juce::uint64 getNextFrame() const noexcept { return packetizer.getFramePosition(); }
    /** Tests: feed a control message as if it came from the network. */
    void handleControlForTest (const juce::String& msg) { handleControl (msg, "127.0.0.1", 0); }

    struct Subscriber { juce::String address; int port = 0; juce::uint32 lastSeenMs = 0; uint32_t receiverId = 0; };
    std::vector<Subscriber> getSubscribers() const;
    juce::int64 getPacketsSent() const noexcept  { return packetsSent.load(); }
    juce::int64 getQueueOverruns() const noexcept { return overruns.load(); }
    uint32_t getSenderId() const noexcept         { return senderId; }
    int getControlPort() const noexcept           { return boundPort.load(); }

private:
    void run();
    void handleControl (const juce::String& msg, const juce::String& from, int port);
    void announce (const juce::String& address, int port);

    static constexpr int queueSize = 1024;
    struct QueuedPacket { int size = 0; std::array<uint8_t, net::maxDatagram> data; };
    std::unique_ptr<QueuedPacket[]> queue;
    std::atomic<int> queueWrite { 0 }, queueRead { 0 };
    juce::WaitableEvent wake;

    Packetizer packetizer;
    const uint32_t senderId;
    std::atomic<uint32_t> session { 0 };
    std::atomic<int> channels { 0 };
    std::atomic<double> sampleRate { 0.0 };
    std::atomic<bool> redundancy { true }, prepared { false };

    mutable juce::CriticalSection lock;
    juce::String name;
    std::vector<Subscriber> subscribers;

    std::unique_ptr<juce::DatagramSocket> socket;
    std::unique_ptr<juce::Thread> thread;
    std::atomic<bool> running { false };
    std::atomic<int> boundPort { 0 };
    std::atomic<juce::int64> packetsSent { 0 }, overruns { 0 };

    // Sync: seqlock for the tape clock (written by the TX thread)
    std::atomic<uint32_t> clkSeq { 0 };
    std::atomic<uint32_t> clkSession { 0 };
    std::atomic<double> clkFrame { 0.0 }, clkTape { 0.0 }, clkPer { 0.0 }, clkBpm { 0.0 }, clkBarOne { 0.0 };
    std::atomic<int> clkFlags { 0 }, clkBpb { 4 }, clkRev { 0 };
    std::atomic<juce::uint32> clkMs { 0 };
    std::atomic<juce::uint64> clkCount { 0 };
    juce::String reelName, pendingTempo;      // lock

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NetSender)
};

} // namespace hawthorne
