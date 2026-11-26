#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Licence/SwellLicence.h"
#include "StutterEngine.h"

namespace ParamID
{
    static constexpr const char* rate    = "rate";
    static constexpr const char* repeats = "repeats";
    static constexpr const char* build   = "build";
    static constexpr const char* pitch   = "pitch";
    static constexpr const char* gate    = "gate";
    static constexpr const char* word    = "word";
    static constexpr const char* output  = "output";
}

#ifndef SWELL_VERSION
 #define SWELL_VERSION "1.0.0"
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

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

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
        juce::String status;
        bool busy = false;
    };
    RenderInfo getRenderInfo() const;
    int getRenderVersion() const { return renderVersion.load(); }
    static juce::File getRenderFolder();
    void markDragged();   // le clip est parti dans la playlist : on le garde sur le disque

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

    juce::ThreadPool renderPool { juce::ThreadPoolOptions().withNumberOfThreads (1).withThreadName ("Swell Stutter render") };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StutterAudioProcessor)
};
