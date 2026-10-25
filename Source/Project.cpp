#include "Project.h"

namespace thoreau
{

Project::Project()
{
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
void Project::resetToDefault()
{
    static const float tunes[kTracks]  = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    static const float decays[kTracks] = { 0.55f, 0.4f, 0.45f, 0.35f, 0.35f, 0.4f, 0.5f, 0.5f, 0.5f, 0.55f, 0.5f, 0.4f };
    static const float levels[kTracks] = { 0, -2, -4, -6, -7, -8, -4, -4, -4, -9, -10, -9 };
    static const float pans[kTracks]   = { 0, 0, 0.1f, -0.15f, 0.25f, 0.25f, -0.35f, 0.0f, 0.35f, -0.3f, 0.3f, -0.2f };

    for (int t = 0; t < kTracks; ++t)
    {
        auto& tr = tracks[(size_t) t];
        tr.tune = tunes[t];
        tr.decay = decays[t];
        tr.tone = 0.5f;
        tr.snap = 0.5f;
        tr.levelDb = levels[t];
        tr.pan = pans[t];
        tr.mute = false;
        tr.solo = false;
        tr.midiNote = defaultMidiNote (t);
        clearSample (t);
    }

    for (auto& p : patterns) { p.clear(); p.length = 16; p.stepsPerBeat = 4; }

    // A: a starter beat
    auto set = [this] (int pat, int track, std::initializer_list<int> steps, int vel = 2)
    {
        for (int s : steps) patterns[(size_t) pat].steps[(size_t) track][(size_t) s] = (uint8_t) vel;
    };
    set (0, 0, { 0, 8, 10 });
    set (0, 1, { 4, 12 });
    set (0, 4, { 0, 2, 4, 6, 8, 10, 12 });
    set (0, 4, { 2, 6, 10 }, 1);
    set (0, 5, { 14 });
    // B: fill
    patterns[1].copyFrom (patterns[0]);
    set (1, 1, { 13, 15 }, 1);
    set (1, 6, { 14 }); set (1, 7, { 12 }); set (1, 8, { 10 });
    set (1, 9, {});

    bpm = 120.0;
    beatsPerBar = 4;
    swing = 50.0f;
    masterDb = 0.0f;
    setSong ({ { 0, 3 }, { 1, 1 } });
    songLoops = false;
    changed();
}

//==============================================================================
std::unique_ptr<juce::XmlElement> Project::toXml() const
{
    auto root = std::make_unique<juce::XmlElement> ("ThoreauProject");
    root->setAttribute ("formatVersion", 1);
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
            tr.levelDb = (float) juce::jlimit (-60.0, 6.0, e->getDoubleAttribute ("level", 0.0));
            tr.pan   = (float) juce::jlimit (-1.0, 1.0, e->getDoubleAttribute ("pan", 0.0));
            tr.mute  = e->getBoolAttribute ("mute", false);
            tr.solo  = e->getBoolAttribute ("solo", false);
            tr.midiNote = juce::jlimit (0, 127, e->getIntAttribute ("note", defaultMidiNote (t)));
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
