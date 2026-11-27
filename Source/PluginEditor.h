#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

// ============================================================ Thème (même famille que Swell Reverse)
namespace SwellColours
{
    const juce::Colour bg       { 0xff121214 };
    const juce::Colour panel    { 0xff1b1b1f };
    const juce::Colour track    { 0xff2a2a30 };
    const juce::Colour text     { 0xffecebe8 };
    const juce::Colour dim      { 0xff8e8c94 };
    const juce::Colour label    { 0xffb9b7bf };
    const juce::Colour rec      { 0xffe5484d };

    juce::Colour accent();
    void setAccent (juce::Colour);
    struct Swatch { const char* name; juce::uint32 argb; };
    const std::vector<Swatch>& swatches();
}

namespace SwellSettings { juce::PropertiesFile& file(); }

class SwellLookAndFeel : public juce::LookAndFeel_V4
{
public:
    SwellLookAndFeel();
    void refreshColours();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int) override;
    void drawTooltip (juce::Graphics&, const juce::String&, int w, int h) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String&, juce::Point<int>, juce::Rectangle<int>) override;
    juce::Font getPopupMenuFont() override;
};

// ============================================================ Zone principale : le clip rendu, glissable
class ClipView : public juce::Component, public juce::SettableTooltipClient
{
public:
    void setInfo (const StutterAudioProcessor::RenderInfo& i) { info = i; repaint(); }
    void setHighlight (bool h) { highlight = h; repaint(); }
    void setPlayhead (float p) { if (p != playhead) { playhead = p; repaint(); } }
    bool hasClip() const { return info.file.existsAsFile(); }
    void startDrag();
    void setWordPlaying (bool p) { if (p != wordPlaying) { wordPlaying = p; repaint(); } }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    std::function<void()> onClickEmpty, onDragStarted;
    std::function<void (int)> onWordClicked;
    juce::Rectangle<float> wordLane() const;   // bande du haut : toute la voix, mot par mot
    juce::Rectangle<float> wordBox (int index) const;
private:
    int wordAt (juce::Point<float>) const;
    StutterAudioProcessor::RenderInfo info;
    bool highlight = false, dragging = false, pressInLane = false, wordPlaying = false;
    int hoverWord = -1;
    float playhead = -1.0f;
};

class ActionButton : public juce::TextButton
{
public:
    enum class Icon { record, play, drag };
    ActionButton (const juce::String& name, Icon i) : juce::TextButton (name), icon (i) {}
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    void setPrimary (bool p) { primary = p; repaint(); }
    void setRecording (bool r) { if (r != recording) { recording = r; repaint(); } }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    std::function<void()> onDragOut;
private:
    Icon icon;
    bool primary = false, recording = false, dragStarted = false;
};

class OverlayPanel : public juce::Component
{
public:
    explicit OverlayPanel (const juce::String& title);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    std::function<void()> onClose;
    void setCardHeight (int h) { cardHeight = h; }
protected:
    juce::Rectangle<int> getContentArea() const { return content; }
    virtual void layoutContent (juce::Rectangle<int>) {}
private:
    juce::String title;
    juce::TextButton closeButton { "Close" };
    juce::Rectangle<int> card, content;
    int cardHeight = 0;
};

class HelpOverlay : public OverlayPanel
{
public:
    HelpOverlay();
protected:
    void layoutContent (juce::Rectangle<int> a) override { text.setBounds (a); }
private:
    juce::TextEditor text;
};

class SettingsOverlay : public OverlayPanel
{
public:
    SettingsOverlay();
    void paint (juce::Graphics&) override;
    std::function<void (juce::Colour)> onAccent;
    std::function<void (float)> onScale;
    std::function<void (bool)> onTooltips;
    void setCurrent (juce::Colour accent, float scale, bool tooltips);
protected:
    void layoutContent (juce::Rectangle<int>) override;
private:
    juce::OwnedArray<juce::TextButton> swatchButtons, scaleButtons;
    juce::ToggleButton tooltipToggle { "Show tooltips" };
    juce::Rectangle<int> colourLabel, scaleLabel;
    juce::Colour currentAccent;
    float currentScale = 1.0f;
};

// ============================================================ Vue principale (taille de référence 640x480)
class StutterMainView : public juce::Component,
                        public juce::DragAndDropContainer,
                        public juce::FileDragAndDropTarget,
                        private juce::Timer
{
public:
    static constexpr int designW = 640, designH = 480;

    StutterMainView (StutterAudioProcessor&, SwellLookAndFeel&);
    ~StutterMainView() override;
    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { clip.setHighlight (true); }
    void fileDragExit (const juce::StringArray&) override              { clip.setHighlight (false); }
    void filesDropped (const juce::StringArray& files, int, int) override;

    std::function<void (float)> onScaleChange;
    std::function<void (bool)>  onTooltipsChange;

    void showHelp();
    void showSettings();
    void setMoreOpen (bool open) { setPage (open ? 1 : 0); }
    void setFxOpen (bool open)   { setPage (open ? 2 : 0); }
    void setPage (int page);     // 0 : stutter, 1 : More, 2 : FX
    void refreshNow() { timerCallback(); }
    void openPresetMenu() { showPresetMenu(); }   // banc de test

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label  label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    void timerCallback() override;
    void setupKnob (Knob&, const juce::String& paramId, const juce::String& name, const juce::String& tip);
    void showMainMenu();
    void showPresetMenu();
    void askPresetName();
    void refreshPresetLabel();
    void chooseFile();
    void applyAccent (juce::Colour c);

    StutterAudioProcessor& proc;
    SwellLookAndFeel& lnf;
    juce::Image logoS;

    Knob rate, repeats, build, pitch, gate, word, output, fadeIn, pan, chorus, phone, backverb;
    std::array<Knob*, 5> mainKnobs { &rate, &repeats, &build, &pitch, &gate };
    std::array<Knob*, 4> moreKnobs { &word, &fadeIn, &pan, &output };
    std::array<Knob*, 3> fxKnobs { &chorus, &phone, &backverb };
    int page = 0;

    juce::ToggleButton moreButton { "More" };
    juce::ToggleButton fxButton { "FX" };
    juce::TextButton menuButton;
    juce::TextButton presetButton;
    juce::String presetLabel;

    ClipView clip;
    ActionButton captureButton { "Capture", ActionButton::Icon::record };
    ActionButton playButton    { "Preview", ActionButton::Icon::play };
    ActionButton dragButton    { "Drag to playlist", ActionButton::Icon::drag };
    std::unique_ptr<juce::FileChooser> chooser;

    std::unique_ptr<HelpOverlay> help;
    std::unique_ptr<SettingsOverlay> settings;
    int lastRenderVersion = -1, blink = 0;
    double lastBpm = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StutterMainView)
};

class StutterAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit StutterAudioProcessorEditor (StutterAudioProcessor&);
    ~StutterAudioProcessorEditor() override;
    void resized() override;
    void setUIScale (float s);
    StutterMainView& getView() { return view; }

private:
    SwellLookAndFeel lnf;
    StutterMainView view;
    std::unique_ptr<juce::TooltipWindow> tooltipWindow;
    void setTooltipsEnabled (bool b);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StutterAudioProcessorEditor)
};
