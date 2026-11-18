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
    repaint (stepArea (t, s).expanded (8));     // (with its LED's glow)
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
    // Only what moved is drawn again: the step column the playhead left and the one it's on, and
    // each voice's pad and meter while they glow - not the whole grid thirty times a second.
    const int ps = engine.isRolling() && engine.getPlayingPattern() == pattern ? engine.getPlayingStep() : -1;
    if (ps != playStep)
    {
        auto column = [this] (int s)
        {
            if (s >= 0) repaint (stepArea (0, s).getUnion (stepArea (kTracks - 1, s)).expanded (8));
        };
        column (playStep);
        column (ps);
        playStep = ps;
    }
    for (int t = 0; t < kTracks; ++t)
    {
        const float h = engine.popTrackHit (t);
        const float pk = engine.popTrackPeak (t);
        const float nh = juce::jmax (h, hit[(size_t) t] * 0.82f);
        const float nl = juce::jmax (juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (pk, -48.0f) + 48.0f) / 48.0f), level[(size_t) t] * 0.88f);
        const bool padMoved = std::abs (nh - hit[(size_t) t]) > 0.01f;
        const bool meterMoved = std::abs (nl - level[(size_t) t]) > 0.01f;
        hit[(size_t) t] = nh < 0.02f ? 0.0f : nh;
        level[(size_t) t] = nl < 0.01f ? 0.0f : nl;
        if (padMoved)   repaint (padArea (t).expanded (2));
        if (meterMoved) repaint (rowArea (t).withX (getWidth() - meterWidth - 2).withWidth (meterWidth));
    }
}

void StepGrid::paint (juce::Graphics& g)
{
    // The machine's keys: a rubber pad per voice (it lights when it plays), mute and solo keys with
    // their lamps, and the step keys - each with an LED, their caps coloured by the beat as on the
    // classic drum machines (red, orange, yellow, cream) so the bar reads at a glance.
    const auto& pt = engine.project.patterns[(size_t) pattern];
    const int len = juce::jmax (1, pt.length.load());
    const int spb = juce::jmax (1, pt.stepsPerBeat.load());
    bool anySolo = false;
    for (auto& tr : engine.project.tracks) anySolo = anySolo || tr.solo.load();

    static const juce::Colour caps[4] = { juce::Colour (0xff6e2e24), juce::Colour (0xff7a4c26), juce::Colour (0xff76692e), juce::Colour (0xff6c6860) };
    const auto led = Pequod::Colours::mustard.brighter (0.3f);

    for (int t = 0; t < kTracks; ++t)
    {
        const auto row = rowArea (t);
        const auto& tr = engine.project.tracks[(size_t) t];
        const bool sel = t == selected;
        const bool silent = tr.mute.load() || (anySolo && ! tr.solo.load());

        if (sel)
        {
            g.setColour (Pequod::Colours::mustard.withAlpha (0.07f));
            g.fillRoundedRectangle (row.toFloat(), 3.0f);
        }

        // the pad: soft grey rubber, backlit when it plays
        auto pad = padArea (t).toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.fillRoundedRectangle (pad.expanded (1.0f).translated (0.0f, 1.0f), 5.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4a4d4a), pad.getX(), pad.getY(), juce::Colour (0xff2c2e2c), pad.getX(), pad.getBottom(), false));
        g.fillRoundedRectangle (pad, 4.5f);
        if (hit[(size_t) t] > 0.0f)
        {
            g.setGradientFill (juce::ColourGradient (Pequod::Colours::mustard.withAlpha (0.25f + 0.55f * hit[(size_t) t]), pad.getCentreX(), pad.getCentreY(),
                                                     Pequod::Colours::mustard.withAlpha (0.05f), pad.getX(), pad.getY(), true));
            g.fillRoundedRectangle (pad, 4.5f);
        }
        g.setColour (sel ? Pequod::Colours::mustard : juce::Colours::white.withAlpha (0.08f));
        g.drawRoundedRectangle (pad.reduced (0.5f), 4.5f, sel ? 1.5f : 1.0f);
        g.setFont (heading (13.0f));
        g.setColour (silent ? Pequod::Colours::textDim : Pequod::Colours::text);
        g.drawText (engine.project.padName (t), pad.reduced (9, 0).toNearestInt(), juce::Justification::centredLeft);
        if (tr.sample.load() != nullptr)
        {
            g.setFont (body (10.0f, true));
            g.setColour (Pequod::Colours::mustard);
            g.drawText ("SMP", pad.reduced (7, 0).toNearestInt(), juce::Justification::centredRight);
        }

        // mute / solo: keys with a lamp above the letter
        auto drawToggle = [&] (juce::Rectangle<int> r, const char* label, bool on, juce::Colour onColour)
        {
            auto k = r.toFloat();
            hw::key (g, k, juce::Colour (0xff3e413e), false, false, onColour);
            hw::lamp (g, { k.getCentreX(), k.getY() + k.getHeight() * 0.3f }, juce::jmin (3.0f, k.getHeight() * 0.1f), onColour, on);
            g.setColour (on ? Pequod::Colours::text : Pequod::Colours::textDim);
            g.setFont (heading (10.0f));
            g.drawText (label, k.withTrimmedTop (k.getHeight() * 0.48f), juce::Justification::centred);
        };
        drawToggle (muteArea (t), "M", tr.mute.load(), Pequod::Colours::danger.brighter (0.3f));
        drawToggle (soloArea (t), "S", tr.solo.load(), juce::Colour (0xffffc040));

        // steps
        for (int s = 0; s < len; ++s)
        {
            const auto r = stepArea (t, s).toFloat();
            const int v = pt.steps[(size_t) t][(size_t) s].load();
            const bool here = s == playStep;
            const auto cap = caps[(s / spb) % 4];
            // (a key that's on is lit from inside, as well as its LED)
            hw::key (g, r, here ? cap.brighter (0.3f) : cap, v > 0, v > 0 && ! silent, cap.brighter (v == 1 ? 0.25f : 0.6f));

            // the LED: off, soft (dim), on, accent (bright, with a halo)
            const float rad = juce::jlimit (2.5f, 6.0f, juce::jmin (r.getWidth(), r.getHeight()) * 0.16f);
            const juce::Point<float> c { r.getCentreX(), r.getY() + juce::jmax (rad + 3.0f, r.getHeight() * 0.3f) };
            auto colour = silent ? led.withSaturation (0.1f) : led;
            if (v == 0)
            {
                g.setColour (juce::Colour (0xff141814));
                g.fillEllipse (c.x - rad, c.y - rad, rad * 2.0f, rad * 2.0f);
            }
            else
            {
                const float level = v == 1 ? 0.45f : v == 2 ? 0.85f : 1.0f;
                g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.5f * level), c.x, c.y, colour.withAlpha (0.0f), c.x + rad * (v == 3 ? 4.0f : 2.6f), c.y, true));
                g.fillEllipse (c.x - rad * 4.0f, c.y - rad * 4.0f, rad * 8.0f, rad * 8.0f);
                g.setColour (colour.withMultipliedBrightness (0.45f + 0.55f * level));
                g.fillEllipse (c.x - rad, c.y - rad, rad * 2.0f, rad * 2.0f);
                g.setColour (juce::Colours::white.withAlpha (0.5f * level));
                g.fillEllipse (c.x - rad * 0.45f, c.y - rad * 0.5f, rad * 0.6f, rad * 0.45f);
                if (v == 3)
                {
                    g.setColour (colour.withAlpha (0.9f));
                    g.drawRoundedRectangle (r.reduced (0.5f), 2.5f, 1.4f);
                }
            }
        }

        // meter: a ladder of LEDs
        auto m = row.withX (getWidth() - meterWidth - 2).withWidth (meterWidth).reduced (2, 4).toFloat();
        hw::ledLadder (g, m, level[(size_t) t], juce::jlimit (4, 10, (int) (m.getHeight() / 4.0f)), Pequod::Colours::mustard);
    }
}

} // namespace thoreau
