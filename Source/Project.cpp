#include "Project.h"

namespace thoreau
{

Project::Project()
{
    kits();                 // builds the kit table here, never on the audio thread
    for (auto& a : songPattern) a = 0;
    for (auto& a : songRepeats) a = 1;
    resetToDefault();
}

std::vector<SongEntry> Project::getSong() const
{
    std::vector<SongEntry> s;
    const int n = juce::jlimit (0, kSongMax, songLength.load());
    for (int i = 0; i < n; ++i)
        s.push_back ({ songPattern[(size_t) i].load(), songRepeats[(size_t) i].load() });
    return s;
}

void Project::setSong (const std::vector<SongEntry>& s)
{
    const int n = juce::jmin ((int) s.size(), kSongMax);
    // Shrink first, fill, then grow: the audio thread never reads a half-written entry past the end.
    songLength = juce::jmin (songLength.load(), n);
    for (int i = 0; i < n; ++i)
    {
        songPattern[(size_t) i] = juce::jlimit (0, kPatterns - 1, s[(size_t) i].pattern);
        songRepeats[(size_t) i] = juce::jlimit (1, 99, s[(size_t) i].repeats);
    }
    songLength = n;
    changed();
}

double Project::songBeats() const noexcept
{
    double b = 0.0;
    const int n = juce::jlimit (0, kSongMax, songLength.load());
    for (int i = 0; i < n; ++i)
        b += patterns[(size_t) juce::jlimit (0, kPatterns - 1, songPattern[(size_t) i].load())].beats() * songRepeats[(size_t) i].load();
    return b;
}

//==============================================================================
bool Project::loadSample (int track, const juce::File& f, juce::String& error)
{
    auto s = Sample::load (f, error);
    if (s == nullptr) return false;
    auto& slot = tracks[(size_t) track].sample;
    slot.store (s.get());
    if (ownedSamples[(size_t) track] != nullptr)
        graveyard.push_back ({ std::move (ownedSamples[(size_t) track]), juce::Time::getMillisecondCounter() });
    ownedSamples[(size_t) track] = std::move (s);
    changed();
    return true;
}

void Project::clearSample (int track)
{
    tracks[(size_t) track].sample.store (nullptr);
    if (ownedSamples[(size_t) track] != nullptr)
        graveyard.push_back ({ std::move (ownedSamples[(size_t) track]), juce::Time::getMillisecondCounter() });
    changed();
}

juce::File Project::getSampleFile (int track) const
{
    auto& s = ownedSamples[(size_t) track];
    return s != nullptr ? s->file : juce::File();
}

void Project::collectGarbage()
{
    // A voice can hold a sample for at most 30 s (the longest sample allowed) plus a block.
    const auto now = juce::Time::getMillisecondCounter();
    graveyard.erase (std::remove_if (graveyard.begin(), graveyard.end(),
                                     [now] (const Dead& d) { return now - d.at > 35000; }),
                     graveyard.end());
}

//==============================================================================
void Project::setKit (int k)
{
    k = juce::jlimit (0, (int) kits().size() - 1, k);
    const auto& def = kitAt (k);
    kit = k;
    for (int t = 0; t < kTracks; ++t)
    {
        auto& tr = tracks[(size_t) t];
        const auto& s = def.slots[(size_t) t];
        tr.pieceKit = -1;
        tr.tune = 0.0f; tr.decay = 0.5f; tr.tone = 0.5f; tr.snap = 0.5f;
        tr.levelDb = s.levelDb;
        tr.pan = s.pan;
        tr.midiNote = s.midi;
    }
    closeDb = def.closeDb; ohDb = def.ohDb; roomDb = def.roomDb; compAmount = def.comp; tapeAmount = def.tape;
    changed();
}

void Project::loadGroove (int k, int a, int b)
{
    const auto& g = kitAt (k).groove;
    auto fill = [this, &g] (int p, const std::array<const char*, kTracks>& rows)
    {
        auto& pt = patterns[(size_t) juce::jlimit (0, kPatterns - 1, p)];
        pt.clear();
        pt.length = juce::jlimit (1, kMaxSteps, g.length);
        pt.stepsPerBeat = g.stepsPerBeat;
        for (int t = 0; t < kTracks; ++t)
            if (const char* r = rows[(size_t) t])
                for (int s = 0; r[s] != 0 && s < kMaxSteps; ++s)
                    pt.steps[(size_t) t][(size_t) s] = (uint8_t) (r[s] == 'x' ? 3 : r[s] == 'o' ? 2 : r[s] == 'g' ? 1 : 0);
    };
    fill (a, g.beat);
    fill (b, g.fill);
    bpm = g.bpm;
    swing = g.swing;
    changed();
}

void Project::resetToDefault()
{
    for (int t = 0; t < kTracks; ++t)
    {
        tracks[(size_t) t].mute = false;
        tracks[(size_t) t].solo = false;
        clearSample (t);
    }
    for (auto& p : patterns) { p.clear(); p.length = 16; p.stepsPerBeat = 4; }
    setKit (defaultKit());
    loadGroove (defaultKit(), 0, 1);
    beatsPerBar = 4;
    masterDb = 0.0f;
    setSong ({ { 0, 3 }, { 1, 1 } });
    songLoops = false;
    changed();
}

//==============================================================================
std::unique_ptr<juce::XmlElement> Project::toXml() const
{
    auto root = std::make_unique<juce::XmlElement> ("ThoreauProject");
    root->setAttribute ("formatVersion", 2);
    root->setAttribute ("kit", kitAt (kit.load()).name);
    root->setAttribute ("closeDb", (double) closeDb.load());
    root->setAttribute ("ohDb", (double) ohDb.load());
    root->setAttribute ("roomDb", (double) roomDb.load());
    root->setAttribute ("comp", (double) compAmount.load());
    root->setAttribute ("tape", (double) tapeAmount.load());
    root->setAttribute ("bpm", bpm.load());
    root->setAttribute ("beatsPerBar", beatsPerBar.load());
    root->setAttribute ("swing", (double) swing.load());
    root->setAttribute ("masterDb", (double) masterDb.load());
    root->setAttribute ("songLoops", songLoops.load());

    auto* kit = root->createNewChildElement ("Kit");
    for (int t = 0; t < kTracks; ++t)
    {
        auto& tr = tracks[(size_t) t];
        auto* e = kit->createNewChildElement ("Track");
        e->setAttribute ("index", t);
        e->setAttribute ("tune", (double) tr.tune.load());
        e->setAttribute ("decay", (double) tr.decay.load());
        e->setAttribute ("tone", (double) tr.tone.load());
        e->setAttribute ("snap", (double) tr.snap.load());
        e->setAttribute ("level", (double) tr.levelDb.load());
        e->setAttribute ("pan", (double) tr.pan.load());
        e->setAttribute ("mute", tr.mute.load());
        e->setAttribute ("solo", tr.solo.load());
        e->setAttribute ("note", tr.midiNote.load());
        if (tr.pieceKit.load() >= 0) e->setAttribute ("from", kitAt (tr.pieceKit.load()).name);
        const auto f = getSampleFile (t);
        if (f != juce::File()) e->setAttribute ("sample", f.getFullPathName());
    }

    auto* pats = root->createNewChildElement ("Patterns");
    for (int p = 0; p < kPatterns; ++p)
    {
        auto& pt = patterns[(size_t) p];
        auto* e = pats->createNewChildElement ("Pattern");
        e->setAttribute ("index", p);
        e->setAttribute ("length", pt.length.load());
        e->setAttribute ("stepsPerBeat", pt.stepsPerBeat.load());
        for (int t = 0; t < kTracks; ++t)
        {
            juce::String row;
            for (int s = 0; s < pt.length.load(); ++s)
                row << juce::String (pt.steps[(size_t) t][(size_t) s].load());
            if (row.containsAnyOf ("123"))
            {
                auto* r = e->createNewChildElement ("Row");
                r->setAttribute ("track", t);
                r->setAttribute ("steps", row);
            }
        }
    }

    auto* song = root->createNewChildElement ("Song");
    for (auto& s : getSong())
    {
        auto* e = song->createNewChildElement ("Entry");
        e->setAttribute ("pattern", s.pattern);
        e->setAttribute ("repeats", s.repeats);
    }
    return root;
}

bool Project::fromXml (const juce::XmlElement& root, juce::String& error)
{
    if (! root.hasTagName ("ThoreauProject"))
    {
        error = "Not a THOREAU project.";
        return false;
    }

    resetToDefault();
    for (auto& p : patterns) p.clear();

    // the kit (by name; projects from before kits were added use THOREAU ANALOGUE)
    auto findKit = [] (const juce::String& name, int fallback)
    {
        for (int i = 0; i < (int) kits().size(); ++i)
            if (kits()[(size_t) i].name == name) return i;
        return fallback;
    };
    setKit (root.hasAttribute ("kit") ? findKit (root.getStringAttribute ("kit"), defaultKit()) : analogKit());
    closeDb    = (float) juce::jlimit (-60.0, 12.0, root.getDoubleAttribute ("closeDb", closeDb.load()));
    ohDb       = (float) juce::jlimit (-60.0, 12.0, root.getDoubleAttribute ("ohDb", ohDb.load()));
    roomDb     = (float) juce::jlimit (-60.0, 12.0, root.getDoubleAttribute ("roomDb", roomDb.load()));
    compAmount = (float) juce::jlimit (0.0, 1.0, root.getDoubleAttribute ("comp", compAmount.load()));
    tapeAmount = (float) juce::jlimit (0.0, 1.0, root.getDoubleAttribute ("tape", tapeAmount.load()));

    bpm = juce::jlimit (20.0, 400.0, root.getDoubleAttribute ("bpm", 120.0));
    beatsPerBar = juce::jlimit (1, 32, root.getIntAttribute ("beatsPerBar", 4));
    swing = (float) juce::jlimit (50.0, 75.0, root.getDoubleAttribute ("swing", 50.0));
    masterDb = (float) juce::jlimit (-60.0, 6.0, root.getDoubleAttribute ("masterDb", 0.0));
    songLoops = root.getBoolAttribute ("songLoops", false);

    juce::StringArray missing;
    if (auto* kit = root.getChildByName ("Kit"))
        for (auto* e : kit->getChildWithTagNameIterator ("Track"))
        {
            const int t = e->getIntAttribute ("index", -1);
            if (! juce::isPositiveAndBelow (t, kTracks)) continue;
            auto& tr = tracks[(size_t) t];
            tr.tune  = (float) juce::jlimit (-12.0, 12.0, e->getDoubleAttribute ("tune", 0.0));
            tr.decay = (float) juce::jlimit (0.0, 1.0, e->getDoubleAttribute ("decay", 0.5));
            tr.tone  = (float) juce::jlimit (0.0, 1.0, e->getDoubleAttribute ("tone", 0.5));
            tr.snap  = (float) juce::jlimit (0.0, 1.0, e->getDoubleAttribute ("snap", 0.5));
            tr.levelDb = (float) juce::jlimit (-60.0, 6.0, e->getDoubleAttribute ("level", tr.levelDb.load()));
            tr.pan   = (float) juce::jlimit (-1.0, 1.0, e->getDoubleAttribute ("pan", tr.pan.load()));
            tr.mute  = e->getBoolAttribute ("mute", false);
            tr.solo  = e->getBoolAttribute ("solo", false);
            tr.midiNote = juce::jlimit (0, 127, e->getIntAttribute ("note", tr.midiNote.load()));
            tr.pieceKit = e->hasAttribute ("from") ? findKit (e->getStringAttribute ("from"), -1) : -1;
            const auto path = e->getStringAttribute ("sample");
            if (path.isNotEmpty())
            {
                juce::String err;
                if (! loadSample (t, juce::File (path), err))
                    missing.add (instrumentName (t) + ": " + juce::File (path).getFileName());
            }
        }

    if (auto* pats = root.getChildByName ("Patterns"))
        for (auto* e : pats->getChildWithTagNameIterator ("Pattern"))
        {
            const int p = e->getIntAttribute ("index", -1);
            if (! juce::isPositiveAndBelow (p, kPatterns)) continue;
            auto& pt = patterns[(size_t) p];
            pt.length = juce::jlimit (1, kMaxSteps, e->getIntAttribute ("length", 16));
            const int spb = e->getIntAttribute ("stepsPerBeat", 4);
            pt.stepsPerBeat = (spb == 2 || spb == 3 || spb == 4 || spb == 6 || spb == 8) ? spb : 4;
            for (auto* r : e->getChildWithTagNameIterator ("Row"))
            {
                const int t = r->getIntAttribute ("track", -1);
                if (! juce::isPositiveAndBelow (t, kTracks)) continue;
                const auto row = r->getStringAttribute ("steps");
                for (int s = 0; s < juce::jmin (row.length(), kMaxSteps); ++s)
                    pt.steps[(size_t) t][(size_t) s] = (uint8_t) juce::jlimit (0, 3, (int) (row[s] - '0'));
            }
        }

    std::vector<SongEntry> song;
    if (auto* s = root.getChildByName ("Song"))
        for (auto* e : s->getChildWithTagNameIterator ("Entry"))
            song.push_back ({ juce::jlimit (0, kPatterns - 1, e->getIntAttribute ("pattern", 0)),
                              juce::jlimit (1, 99, e->getIntAttribute ("repeats", 1)) });
    setSong (song);

    if (! missing.isEmpty())
        error = "Some samples couldn't be found, so those drums use THOREAU's own sound:\n" + missing.joinIntoString ("\n");
    changed();
    return true;
}

bool Project::save (const juce::File& file, juce::String& error) const
{
    auto xml = toXml();
    file.getParentDirectory().createDirectory();
    juce::TemporaryFile tmp (file);
    if (! xml->writeTo (tmp.getFile()) || ! tmp.overwriteTargetFileWithTemporary())
    {
        error = "Couldn't save " + file.getFullPathName();
        return false;
    }
    return true;
}

bool Project::load (const juce::File& file, juce::String& error)
{
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr)
    {
        error = "Couldn't read " + file.getFileName();
        return false;
    }
    return fromXml (*xml, error);
}

} // namespace thoreau
