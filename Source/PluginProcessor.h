#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Licence/SwellLicence.h"
#include "StutterEngine.h"
#include <map>

namespace ParamID
{
    static constexpr const char* rate    = "rate";
    static constexpr const char* repeats = "repeats";
    static constexpr const char* build   = "build";
    static constexpr const char* pitch   = "pitch";
    static constexpr const char* gate    = "gate";
    static constexpr const char* word    = "word";
    static constexpr const char* output  = "output";
    static constexpr const char* fadeIn  = "fadein";
    static constexpr const char* pan     = "pan";
    static constexpr const char* chorus  = "chorus";
    static constexpr const char* phone   = "phone";
    static constexpr const char* backverb = "backverb";
}

#ifndef SWELL_VERSION
 #define SWELL_VERSION "1.2.0"
#endif

// Swell Stutter : on capture une voix (ou on dépose un sample), le plugin trouve le début du mot,
// le hache en rythme, et on glisse le clip dans la playlist. Le son qui traverse le plugin n'est pas modifié.
class StutterAudioProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    juce::SharedResourcePointer<swell::Licence> licence;

    StutterAudioProcessor();
    ~StutterAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Swell Stutter"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    // Les presets d'usine sont aussi visibles comme « programmes » dans le DAW.
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int) override;
    void changeProgramName (int, const juce::String&) override {}

    // --- Presets : d'usine, puis ceux de l'utilisateur (Music/Swell Stutter/Presets).
    // Un preset ne change que les réglages du son : jamais la voix capturée ni le mot choisi.
    struct Preset { juce::String name; bool factory = true; juce::File file; };
    std::vector<Preset> getPresets() const;
    bool loadPreset (int index);                       // index dans getPresets()
    bool saveUserPreset (const juce::String& name);    // écrase un preset utilisateur du même nom
    bool deleteUserPreset (int index);
    juce::String getPresetName() const;                // vide si aucun preset chargé
    bool isPresetModified() const;                     // un réglage a bougé depuis le chargement
    static juce::File getPresetFolder();
    static const juce::StringArray& presetParamIds();

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    juce::AudioProcessorValueTreeState apvts;

    double getHostBpm() const { return hostBpm.load(); }
    static const juce::StringArray& rateNames();
    static double rateBeats (int index);

    // --- Capture ---
    enum class CaptureState { idle = 0, armed, recording };
    void toggleCapture();
    CaptureState getCaptureState() const { return (CaptureState) captureState.load(); }
    float getCaptureSeconds() const;

    // --- Rendu du clip ---
    void renderFromFile (const juce::File& f);
    void renderFromBuffer (const juce::AudioBuffer<float>& src, double sr, const juce::String& name);
    StutterEngine::Settings currentSettings() const;

    struct RenderInfo
    {
        juce::File file;
        std::vector<float> peaks;          // (min, max) par colonne
        std::vector<float> slotMarks;      // début de chaque répétition, en fraction du clip
        float stutterEndFrac = 0.0f;
        std::vector<float> sourcePeaks;    // toute la voix capturée, (min, max) par colonne
        std::vector<std::pair<float, float>> words;   // début / fin de chaque mot, en fraction de la voix
        int wordIndex = -1;                // mot haché
        juce::String status;
        bool busy = false;
    };
    RenderInfo getRenderInfo() const;
    int getRenderVersion() const { return renderVersion.load(); }
    static juce::File getRenderFolder();
    void markDragged();   // le clip est parti dans la playlist : on le garde sur le disque

    // --- Choix du mot (clic sur la voix) : il devient le mot haché et on l'entend seul ---
    void selectWord (int index);
    void playWord (int index);
    bool isWordPlaying() const { return previewPlaying.load() && previewIsWord.load(); }

    // --- Pré-écoute ---
    void togglePreview();
    void stopPreview()            { previewPlaying.store (false); }
    bool isPreviewPlaying() const { return previewPlaying.load(); }
    float getPreviewProgress() const;

    // --- Banc de test ---
    void finishCaptureNow() { finishCapture(); }
    bool waitForRender (int timeoutMs);
    StutterEngine::Result getLastResult() const;

private:
    void finishCapture();
    void setRenderStatus (const juce::String& s, bool busy);
    void timerCallback() override;
    juce::String renderKey() const;
    void buildPreview (const juce::AudioBuffer<float>& clip, double sr);

    std::atomic<float>* pRate = nullptr;
    std::atomic<float>* pRepeats = nullptr;
    std::atomic<float>* pBuild = nullptr;
    std::atomic<float>* pPitch = nullptr;
    std::atomic<float>* pGate = nullptr;
    std::atomic<float>* pWord = nullptr;
    std::atomic<float>* pOutput = nullptr;
    std::atomic<float>* pFadeIn = nullptr;
    std::atomic<float>* pPan = nullptr;
    std::atomic<float>* pChorus = nullptr;
    std::atomic<float>* pPhone = nullptr;
    std::atomic<float>* pBackverb = nullptr;
    void applyPresetValues (const std::map<juce::String, float>& values, const juce::String& name);
    std::map<juce::String, float> currentPresetValues() const;
    std::map<juce::String, float> loadedPresetValues;   // pour savoir si le preset a été retouché

    std::vector<StutterEngine::Word> lastWords;   // mots de la voix courante (thread des messages)

    double currentSampleRate = 44100.0;
    std::atomic<double> hostBpm { 120.0 };

    // Capture
    juce::AudioBuffer<float> captureBuffer;
    std::atomic<int>  captureState { 0 };
    std::atomic<int>  captureWrite { 0 };
    std::atomic<bool> captureDone { false };
    std::atomic<bool> captureStopRequest { false };
    bool hostWasPlaying = false;
    static constexpr double maxCaptureSeconds = 30.0;

    // Rendu
    mutable juce::CriticalSection renderLock;
    RenderInfo renderInfo;
    StutterEngine::Result lastResult;
    std::atomic<int> renderVersion { 0 };
    std::atomic<int> jobsRunning { 0 };
    std::shared_ptr<juce::AudioBuffer<float>> lastSource;
    double       lastSourceRate = 44100.0;
    juce::String lastSourceName;
    juce::File   lastSourceFile;          // voix d'origine sur disque (rechargée avec le projet)
    juce::String lastRenderedKey;
    bool         lastFileDragged = false;
    juce::String pendingRenderKey;
    int          renderKeyTicks = 0;

    JUCE_DECLARE_WEAK_REFERENCEABLE (StutterAudioProcessor)

    // Pré-écoute
    juce::SpinLock previewLock;
    juce::AudioBuffer<float> previewBuffer;
    int previewPos = 0;
    std::atomic<int>  previewLength { 0 };
    std::atomic<int>  previewPosAtomic { 0 };
    std::atomic<bool> previewPlaying { false };
    std::atomic<bool> previewIsWord { false };
    juce::AudioBuffer<float> clipPreview;   // le clip, gardé pour revenir à la pré-écoute après un mot

    juce::ThreadPool renderPool { juce::ThreadPoolOptions().withNumberOfThreads (1).withThreadName ("Swell Stutter render") };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StutterAudioProcessor)
};
