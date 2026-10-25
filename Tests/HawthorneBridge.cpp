#include "HawthorneBridge.h"
#include <Network/NetReceiver.h>

struct HawthorneSim::Impl
{
    hawthorne::NetReceiver rx;
};

HawthorneSim::HawthorneSim() : impl (std::make_unique<Impl>()) {}
HawthorneSim::~HawthorneSim() { stop(); }

bool HawthorneSim::start (juce::String& e)                 { return impl->rx.start (e); }
void HawthorneSim::stop()                                  { impl->rx.stop(); }
void HawthorneSim::addManualAddress (const juce::String& ip) { impl->rx.setManualAddresses ({ ip }); }
void HawthorneSim::connect (const juce::String& name)      { impl->rx.connect (0, name); }
void HawthorneSim::prepare (double sr)                     { impl->rx.prepare (sr); }
void HawthorneSim::process (int n)                         { impl->rx.process (n); }
const float* HawthorneSim::channel (int ch) const          { return impl->rx.getChannel (0, ch); }
bool HawthorneSim::isLive() const                          { return impl->rx.isLive (0); }
bool HawthorneSim::isSyncing() const                       { return impl->rx.isSyncing (0); }

void HawthorneSim::publishTape (double pos, double per, bool rolling, bool recording)
{
    using TS = hawthorne::net::TapeSync;
    impl->rx.publishTape (pos, per, (rolling ? TS::rolling : 0) | (recording ? TS::recording : 0) | TS::reelLoaded);
}

void HawthorneSim::setReel (const juce::String& name, double bpm, int bpb, double barOne)
{
    impl->rx.setReelSync (true, name, bpm, bpb, barOne);
}

bool HawthorneSim::popTempo (double& bpm, int& bpb, double& barOne)
{
    int slot = -1;
    hawthorne::net::TempoRequest r;
    if (! impl->rx.popTempoRequest (slot, r)) return false;
    bpm = r.bpm; bpb = r.beatsPerBar; barOne = r.barOne;
    return true;
}

juce::String HawthorneSim::describe() const
{
    const auto st = impl->rx.getStatus (0);
    return hawthorne::NetReceiver::describe (st.state) + ", " + juce::String (st.channels) + " ch, buffer "
         + juce::String (st.bufferMs, 1) + " ms, " + juce::String (st.dropouts) + " dropouts";
}
