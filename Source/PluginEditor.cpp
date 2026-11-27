#include "PluginEditor.h"
#include "BinaryData.h"

using namespace juce;

static String fr (const char* utf8) { return String (CharPointer_UTF8 (utf8)); }

// ============================================================ Thème
namespace
{
    Colour gAccent { 0xff22d3ee };
}

Colour SwellColours::accent()  { return gAccent; }
void   SwellColours::setAccent (Colour c) { gAccent = c; }

const std::vector<SwellColours::Swatch>& SwellColours::swatches()
{
    static const std::vector<Swatch> s {
        { "Orange",  0xffff7a3d }, { "Red",     0xffef4444 }, { "Pink",    0xffec4899 },
        { "Purple",  0xff8b5cf6 }, { "Blue",    0xff3b82f6 }, { "Cyan",    0xff22d3ee },
        { "Green",   0xff22c55e }, { "Gold",    0xfff5b301 }, { "White",   0xffe8e6e1 },
    };
    return s;
}

PropertiesFile& SwellSettings::file()
{
    static PropertiesFile pf ([]
    {
        PropertiesFile::Options o;
        o.applicationName     = "Swell Stutter";
        o.filenameSuffix      = ".settings";
        o.folderName          = "Swell Stutter";
        o.osxLibrarySubFolder = "Application Support";
        o.millisecondsBeforeSaving = 0;   // sauvegarde immédiate
        return o;
    }());
    return pf;
}


// ============================================================ LookAndFeel
SwellLookAndFeel::SwellLookAndFeel() { refreshColours(); }

void SwellLookAndFeel::refreshColours()
{
    setColour (Slider::textBoxTextColourId,       SwellColours::text);
    setColour (Slider::textBoxOutlineColourId,    Colours::transparentBlack);
    setColour (Slider::textBoxBackgroundColourId, Colours::transparentBlack);
    setColour (Slider::textBoxHighlightColourId,  SwellColours::accent().withAlpha (0.4f));
    setColour (Label::textColourId,               SwellColours::dim);
    setColour (PopupMenu::backgroundColourId,     SwellColours::panel);
    setColour (PopupMenu::textColourId,           SwellColours::text);
    setColour (PopupMenu::headerTextColourId,     SwellColours::dim);
    setColour (PopupMenu::highlightedBackgroundColourId, SwellColours::track);
    setColour (PopupMenu::highlightedTextColourId, SwellColours::text);
    setColour (TextButton::buttonColourId,        SwellColours::panel);
    setColour (TextButton::textColourOffId,       SwellColours::text);
    setColour (TextButton::textColourOnId,        SwellColours::text);
    setColour (AlertWindow::backgroundColourId,   SwellColours::panel);
    setColour (AlertWindow::textColourId,         SwellColours::text);
    setColour (AlertWindow::outlineColourId,      SwellColours::track);
    setColour (TextEditor::backgroundColourId,    SwellColours::bg);
    setColour (TextEditor::textColourId,          SwellColours::text);
    setColour (TextEditor::outlineColourId,       SwellColours::track);
    setColour (TextEditor::focusedOutlineColourId, SwellColours::dim);
    setColour (TextEditor::highlightColourId,     SwellColours::accent().withAlpha (0.35f));
    setColour (ScrollBar::thumbColourId,          SwellColours::track.brighter (0.2f));
    setColour (TooltipWindow::backgroundColourId, SwellColours::panel.brighter (0.05f));
    setColour (TooltipWindow::textColourId,       SwellColours::text);
    setColour (TooltipWindow::outlineColourId,    SwellColours::track);
}

// Bouton rotatif : un arc fin, un point. Rien d'autre.
void SwellLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos,
                                         float startAngle, float endAngle, Slider& s)
{
    const auto bounds = Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.0f);
    const float radius = jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto  centre = bounds.getCentre();
    const float lineW  = 2.5f;
    const float arcR   = radius - lineW;
    const float angle  = startAngle + pos * (endAngle - startAngle);
    const float alpha  = s.isEnabled() ? 1.0f : 0.3f;
    const bool  hover  = s.isMouseOverOrDragging() && s.isEnabled();

    Path bgArc;
    bgArc.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
    g.setColour (SwellColours::track.withAlpha (alpha));
    g.strokePath (bgArc, PathStrokeType (lineW, PathStrokeType::curved, PathStrokeType::rounded));

    if (pos > 0.001f)
    {
        Path valArc;
        valArc.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, angle, true);
        g.setColour (SwellColours::accent().withAlpha (alpha));
        g.strokePath (valArc, PathStrokeType (lineW, PathStrokeType::curved, PathStrokeType::rounded));
    }

    const float bodyR = arcR - 7.0f;
    g.setColour ((hover ? SwellColours::track.brighter (0.08f) : SwellColours::track).withAlpha (alpha));
    g.fillEllipse (centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);

    const float dotDist = bodyR - 6.0f;
    const Point<float> dot (centre.x + dotDist * std::sin (angle), centre.y - dotDist * std::cos (angle));
    g.setColour (SwellColours::text.withAlpha (alpha));
    g.fillEllipse (dot.x - 2.2f, dot.y - 2.2f, 4.4f, 4.4f);
}

void SwellLookAndFeel::drawToggleButton (Graphics& g, ToggleButton& b, bool highlighted, bool)
{
    auto r = b.getLocalBounds().toFloat();
    const bool on = b.getToggleState();

    // Case à cocher (réglages)
    if (b.getProperties()["check"])
    {
        auto box = r.removeFromLeft (r.getHeight()).withSizeKeepingCentre (14.0f, 14.0f);
        g.setColour (on ? SwellColours::accent() : (highlighted ? SwellColours::dim : SwellColours::track.brighter (0.3f)));
        g.drawRoundedRectangle (box, 3.0f, 1.4f);
        if (on)
        {
            Path tick;
            tick.startNewSubPath (box.getX() + 3.0f, box.getCentreY());
            tick.lineTo (box.getX() + 6.0f, box.getBottom() - 3.5f);
            tick.lineTo (box.getRight() - 3.0f, box.getY() + 3.5f);
            g.strokePath (tick, PathStrokeType (1.8f, PathStrokeType::curved, PathStrokeType::rounded));
        }
        g.setColour (SwellColours::text);
        g.setFont (FontOptions (13.0f));
        g.drawText (b.getButtonText(), r.withTrimmedLeft (4.0f), Justification::centredLeft);
        return;
    }

    // Interrupteur texte (Sync, More) : un petit point + le mot
    const float d = 6.0f;
    auto dotArea = r.removeFromLeft (d + 8.0f);
    const auto dot = Rectangle<float> (d, d).withCentre ({ dotArea.getX() + d * 0.5f + 1.0f, r.getCentreY() });
    if (on)
    {
        g.setColour (SwellColours::accent());
        g.fillEllipse (dot);
    }
    else
    {
        g.setColour (SwellColours::dim);
        g.drawEllipse (dot.reduced (0.5f), 1.2f);
    }
    g.setColour (on || highlighted ? SwellColours::text : SwellColours::dim);
    g.setFont (FontOptions (13.0f));
    g.drawText (b.getButtonText(), r, Justification::centredLeft);
}

void SwellLookAndFeel::drawButtonBackground (Graphics& g, Button& b, const Colour& colour, bool highlighted, bool down)
{
    if (b.getProperties()["flat"])
    {
        if (highlighted || down)
        {
            g.setColour (SwellColours::track.withAlpha (down ? 0.9f : 0.6f));
            g.fillRoundedRectangle (b.getLocalBounds().toFloat(), 5.0f);
        }
        return;
    }
    auto c = colour;
    if (down)             c = c.brighter (0.12f);
    else if (highlighted) c = c.brighter (0.06f);
    g.setColour (c.withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.4f));
    g.fillRoundedRectangle (b.getLocalBounds().toFloat(), 6.0f);
}

void SwellLookAndFeel::drawButtonText (Graphics& g, TextButton& b, bool highlighted, bool)
{
    const bool flat = b.getProperties()["flat"];
    g.setFont (getTextButtonFont (b, b.getHeight()));
    g.setColour ((flat && ! highlighted ? SwellColours::label : SwellColours::text)
                     .withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.4f));
    g.drawText (b.getButtonText(), b.getLocalBounds().reduced (6, 0), Justification::centred, true);
}

Font SwellLookAndFeel::getTextButtonFont (TextButton& b, int)
{
    const float size = b.getProperties().contains ("fontSize") ? (float) b.getProperties()["fontSize"] : 13.0f;
    return FontOptions (size);
}

Font SwellLookAndFeel::getPopupMenuFont()
{
    return FontOptions (14.0f);
}

Rectangle<int> SwellLookAndFeel::getTooltipBounds (const String& tipText, Point<int> screenPos, Rectangle<int> parentArea)
{
    AttributedString s;
    s.append (tipText, FontOptions (13.0f), SwellColours::text);
    TextLayout tl;
    tl.createLayoutWithBalancedLineLengths (s, 240.0f);
    const int w = (int) tl.getWidth() + 20, h = (int) tl.getHeight() + 14;
    return Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 24,
                           screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6)  : screenPos.y + 6,
                           w, h).constrainedWithin (parentArea);
}

void SwellLookAndFeel::drawTooltip (Graphics& g, const String& text, int w, int h)
{
    auto r = Rectangle<float> (0, 0, (float) w, (float) h);
    g.setColour (findColour (TooltipWindow::backgroundColourId));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (SwellColours::track);
    g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);

    AttributedString s;
    s.setJustification (Justification::centredLeft);
    s.append (text, FontOptions (13.0f), SwellColours::text);
    TextLayout tl;
    tl.createLayoutWithBalancedLineLengths (s, 240.0f);
    tl.draw (g, r.reduced (10.0f, 7.0f));
}
// ============================================================ Boutons d'action
void ActionButton::paintButton (Graphics& g, bool highlighted, bool down)
{
    auto r = getLocalBounds().toFloat();
    const float alpha = isEnabled() ? 1.0f : 0.35f;

    Colour bg = SwellColours::panel, ink = SwellColours::text;
    if (recording)    { bg = SwellColours::rec;      ink = SwellColours::text; }
    else if (primary) { bg = SwellColours::accent(); ink = SwellColours::bg; }
    if (isEnabled())
    {
        if (down)             bg = bg.brighter (0.12f);
        else if (highlighted) bg = bg.brighter (0.06f);
    }
    g.setColour (bg.withMultipliedAlpha (alpha));
    g.fillRoundedRectangle (r, 6.0f);

    const Font f (FontOptions (13.0f, Font::bold));
    const float textW = GlyphArrangement::getStringWidth (f, getButtonText());
    const float iconW = 12.0f, gapW = 8.0f;
    const float startX = r.getCentreX() - (iconW + gapW + textW) * 0.5f;
    const auto icon = Rectangle<float> (startX, r.getCentreY() - iconW * 0.5f, iconW, iconW);

    switch (this->icon)
    {
        case Icon::record:
            g.setColour ((recording ? SwellColours::text : SwellColours::rec).withMultipliedAlpha (alpha));
            g.fillEllipse (icon.reduced (1.5f));
            break;
        case Icon::play:
            g.setColour (ink.withMultipliedAlpha (alpha));
            if (getButtonText() == "Stop")
                g.fillRoundedRectangle (icon.reduced (2.0f), 1.5f);
            else
            {
                Path p;
                p.addTriangle (icon.getX() + 2.0f, icon.getY() + 1.0f, icon.getX() + 2.0f, icon.getBottom() - 1.0f,
                               icon.getRight() - 1.0f, icon.getCentreY());
                g.fillPath (p);
            }
            break;
        case Icon::drag:
            g.setColour (ink.withMultipliedAlpha (alpha));
            for (int row = 0; row < 3; ++row)
                for (int col = 0; col < 2; ++col)
                    g.fillEllipse (icon.getX() + 3.0f + (float) col * 4.5f, icon.getY() + 1.0f + (float) row * 4.5f, 2.4f, 2.4f);
            break;
    }

    g.setColour (ink.withMultipliedAlpha (alpha));
    g.setFont (f);
    g.drawText (getButtonText(), Rectangle<float> (icon.getRight() + gapW, r.getY(), textW + 4.0f, r.getHeight()),
                Justification::centredLeft);
}

void ActionButton::mouseDown (const MouseEvent& e)
{
    dragStarted = false;
    TextButton::mouseDown (e);
}

void ActionButton::mouseDrag (const MouseEvent& e)
{
    TextButton::mouseDrag (e);
    if (onDragOut && ! dragStarted && isEnabled() && e.getDistanceFromDragStart() > 6)
    {
        dragStarted = true;
        onDragOut();
    }
}

// ============================================================ Overlays
OverlayPanel::OverlayPanel (const String& t) : title (t)
{
    closeButton.getProperties().set ("flat", true);
    closeButton.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (closeButton);
    setWantsKeyboardFocus (true);
}

void OverlayPanel::mouseDown (const MouseEvent& e)
{
    if (! card.contains (e.getPosition()) && onClose)
        onClose();
}

void OverlayPanel::paint (Graphics& g)
{
    g.fillAll (Colours::black.withAlpha (0.55f));
    g.setColour (SwellColours::panel);
    g.fillRoundedRectangle (card.toFloat(), 10.0f);

    g.setColour (SwellColours::text);
    g.setFont (FontOptions (17.0f, Font::bold));
    g.drawText (title, card.reduced (22, 0).removeFromTop (54), Justification::centredLeft);
}

void OverlayPanel::resized()
{
    card = getLocalBounds().reduced (40, 36);
    if (cardHeight > 0)
        card = card.withSizeKeepingCentre (card.getWidth(), jmin (card.getHeight(), cardHeight));
    closeButton.setBounds (card.getRight() - 74, card.getY() + 13, 58, 28);
    content = card.reduced (22, 0).withTrimmedTop (54).withTrimmedBottom (20);
    layoutContent (content);
}

HelpOverlay::HelpOverlay() : OverlayPanel ("How it works")
{
    text.setMultiLine (true, true);
    text.setReadOnly (true);
    text.setCaretVisible (false);
    text.setScrollbarsShown (true);
    text.setColour (TextEditor::backgroundColourId, SwellColours::panel);
    text.setColour (TextEditor::outlineColourId, Colours::transparentBlack);
    text.setBorder (BorderSize<int> (0));
    const String help = fr (
        "Swell Stutter makes the vocal stutter: I-I-I-I want, ta-ta-ta-ta.\n\n"
        "Make a clip\n"
        "Press Capture, then play your vocal in your DAW. Capture stops with playback.\n"
        "You can also drop a vocal sample on the big area, or click it to load one.\n"
        "Swell finds the first word and chops it in time with your tempo.\n"
        "Want another word? Click it in the strip above the clip: you hear it, and it gets chopped.\n"
        "Drag the clip into your playlist. Done.\n\n"
        "Knobs\n"
        "Rate: how long each repeat lasts, locked to your tempo (1/4 to 1/32, T = triplet).\n"
        "Repeats: how many times the word is repeated before the phrase goes on.\n"
        "Build: the repeats get faster and faster, like a build-up.\n"
        "Pitch: the repeats climb in pitch, up to the last one.\n"
        "Gate: 100 % = repeats touch each other. Lower = shorter, drier hits.\n\n"
        "More\n"
        "Word: which word is chopped (or click it in the strip). Fade in: the repeats grow louder.\n"
        "Pan: the repeats bounce left and right. Output: clip level.\n\n"
        "FX\n"
        "Chorus, Phone (telephone voice) and Backverb (reverse reverb rising into the word).\n"
        "They colour the repeats only: the phrase comes back clean.\n"
        "With Backverb, the clip starts 1 beat early: drop it one beat before the word.\n\n"
        "Tips\n"
        "Every knob change makes a new clip by itself. Preview to listen.\n"
        "Double-click a knob to reset it. Clips are saved in Music/Swell Stutter.");
    for (auto& line : StringArray::fromLines (help))
    {
        const bool heading = line.isNotEmpty() && line.length() < 18 && ! line.containsChar ('.');
        text.setFont (heading ? FontOptions (14.0f, Font::bold) : FontOptions (14.0f));
        text.setColour (TextEditor::textColourId, heading ? SwellColours::text : SwellColours::label);
        text.insertTextAtCaret (line + "\n");
    }
    text.moveCaretToTop (false);
    addAndMakeVisible (text);
}

SettingsOverlay::SettingsOverlay() : OverlayPanel ("Settings")
{
    setCardHeight (330);
    for (auto& s : SwellColours::swatches())
    {
        auto* b = swatchButtons.add (new TextButton());
        const Colour c (s.argb);
        b->setTooltip (fr (s.name));
        b->setColour (TextButton::buttonColourId, c);
        b->onClick = [this, c] { currentAccent = c; if (onAccent) onAccent (c); repaint(); };
        addAndMakeVisible (b);
    }
    for (float s : { 0.75f, 1.0f, 1.25f, 1.5f })
    {
        auto* b = scaleButtons.add (new TextButton (String (roundToInt (s * 100)) + " %"));
        b->onClick = [this, s] { currentScale = s; if (onScale) onScale (s); repaint(); };
        addAndMakeVisible (b);
    }
    tooltipToggle.getProperties().set ("check", true);
    tooltipToggle.onClick = [this] { if (onTooltips) onTooltips (tooltipToggle.getToggleState()); };
    addAndMakeVisible (tooltipToggle);
}

void SettingsOverlay::setCurrent (Colour a, float s, bool tips)
{
    currentAccent = a;
    currentScale = s;
    tooltipToggle.setToggleState (tips, dontSendNotification);
    repaint();
}

void SettingsOverlay::layoutContent (Rectangle<int> area)
{
    colourLabel = area.removeFromTop (22);
    auto row = area.removeFromTop (30);
    for (auto* b : swatchButtons)
    {
        b->setBounds (row.removeFromLeft (30));
        row.removeFromLeft (10);
    }
    area.removeFromTop (20);

    scaleLabel = area.removeFromTop (22);
    row = area.removeFromTop (30);
    for (auto* b : scaleButtons)
    {
        b->setBounds (row.removeFromLeft (70));
        row.removeFromLeft (8);
    }
    area.removeFromTop (20);
    tooltipToggle.setBounds (area.removeFromTop (28).removeFromLeft (180));
}

void SettingsOverlay::paint (Graphics& g)
{
    OverlayPanel::paint (g);
    g.setColour (SwellColours::dim);
    g.setFont (FontOptions (13.0f));
    g.drawText ("Colour", colourLabel, Justification::centredLeft);
    g.drawText ("Window size", scaleLabel, Justification::centredLeft);

    // repère de la sélection courante
    g.setColour (SwellColours::text);
    for (int i = 0; i < swatchButtons.size(); ++i)
        if (Colour (SwellColours::swatches()[(size_t) i].argb) == currentAccent)
            g.drawRoundedRectangle (swatchButtons[i]->getBounds().toFloat().expanded (3.0f), 8.0f, 1.5f);
    for (int i = 0; i < scaleButtons.size(); ++i)
        if (std::abs (currentScale - (0.75f + 0.25f * (float) i)) < 0.01f)
            g.drawRoundedRectangle (scaleButtons[i]->getBounds().toFloat().expanded (2.0f), 8.0f, 1.2f);

    g.setColour (SwellColours::dim);
    g.setFont (FontOptions (12.0f));
    auto bottom = getContentArea().removeFromBottom (36);
    g.drawText ("Swell Stutter  v" + String (SWELL_VERSION) + fr ("  ·  by @sevyondatracks"), bottom, Justification::centredLeft);
}

// ============================================================ Zone du clip
void ClipView::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (SwellColours::panel);
    g.fillRoundedRectangle (r, 10.0f);

    auto wave = r.reduced (22.0f, 30.0f);
    if (! info.words.empty())
    {
        wave.setTop (wordLane().getBottom() + 14.0f);

        // Bande des mots : la voix entière, chaque mot cliquable, le mot haché en couleur.
        const auto lane = wordLane();
        const auto& sp = info.sourcePeaks;
        const int scols = (int) sp.size() / 2;
        for (int i = 0; i < (int) info.words.size(); ++i)
        {
            const auto box = wordBox (i);
            const bool sel = i == info.wordIndex, over = i == hoverWord;
            g.setColour (sel ? SwellColours::accent().withAlpha (wordPlaying ? 0.32f : 0.20f)
                             : SwellColours::track.withAlpha (over ? 0.9f : 0.45f));
            g.fillRoundedRectangle (box, 4.0f);
            if (sel)
            {
                g.setColour (SwellColours::accent());
                g.drawRoundedRectangle (box.reduced (0.5f), 4.0f, 1.2f);
            }
        }
        if (scols > 1)
        {
            const float mid = lane.getCentreY(), half = lane.getHeight() * 0.42f;
            const int px = jmax (1, (int) lane.getWidth());
            for (int x = 0; x < px; ++x)
            {
                const int c0 = x * scols / px, c1 = jmax (c0 + 1, (x + 1) * scols / px);
                float mn = 0.0f, mx = 0.0f;
                for (int i = c0; i < c1 && i < scols; ++i) { mn = jmin (mn, sp[(size_t) i * 2]); mx = jmax (mx, sp[(size_t) i * 2 + 1]); }
                const float frac = (float) x / (float) px;
                bool inSel = false;
                if (isPositiveAndBelow (info.wordIndex, (int) info.words.size()))
                    inSel = frac >= info.words[(size_t) info.wordIndex].first && frac < info.words[(size_t) info.wordIndex].second;
                g.setColour (inSel ? SwellColours::text : SwellColours::label.withAlpha (0.5f));
                const float top = mid - jmin (1.0f, mx) * half, bottom = mid - jmax (-1.0f, mn) * half;
                g.fillRect (lane.getX() + (float) x, top, 1.0f, jmax (1.0f, bottom - top));
            }
        }
    }
    const auto& pk = info.peaks;
    const int cols = (int) pk.size() / 2;

    if (hasClip() && cols > 1)
    {
        const float startFrac = info.slotMarks.empty() ? 0.0f : info.slotMarks.front();
        const float mid = wave.getCentreY(), half = wave.getHeight() * 0.5f;
        const int px = jmax (1, (int) wave.getWidth());
        const Colour hot = SwellColours::accent(), cold = SwellColours::label.withAlpha (0.55f);

        // Vraie forme d'onde : un trait min -> max par colonne de pixels
        for (int x = 0; x < px; ++x)
        {
            const int c0 = x * cols / px, c1 = jmax (c0 + 1, (x + 1) * cols / px);
            float mn = 0.0f, mx = 0.0f;
            for (int i = c0; i < c1 && i < cols; ++i)
            {
                mn = jmin (mn, pk[(size_t) i * 2]);
                mx = jmax (mx, pk[(size_t) i * 2 + 1]);
            }
            const float frac = (float) x / (float) px;
            g.setColour (frac >= startFrac && frac < info.stutterEndFrac ? hot : cold);
            const float top = mid - jmin (1.0f, mx) * half, bottom = mid - jmax (-1.0f, mn) * half;
            g.fillRect (wave.getX() + (float) x, top, 1.0f, jmax (1.0f, bottom - top));
        }
        g.setColour (SwellColours::track);
        g.fillRect (wave.getX(), mid - 0.5f, wave.getWidth(), 1.0f);

        // Un repère discret au début de chaque répétition
        g.setColour (SwellColours::text.withAlpha (0.18f));
        for (float m : info.slotMarks)
            g.fillRect (wave.getX() + wave.getWidth() * m - 0.5f, wave.getY() - 8.0f, 1.0f, wave.getHeight() + 16.0f);

        if (playhead >= 0.0f)
        {
            g.setColour (SwellColours::text);
            g.fillRect (wave.getX() + wave.getWidth() * playhead - 0.75f, r.getY() + 12.0f, 1.5f, r.getHeight() - 24.0f);
        }
    }
    else
    {
        g.setColour (SwellColours::text);
        g.setFont (FontOptions (15.0f));
        g.drawText ("Capture a vocal", r.withTrimmedBottom (r.getHeight() * 0.5f - 2.0f).withTrimmedTop (r.getHeight() * 0.5f - 22.0f),
                    Justification::centred);
        g.setColour (SwellColours::dim);
        g.setFont (FontOptions (13.0f));
        g.drawText ("or drop a sample here, or click to load one", r.withTrimmedTop (r.getHeight() * 0.5f + 2.0f).withHeight (20.0f),
                    Justification::centred);
    }

    if (info.status.isNotEmpty())
    {
        g.setColour (SwellColours::dim);
        g.setFont (FontOptions (12.0f));
        g.drawText (info.busy ? fr ("Rendering…") : info.status, r.reduced (14.0f, 7.0f).removeFromBottom (16.0f),
                    Justification::centredLeft);
    }

    if (highlight)
    {
        g.setColour (SwellColours::accent());
        g.drawRoundedRectangle (r.reduced (1.0f), 10.0f, 1.5f);
    }
}

Rectangle<float> ClipView::wordLane() const
{
    return getLocalBounds().toFloat().reduced (22.0f, 0.0f).withTrimmedTop (16.0f).withHeight (34.0f);
}

Rectangle<float> ClipView::wordBox (int i) const
{
    const auto lane = wordLane();
    if (! isPositiveAndBelow (i, (int) info.words.size()))
        return {};
    const auto [a, b] = info.words[(size_t) i];
    const float x0 = lane.getX() + lane.getWidth() * a, x1 = lane.getX() + lane.getWidth() * b;
    return { x0 - 2.0f, lane.getY(), jmax (6.0f, x1 - x0 + 4.0f), lane.getHeight() };
}

int ClipView::wordAt (Point<float> p) const
{
    if (! wordLane().expanded (0.0f, 4.0f).contains (p))
        return -1;
    int best = -1;
    float bestDist = 1.0e9f;
    for (int i = 0; i < (int) info.words.size(); ++i)
    {
        const auto box = wordBox (i);
        if (box.contains (p.x, box.getCentreY())) return i;
        const float d = jmin (std::abs (p.x - box.getX()), std::abs (p.x - box.getRight()));
        if (d < bestDist) { bestDist = d; best = i; }
    }
    return bestDist < 12.0f ? best : -1;
}

void ClipView::mouseDown (const MouseEvent& e)
{
    dragging = false;
    pressInLane = ! info.words.empty() && wordLane().expanded (0.0f, 4.0f).contains (e.position);
    if (pressInLane)
        if (const int w = wordAt (e.position); w >= 0 && onWordClicked)
            onWordClicked (w);
}

void ClipView::mouseMove (const MouseEvent& e)
{
    const int w = info.words.empty() ? -1 : wordAt (e.position);
    if (w != hoverWord) { hoverWord = w; repaint(); }
    setMouseCursor (w >= 0 ? MouseCursor::PointingHandCursor : MouseCursor::NormalCursor);
}

void ClipView::mouseExit (const MouseEvent&)
{
    if (hoverWord >= 0) { hoverWord = -1; repaint(); }
}

void ClipView::startDrag()
{
    if (! hasClip())
        return;
    if (onDragStarted)
        onDragStarted();
    DragAndDropContainer::performExternalDragDropOfFiles ({ info.file.getFullPathName() }, false, this);
}

void ClipView::mouseDrag (const MouseEvent& e)
{
    if (pressInLane || dragging || ! hasClip() || e.getDistanceFromDragStart() < 6)
        return;
    dragging = true;
    startDrag();
}

void ClipView::mouseUp (const MouseEvent& e)
{
    if (! pressInLane && ! dragging && ! hasClip() && e.getDistanceFromDragStart() < 4 && onClickEmpty)
        onClickEmpty();
}

// ============================================================ Vue principale
StutterMainView::StutterMainView (StutterAudioProcessor& p, SwellLookAndFeel& l) : proc (p), lnf (l)
{
    logoS = ImageCache::getFromMemory (BinaryData::logo_s_png, BinaryData::logo_s_pngSize);

    setupKnob (rate,    ParamID::rate,    "Rate",    "How long each repeat lasts, locked to your tempo. T = triplet.");
    setupKnob (repeats, ParamID::repeats, "Repeats", "How many times the word repeats before the phrase goes on.");
    setupKnob (build,   ParamID::build,   "Build",   "The repeats speed up toward the end, like a build-up.");
    setupKnob (pitch,   ParamID::pitch,   "Pitch",   "The repeats climb in pitch, up to the last one.");
    setupKnob (gate,    ParamID::gate,    "Gate",    "100 %: repeats touch each other. Lower: shorter, drier hits.");
    setupKnob (word,    ParamID::word,    "Word",    "Which word to chop: the 1st, 2nd, 3rd...");
    setupKnob (output,  ParamID::output,  "Output",  "Level of the clip.");
    setupKnob (fadeIn,  ParamID::fadeIn,  "Fade in", "The repeats start quiet and grow louder.");
    setupKnob (pan,     ParamID::pan,     "Pan",     "The repeats bounce left and right.");
    setupKnob (chorus,  ParamID::chorus,  "Chorus",  "Wide, doubled repeats. The phrase comes back clean.");
    setupKnob (phone,   ParamID::phone,   "Phone",   "Telephone voice on the repeats. The phrase comes back clean.");
    setupKnob (backverb, ParamID::backverb, "Backverb", "Reverse reverb rising into the word. The clip starts 1 beat early.");

    moreButton.setClickingTogglesState (true);
    moreButton.setTooltip ("Word, fade in, pan and output.");
    moreButton.onClick = [this] { setPage (moreButton.getToggleState() ? 1 : 0); };
    addAndMakeVisible (moreButton);
    fxButton.setClickingTogglesState (true);
    fxButton.setTooltip ("Chorus, phone and backverb on the repeats.");
    fxButton.onClick = [this] { setPage (fxButton.getToggleState() ? 2 : 0); };
    addAndMakeVisible (fxButton);

    menuButton.setButtonText (String (CharPointer_UTF8 ("\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2")));
    menuButton.getProperties().set ("flat", true);
    menuButton.getProperties().set ("fontSize", 10.0f);
    menuButton.setTooltip ("Load a sample, clips folder, help, settings");
    menuButton.onClick = [this] { showMainMenu(); };
    addAndMakeVisible (menuButton);

    presetButton.getProperties().set ("flat", true);
    presetButton.setTooltip ("Presets. Save your own settings with Save preset.");
    presetButton.onClick = [this] { showPresetMenu(); };
    addAndMakeVisible (presetButton);

    addAndMakeVisible (clip);
    clip.onClickEmpty  = [this] { chooseFile(); };
    clip.onDragStarted = [this] { proc.markDragged(); };
    clip.onWordClicked = [this] (int w) { proc.selectWord (w); };
    captureButton.onClick = [this] { proc.toggleCapture(); };
    captureButton.setTooltip ("Arm, then press play in your DAW. Recording starts on the first sound and stops with playback.");
    playButton.onClick = [this] { proc.togglePreview(); };
    playButton.setTooltip ("Listen to the clip.");
    dragButton.setPrimary (true);
    dragButton.onDragOut = [this] { clip.startDrag(); };
    dragButton.setTooltip ("Drag this into your playlist.");
    for (auto* b : { &captureButton, &playButton, &dragButton })
        addAndMakeVisible (b);

    setMoreOpen (false);
    setSize (designW, designH);
    timerCallback();
    startTimerHz (30);
}

StutterMainView::~StutterMainView() { stopTimer(); }

void StutterMainView::setupKnob (Knob& k, const String& paramId, const String& name, const String& tip)
{
    k.slider.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (Slider::TextBoxBelow, false, 90, 18);
    k.slider.setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
    k.slider.setColour (Slider::textBoxTextColourId, SwellColours::text);
    k.slider.setRotaryParameters (MathConstants<float>::pi * 1.25f, MathConstants<float>::pi * 2.75f, true);
    k.slider.setTooltip (tip);
    addAndMakeVisible (k.slider);

    k.label.setText (name, dontSendNotification);
    k.label.setJustificationType (Justification::centred);
    k.label.setFont (FontOptions (13.0f));
    k.label.setColour (Label::textColourId, SwellColours::dim);
    k.label.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (k.label);

    k.attachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, paramId, k.slider);
    if (auto* param = proc.apvts.getParameter (paramId))
        k.slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
}

void StutterMainView::setPage (int p)
{
    page = jlimit (0, 2, p);
    moreButton.setToggleState (page == 1, dontSendNotification);
    fxButton.setToggleState (page == 2, dontSendNotification);
    for (auto* k : mainKnobs) { k->slider.setVisible (page == 0); k->label.setVisible (page == 0); }
    for (auto* k : moreKnobs) { k->slider.setVisible (page == 1); k->label.setVisible (page == 1); }
    for (auto* k : fxKnobs)   { k->slider.setVisible (page == 2); k->label.setVisible (page == 2); }
}

void StutterMainView::showMainMenu()
{
    PopupMenu m;
    m.setLookAndFeel (&lnf);
    m.addItem (1, "Load a sample");
    m.addItem (2, "Open clips folder");
    m.addSeparator();
    m.addItem (3, "How it works");
    m.addItem (4, "Settings");
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (menuButton), [this] (int id)
    {
        switch (id)
        {
            case 1: chooseFile(); break;
            case 2: { auto f = StutterAudioProcessor::getRenderFolder(); f.createDirectory(); f.revealToUser(); break; }
            case 3: showHelp(); break;
            case 4: showSettings(); break;
            default: break;
        }
    });
}

void StutterMainView::refreshPresetLabel()
{
    auto name = proc.getPresetName();
    if (name.isEmpty()) name = "Presets";
    else if (proc.isPresetModified()) name += "*";
    if (name != presetLabel)
    {
        presetLabel = name;
        presetButton.setButtonText (name + String (CharPointer_UTF8 ("  \xe2\x96\xbe")));
    }
}

// Menu des presets : ceux d'usine, puis les tiens, puis Enregistrer / Supprimer.
void StutterMainView::showPresetMenu()
{
    const auto list = proc.getPresets();
    const auto current = proc.getPresetName();
    PopupMenu m, del;
    m.setLookAndFeel (&lnf);
    bool anyUser = false;
    for (int i = 0; i < (int) list.size(); ++i)
    {
        if (! list[(size_t) i].factory && ! anyUser)
        {
            m.addSeparator();
            m.addSectionHeader ("My presets");
            anyUser = true;
        }
        m.addItem (1 + i, list[(size_t) i].name, true, list[(size_t) i].name == current);
        if (! list[(size_t) i].factory)
            del.addItem (1001 + i, list[(size_t) i].name);
    }
    m.addSeparator();
    m.addItem (2000, fr ("Save preset…"));
    if (anyUser)
        m.addSubMenu ("Delete preset", del);
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (presetButton), [this] (int id)
    {
        if (id >= 1 && id < 1000)        proc.loadPreset (id - 1);
        else if (id > 1000 && id < 2000) proc.deleteUserPreset (id - 1001);
        else if (id == 2000)             askPresetName();
        refreshPresetLabel();
    });
}

void StutterMainView::askPresetName()
{
    auto* w = new AlertWindow ("Save preset", "Name your preset.", MessageBoxIconType::NoIcon, this);
    w->setLookAndFeel (&lnf);
    const auto current = proc.getPresetName();
    bool isUser = false;
    for (auto& p : proc.getPresets())
        isUser = isUser || (! p.factory && p.name == current);
    w->addTextEditor ("name", isUser ? current : String(), {});
    w->addButton ("Save", 1, KeyPress (KeyPress::returnKey));
    w->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
    w->enterModalState (true, ModalCallbackFunction::create ([this, w] (int result)
    {
        if (result == 1)
            proc.saveUserPreset (w->getTextEditorContents ("name"));
        refreshPresetLabel();
    }), true);
}

void StutterMainView::chooseFile()
{
    chooser = std::make_unique<FileChooser> ("Choose a vocal", File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.m4a;*.ogg");
    chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this] (const FileChooser& fc)
    {
        auto f = fc.getResult();
        if (f.existsAsFile())
            proc.renderFromFile (f);
    });
}

void StutterMainView::showHelp()
{
    help = std::make_unique<HelpOverlay>();
    help->onClose = [this] { Component::SafePointer<StutterMainView> sp (this); MessageManager::callAsync ([sp] { if (sp) sp->help.reset(); }); };
    addAndMakeVisible (*help);
    help->setBounds (getLocalBounds());
}

void StutterMainView::showSettings()
{
    settings = std::make_unique<SettingsOverlay>();
    auto& pf = SwellSettings::file();
    settings->setCurrent (SwellColours::accent(), (float) pf.getDoubleValue ("uiScale", 1.0), pf.getBoolValue ("tooltips", true));
    settings->onAccent   = [this] (Colour c) { applyAccent (c); };
    settings->onScale    = [this] (float s) { if (onScaleChange) onScaleChange (s); };
    settings->onTooltips = [this] (bool b) { if (onTooltipsChange) onTooltipsChange (b); };
    settings->onClose = [this] { Component::SafePointer<StutterMainView> sp (this); MessageManager::callAsync ([sp] { if (sp) sp->settings.reset(); }); };
    addAndMakeVisible (*settings);
    settings->setBounds (getLocalBounds());
}

void StutterMainView::applyAccent (Colour c)
{
    SwellColours::setAccent (c);
    SwellSettings::file().setValue ("accent", c.toString());
    lnf.refreshColours();
    std::function<void (Component&)> refresh = [&] (Component& comp) { comp.sendLookAndFeelChange(); comp.repaint(); };
    refresh (getTopLevelComponent() != nullptr ? *getTopLevelComponent() : *this);
}

static bool isAudioFile (const String& f) { return File (f).hasFileExtension ("wav;aif;aiff;flac;mp3;m4a;ogg"); }

bool StutterMainView::isInterestedInFileDrag (const StringArray& files)
{
    for (auto& f : files)
        if (isAudioFile (f))
            return true;
    return false;
}

void StutterMainView::filesDropped (const StringArray& files, int, int)
{
    clip.setHighlight (false);
    for (auto& f : files)
        if (isAudioFile (f)) { proc.renderFromFile (File (f)); break; }
}

void StutterMainView::timerCallback()
{
    if (const int v = proc.getRenderVersion(); v != lastRenderVersion)
    {
        lastRenderVersion = v;
        const auto info = proc.getRenderInfo();
        clip.setInfo (info);
        const bool ready = info.file.existsAsFile();
        clip.setTooltip (ready ? "Click a word in the strip to hear it and chop it. Drag the clip into your playlist." : String());
        playButton.setEnabled (ready);
        dragButton.setEnabled (ready);
    }

    refreshPresetLabel();

    if (proc.getHostBpm() != lastBpm)
    {
        lastBpm = proc.getHostBpm();
        repaint (0, 0, getWidth(), 60);
    }

    const bool playing = proc.isPreviewPlaying();
    const String playText = playing && ! proc.isWordPlaying() ? "Stop" : "Preview";
    if (playButton.getButtonText() != playText)
        playButton.setButtonText (playText);
    clip.setPlayhead (playing && ! proc.isWordPlaying() ? proc.getPreviewProgress() : -1.0f);
    clip.setWordPlaying (proc.isWordPlaying());

    ++blink;
    String capText = "Capture";
    bool recording = false;
    switch (proc.getCaptureState())
    {
        case StutterAudioProcessor::CaptureState::idle:      break;
        case StutterAudioProcessor::CaptureState::armed:     capText = (blink / 12) % 2 ? fr ("Waiting for play…") : fr ("Press play…"); break;
        case StutterAudioProcessor::CaptureState::recording: capText = "Stop  " + String (proc.getCaptureSeconds(), 1) + " s"; recording = true; break;
    }
    if (captureButton.getButtonText() != capText)
        captureButton.setButtonText (capText);
    captureButton.setRecording (recording);
}

void StutterMainView::paint (Graphics& g)
{
    g.fillAll (SwellColours::bg);

    auto header = getLocalBounds().removeFromTop (60).reduced (24, 0);
    if (logoS.isValid())
    {
        g.drawImage (logoS, header.removeFromLeft (26).withSizeKeepingCentre (26, 24).toFloat(), RectanglePlacement::centred);
        header.removeFromLeft (10);
    }
    const Font title (FontOptions (19.0f, Font::bold));
    g.setColour (SwellColours::text);
    g.setFont (title);
    g.drawText ("Swell", header.removeFromLeft ((int) GlyphArrangement::getStringWidth (title, "Swell") + 2), Justification::centredLeft);
    header.removeFromLeft (8);
    g.setColour (SwellColours::dim);
    g.setFont (FontOptions (15.0f));
    g.drawText ("Stutter", header.translated (0, 1), Justification::centredLeft);

    // Tempo suivi, discret, à gauche du menu
    g.setFont (FontOptions (13.0f));
    g.drawText (String (proc.getHostBpm(), proc.getHostBpm() == std::floor (proc.getHostBpm()) ? 0 : 1) + " BPM",
                getLocalBounds().removeFromTop (60).reduced (24, 0).withTrimmedRight (48), Justification::centredRight);
}

void StutterMainView::resized()
{
    auto area = getLocalBounds().reduced (24, 0);

    auto header = area.removeFromTop (60);
    menuButton.setBounds (header.removeFromRight (34).withSizeKeepingCentre (34, 30));
    presetButton.setBounds (header.withSizeKeepingCentre (190, 30));   // au centre de l'en-tête, discret

    clip.setBounds (area.removeFromTop (200));
    area.removeFromTop (12);

    auto actions = area.removeFromTop (40);
    const int bw = (actions.getWidth() - 20) / 3;
    captureButton.setBounds (actions.removeFromLeft (bw));
    actions.removeFromLeft (10);
    playButton.setBounds (actions.removeFromLeft (bw));
    actions.removeFromLeft (10);
    dragButton.setBounds (actions);

    auto footer = area.removeFromBottom (36);
    moreButton.setBounds (footer.removeFromLeft (90).withSizeKeepingCentre (90, 24));
    fxButton.setBounds (footer.removeFromLeft (80).withSizeKeepingCentre (80, 24));

    area.removeFromTop (14);
    const int cw = area.getWidth() / 5;
    auto place = [&] (Knob& k, Rectangle<int> cell)
    {
        k.label.setBounds (cell.removeFromTop (18));
        k.slider.setBounds (cell.withSizeKeepingCentre (cw, jmin (cell.getHeight(), 92)));
    };
    auto row = area;
    for (auto* k : mainKnobs)
        place (*k, row.removeFromLeft (cw));
    row = area;
    for (auto* k : moreKnobs)
        place (*k, row.removeFromLeft (cw));
    row = area;
    for (auto* k : fxKnobs)
        place (*k, row.removeFromLeft (cw));

    if (help != nullptr)     help->setBounds (getLocalBounds());
    if (settings != nullptr) settings->setBounds (getLocalBounds());
}

// ============================================================ Éditeur
StutterAudioProcessorEditor::StutterAudioProcessorEditor (StutterAudioProcessor& p)
    : AudioProcessorEditor (&p), view ([&]() -> StutterAudioProcessor&
      {
          auto saved = SwellSettings::file().getValue ("accent");
          if (saved.isNotEmpty())
              SwellColours::setAccent (Colour::fromString (saved));
          lnf.refreshColours();
          return p;
      }(), lnf)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (view);
    view.onScaleChange    = [this] (float s) { setUIScale (s); };
    view.onTooltipsChange = [this] (bool b) { SwellSettings::file().setValue ("tooltips", b); setTooltipsEnabled (b); };
    setTooltipsEnabled (SwellSettings::file().getBoolValue ("tooltips", true));

    setResizable (true, true);
    if (auto* c = getConstrainer())
    {
        c->setFixedAspectRatio ((double) StutterMainView::designW / StutterMainView::designH);
        c->setSizeLimits (StutterMainView::designW * 3 / 4, StutterMainView::designH * 3 / 4,
                          StutterMainView::designW * 2,     StutterMainView::designH * 2);
    }
    const float s = jlimit (0.75f, 2.0f, (float) SwellSettings::file().getDoubleValue ("uiScale", 1.0));
    setSize (roundToInt (StutterMainView::designW * s), roundToInt (StutterMainView::designH * s));
}

StutterAudioProcessorEditor::~StutterAudioProcessorEditor()
{
    tooltipWindow.reset();
    setLookAndFeel (nullptr);
}

void StutterAudioProcessorEditor::setTooltipsEnabled (bool b)
{
    if (b && tooltipWindow == nullptr)
    {
        tooltipWindow = std::make_unique<TooltipWindow> (this, 800);
        tooltipWindow->setLookAndFeel (&lnf);
    }
    else if (! b)
        tooltipWindow.reset();
}

void StutterAudioProcessorEditor::setUIScale (float s)
{
    setSize (roundToInt (StutterMainView::designW * s), roundToInt (StutterMainView::designH * s));
}

void StutterAudioProcessorEditor::resized()
{
    const float s = (float) getWidth() / (float) StutterMainView::designW;
    view.setBounds (0, 0, StutterMainView::designW, StutterMainView::designH);
    view.setTransform (AffineTransform::scale (s));
    SwellSettings::file().setValue ("uiScale", s);
}
