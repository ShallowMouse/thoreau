#include "StepGrid.h"

namespace thoreau
{

using namespace Pequod;

StepGrid::StepGrid (DrumEngine& e) : engine (e)
{
    setTooltip ("Click a step to turn it on or off. Right-click (or shift-click) for soft / normal / accent. Drag to paint.");
}

juce::Rectangle<int> StepGrid::rowArea (int t) const
{
    const float h = (float) getHeight() / (float) kTracks;
    return { 0, (int) std::round (t * h), getWidth(), (int) std::round ((t + 1) * h) - (int) std::round (t * h) };
}

juce::Rectangle<int> StepGrid::padArea (int t) const  { return rowArea (t).withWidth (padWidth).reduced (2, 2); }
juce::Rectangle<int> StepGrid::muteArea (int t) const { return rowArea (t).withX (padWidth).withWidth (msWidth / 2).reduced (2, 3); }
juce::Rectangle<int> StepGrid::soloArea (int t) const { return rowArea (t).withX (padWidth + msWidth / 2).withWidth (msWidth / 2).reduced (2, 3); }

juce::Rectangle<int> StepGrid::stepArea (int t, int s) const
{
    const auto& pt = engine.project.patterns[(size_t) pattern];
    const int len = juce::jmax (1, pt.length.load());
    const int spb = juce::jmax (1, pt.stepsPerBeat.load());
    const int x0 = padWidth + msWidth + 6, x1 = getWidth() - meterWidth - 8;
    const int beats = (len + spb - 1) / spb;
    const float gap = 6.0f;                                   // between beats
    const float cell = ((float) (x1 - x0) - gap * (float) (beats - 1)) / (float) len;
    const float x = (float) x0 + cell * (float) s + gap * (float) (s / spb);
    auto r = rowArea (t);
    return juce::Rectangle<float> (x, (float) r.getY(), cell, (float) r.getHeight()).reduced (1.5f, 3.0f).toNearestInt();
}

int StepGrid::stepAt (juce::Point<int> p, int& track) const
{
    track = juce::jlimit (0, kTracks - 1, (int) ((float) p.y / ((float) getHeight() / (float) kTracks)));
    const int len = engine.project.patterns[(size_t) pattern].length.load();
    for (int s = 0; s < len; ++s)
        if (stepArea (track, s).expanded (2, 0).contains (p.x, stepArea (track, s).getCentreY()))
            return s;
    return -1;
}

void StepGrid::setStep (int t, int s, int v)
{
    auto& cell = engine.project.patterns[(size_t) pattern].steps[(size_t) t][(size_t) s];
    if (cell.load() == v) return;
    cell = (uint8_t) v;
    engine.project.changed();
    if (onEdited) onEdited();
    repaint (stepArea (t, s).expanded (2));
}

//==============================================================================
void StepGrid::mouseDown (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    for (int t = 0; t < kTracks; ++t)
    {
        if (padArea (t).contains (p))
        {
            const float vel = juce::jlimit (0.3f, 1.0f, 0.4f + 0.6f * (float) (p.x - padArea (t).getX()) / (float) padArea (t).getWidth());
            engine.audition (t, e.mods.isShiftDown() ? 1.0f : juce::jmax (0.78f, vel));
            selected = t;
            if (onTrackSelected) onTrackSelected (t);
            repaint();
            return;
        }
        auto& tr = engine.project.tracks[(size_t) t];
        if (muteArea (t).contains (p)) { tr.mute = ! tr.mute.load(); engine.project.changed(); if (onEdited) onEdited(); repaint(); return; }
        if (soloArea (t).contains (p)) { tr.solo = ! tr.solo.load(); engine.project.changed(); if (onEdited) onEdited(); repaint(); return; }
    }

    int t = 0;
    const int s = stepAt (p, t);
    if (s < 0) return;
    const int cur = engine.project.patterns[(size_t) pattern].steps[(size_t) t][(size_t) s].load();
    if (e.mods.isPopupMenu() || e.mods.isShiftDown())
        paintValue = cur == 0 ? 1 : cur == 1 ? 2 : cur == 2 ? 3 : 0;
    else
        paintValue = cur == 0 ? 2 : 0;
    paintTrack = t;
    lastPainted = s;
    setStep (t, s, paintValue);
    if (selected != t) { selected = t; if (onTrackSelected) onTrackSelected (t); repaint(); }
}

void StepGrid::mouseDrag (const juce::MouseEvent& e)
{
    if (paintValue < 0) return;
    int t = 0;
    const int s = stepAt ({ e.x, rowArea (paintTrack).getCentreY() }, t);
    if (s >= 0 && s != lastPainted)
    {
        lastPainted = s;
        setStep (paintTrack, s, paintValue);
    }
}

void StepGrid::mouseUp (const juce::MouseEvent&)
{
    paintValue = -1;
    paintTrack = -1;
}

void StepGrid::mouseMove (const juce::MouseEvent& e)
{
    juce::ignoreUnused (e);
}

//==============================================================================
void StepGrid::tick()
{
    const int ps = engine.isRolling() && engine.getPlayingPattern() == pattern ? engine.getPlayingStep() : -1;
    bool dirty = ps != playStep;
    playStep = ps;
    for (int t = 0; t < kTracks; ++t)
    {
        const float h = engine.popTrackHit (t);
        const float pk = engine.popTrackPeak (t);
        const float nh = juce::jmax (h, hit[(size_t) t] * 0.82f);
        const float nl = juce::jmax (juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (pk, -48.0f) + 48.0f) / 48.0f), level[(size_t) t] * 0.88f);
        if (std::abs (nh - hit[(size_t) t]) > 0.01f || std::abs (nl - level[(size_t) t]) > 0.01f) dirty = true;
        hit[(size_t) t] = nh < 0.02f ? 0.0f : nh;
        level[(size_t) t] = nl < 0.01f ? 0.0f : nl;
    }
    if (dirty) repaint();
}

void StepGrid::paint (juce::Graphics& g)
{
    const auto& pt = engine.project.patterns[(size_t) pattern];
    const int len = juce::jmax (1, pt.length.load());
    const int spb = juce::jmax (1, pt.stepsPerBeat.load());
    bool anySolo = false;
    for (auto& tr : engine.project.tracks) anySolo = anySolo || tr.solo.load();

    for (int t = 0; t < kTracks; ++t)
    {
        const auto row = rowArea (t);
        const auto& tr = engine.project.tracks[(size_t) t];
        const bool sel = t == selected;
        const bool silent = tr.mute.load() || (anySolo && ! tr.solo.load());

        if (sel)
        {
            g.setColour (Pequod::Colours::panelHi.withAlpha (0.6f));
            g.fillRect (row);
        }

        // pad
        auto pad = padArea (t).toFloat();
        g.setColour (Pequod::Colours::well);
        g.fillRoundedRectangle (pad, 3.0f);
        if (hit[(size_t) t] > 0.0f)
        {
            g.setColour (Pequod::Colours::mustard.withAlpha (0.18f + 0.6f * hit[(size_t) t]));
            g.fillRoundedRectangle (pad, 3.0f);
        }
        g.setColour (sel ? Pequod::Colours::mustard : Pequod::Colours::line);
        g.drawRoundedRectangle (pad.reduced (0.5f), 3.0f, sel ? 1.5f : 1.0f);
        g.setFont (heading (13.0f));
        g.setColour (silent ? Pequod::Colours::textDim : Pequod::Colours::text);
        g.drawText (instrumentName (t), pad.reduced (8, 0).toNearestInt(), juce::Justification::centredLeft);
        if (tr.sample.load() != nullptr)
        {
            g.setFont (body (10.0f, true));
            g.setColour (Pequod::Colours::mustard);
            g.drawText ("SMP", pad.reduced (6, 0).toNearestInt(), juce::Justification::centredRight);
        }

        // mute / solo
        auto drawToggle = [&] (juce::Rectangle<int> r, const char* label, bool on, juce::Colour onColour)
        {
            g.setColour (on ? onColour : Pequod::Colours::panelHi);
            g.fillRoundedRectangle (r.toFloat(), 2.0f);
            g.setColour (on ? Pequod::Colours::bg : Pequod::Colours::textDim);
            g.setFont (heading (11.0f));
            g.drawText (label, r, juce::Justification::centred);
        };
        drawToggle (muteArea (t), "M", tr.mute.load(), Pequod::Colours::danger);
        drawToggle (soloArea (t), "S", tr.solo.load(), Pequod::Colours::mustard);

        // steps
        for (int s = 0; s < len; ++s)
        {
            const auto r = stepArea (t, s).toFloat();
            const int v = pt.steps[(size_t) t][(size_t) s].load();
            const bool beatStart = s % spb == 0;
            const bool here = s == playStep;

            g.setColour (here ? Pequod::Colours::panelHi.brighter (0.25f) : (beatStart ? Pequod::Colours::panelHi.brighter (0.06f) : Pequod::Colours::panelHi));
            g.fillRoundedRectangle (r, 2.0f);

            if (v > 0)
            {
                const auto c = v == 1 ? Pequod::Colours::mustardLo : v == 2 ? Pequod::Colours::mustard : Pequod::Colours::mustard.brighter (0.35f);
                const auto inner = v == 1 ? r.reduced (r.getWidth() * 0.22f, r.getHeight() * 0.22f) : r;
                g.setColour (silent ? c.withSaturation (0.15f).darker (0.3f) : c);
                g.fillRoundedRectangle (inner, 2.0f);
                if (v == 3)
                {
                    g.setColour (Pequod::Colours::text);
                    g.drawRoundedRectangle (r.reduced (1.0f), 2.0f, 1.5f);
                }
            }
            if (here)
            {
                g.setColour (Pequod::Colours::text.withAlpha (0.55f));
                g.drawRoundedRectangle (r, 2.0f, 1.0f);
            }
        }

        // meter
        auto m = row.withX (getWidth() - meterWidth - 2).withWidth (meterWidth).reduced (2, 4).toFloat();
        g.setColour (Pequod::Colours::well);
        g.fillRect (m);
        g.setColour (level[(size_t) t] > 0.95f ? Pequod::Colours::danger : Pequod::Colours::mustard);
        g.fillRect (m.withTop (m.getBottom() - m.getHeight() * level[(size_t) t]));
    }

}

} // namespace thoreau
