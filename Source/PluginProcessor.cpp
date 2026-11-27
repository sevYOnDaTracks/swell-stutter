#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <juce_audio_formats/juce_audio_formats.h>

static juce::String fr (const char* utf8) { return juce::String (juce::CharPointer_UTF8 (utf8)); }

const juce::StringArray& StutterAudioProcessor::rateNames()
{
    static const juce::StringArray n { "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32" };
    return n;
}

double StutterAudioProcessor::rateBeats (int i)
{
    static constexpr double b[] = { 1.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 };
    return b[juce::jlimit (0, (int) std::size (b) - 1, i)];
}

StutterAudioProcessor::StutterAudioProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "SwellStutter", createLayout())
{
    pRate    = apvts.getRawParameterValue (ParamID::rate);
    pRepeats = apvts.getRawParameterValue (ParamID::repeats);
    pBuild   = apvts.getRawParameterValue (ParamID::build);
    pPitch   = apvts.getRawParameterValue (ParamID::pitch);
    pGate    = apvts.getRawParameterValue (ParamID::gate);
    pWord    = apvts.getRawParameterValue (ParamID::word);
    pOutput  = apvts.getRawParameterValue (ParamID::output);
    pFadeIn  = apvts.getRawParameterValue (ParamID::fadeIn);
    pPan     = apvts.getRawParameterValue (ParamID::pan);
    pChorus  = apvts.getRawParameterValue (ParamID::chorus);
    pPhone   = apvts.getRawParameterValue (ParamID::phone);
    pBackverb = apvts.getRawParameterValue (ParamID::backverb);
    startTimerHz (20);
}

StutterAudioProcessor::~StutterAudioProcessor()
{
    stopTimer();
    renderPool.removeAllJobs (false, 10000);
}

juce::AudioProcessorValueTreeState::ParameterLayout StutterAudioProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    auto pct = [] (float v, int) { return String (roundToInt (v * 100)) + " %"; };

    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ParamID::rate, 1 }, "Rate", rateNames(), 3));
    p.push_back (std::make_unique<AudioParameterInt> (ParameterID { ParamID::repeats, 1 }, "Repeats", 1, 16, 4,
        AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return String (v) + fr (" ×"); })));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ParamID::build, 1 }, "Build",
        NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f, AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ParamID::pitch, 1 }, "Pitch",
        NormalisableRange<float> (0.0f, 12.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        { return v < 0.05f ? String ("Off") : "+" + String (v, v < 10.0f ? 1 : 0) + " st"; })));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ParamID::gate, 1 }, "Gate",
        NormalisableRange<float> (0.25f, 1.0f, 0.001f), 0.85f, AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    p.push_back (std::make_unique<AudioParameterInt> (ParameterID { ParamID::word, 1 }, "Word", 1, 32, 1,
        AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int)
        { static const char* n[] = { "1st", "2nd", "3rd" }; return v <= 3 ? String (n[v - 1]) : String (v) + "th"; })));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ParamID::output, 1 }, "Output",
        NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; })));
    auto amount = [] (float v, int) { return v < 0.005f ? String ("Off") : String (roundToInt (v * 100)) + " %"; };
    for (auto [id, name] : { std::pair<const char*, const char*> { ParamID::fadeIn, "Fade in" }, { ParamID::pan, "Pan" },
                             { ParamID::chorus, "Chorus" }, { ParamID::phone, "Phone" }, { ParamID::backverb, "Backverb" } })
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f,
                                                            AudioParameterFloatAttributes().withStringFromValueFunction (amount)));
    return { p.begin(), p.end() };
}

bool StutterAudioProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return l.getMainInputChannelSet() == out;
}

void StutterAudioProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate;
    const int capLen = (int) (maxCaptureSeconds * sampleRate);
    if (captureBuffer.getNumSamples() != capLen)
    {
        captureState.store ((int) CaptureState::idle);
        captureBuffer.setSize (2, capLen);
    }
}

void StutterAudioProcessor::timerCallback()
{
    if (captureDone.exchange (false))
        finishCapture();

    // Re-rendu automatique quand un réglage ou le tempo change (anti-rebond ~150 ms)
    if (lastSource != nullptr && jobsRunning.load() == 0)
    {
        const auto key = renderKey();
        if (key != pendingRenderKey)
        {
            pendingRenderKey = key;
            renderKeyTicks = 0;
        }
        else if (renderKeyTicks >= 0 && ++renderKeyTicks >= 3)
        {
            renderKeyTicks = -1;
            bool upToDate;
            {
                const juce::ScopedLock sl (renderLock);
                upToDate = key == lastRenderedKey;
            }
            if (! upToDate)
                renderFromBuffer (*lastSource, lastSourceRate, lastSourceName);
        }
    }
}

juce::String StutterAudioProcessor::renderKey() const
{
    const auto s = currentSettings();
    juce::String k;
    k << s.rateBeats << "|" << s.repeats << "|" << s.build << "|" << s.pitch << "|" << s.gate << "|"
      << s.word << "|" << s.gainDb << "|" << juce::roundToInt (s.bpm * 10) << "|" << s.fadeIn << "|" << s.pan << "|"
      << s.chorus << "|" << s.phone << "|" << s.backverb;
    return k;
}

StutterEngine::Settings StutterAudioProcessor::currentSettings() const
{
    StutterEngine::Settings s;
    s.bpm       = juce::jlimit (20.0, 999.0, hostBpm.load());
    s.rateBeats = rateBeats ((int) pRate->load());
    s.repeats   = (int) pRepeats->load();
    s.build     = pBuild->load();
    s.pitch     = pPitch->load();
    s.gate      = pGate->load();
    s.word      = (int) pWord->load();
    s.gainDb    = pOutput->load();
    s.fadeIn    = pFadeIn->load();
    s.pan       = pPan->load();
    s.chorus    = pChorus->load();
    s.phone     = pPhone->load();
    s.backverb  = pBackverb->load();
    return s;
}

void StutterAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    if (! licence->isUnlocked()) { buffer.clear(); return; }
    juce::ScopedNoDenormals noDenormals;

    bool hostPlaying = false, hostKnowsPlaying = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto bpm = pos->getBpm())
                if (*bpm > 0.0)
                    hostBpm.store (*bpm);
            hostPlaying = pos->getIsPlaying();
            hostKnowsPlaying = true;
        }

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = juce::jmin (2, buffer.getNumChannels());
    if (numSamples == 0 || numChannels == 0)
        return;

    // --- Capture de l'entrée : démarre au premier son, s'arrête avec la lecture ---
    auto state = (CaptureState) captureState.load (std::memory_order_acquire);
    if (state != CaptureState::idle && captureBuffer.getNumSamples() > 0)
    {
        int start = 0;
        if (state == CaptureState::armed)
        {
            start = numSamples;
            for (int n = 0; n < numSamples && start == numSamples; ++n)
                for (int ch = 0; ch < numChannels; ++ch)
                    if (std::abs (buffer.getSample (ch, n)) > 0.001f) { start = n; break; }
            if (start < numSamples)
            {
                start = juce::jmax (0, start - (int) (0.010 * currentSampleRate));
                state = CaptureState::recording;
                captureState.store ((int) state, std::memory_order_release);
            }
        }
        if (state == CaptureState::recording)
        {
            const int w = captureWrite.load();
            const int n = juce::jmin (numSamples - start, captureBuffer.getNumSamples() - w);
            if (n > 0)
            {
                captureBuffer.copyFrom (0, w, buffer, 0, start, n);
                captureBuffer.copyFrom (1, w, buffer, numChannels > 1 ? 1 : 0, start, n);
                captureWrite.store (w + n);
            }
            const bool full    = captureWrite.load() >= captureBuffer.getNumSamples();
            const bool stopped = hostKnowsPlaying && hostWasPlaying && ! hostPlaying;
            if (full || stopped || captureStopRequest.load())
            {
                captureStopRequest.store (false);
                captureState.store ((int) CaptureState::idle, std::memory_order_release);
                captureDone.store (true);
            }
        }
    }
    hostWasPlaying = hostPlaying;

    // Le son traverse sans changement ; la pré-écoute du clip s'ajoute par-dessus.
    if (previewPlaying.load())
    {
        const juce::SpinLock::ScopedTryLockType lock (previewLock);
        if (lock.isLocked())
        {
            const int len = previewBuffer.getNumSamples();
            const int n = juce::jmin (numSamples, len - previewPos);
            if (n > 0)
                for (int ch = 0; ch < numChannels; ++ch)
                    buffer.addFrom (ch, 0, previewBuffer, numChannels == 1 ? 0 : ch, previewPos, n,
                                    numChannels == 1 ? 1.0f : 1.0f);
            previewPos += juce::jmax (0, n);
            if (previewPos >= len)
                previewPlaying.store (false);
            previewPosAtomic.store (previewPos);
        }
    }
}

// ============================================================ Pré-écoute
void StutterAudioProcessor::togglePreview()
{
    if (previewPlaying.load() && ! previewIsWord.load()) { previewPlaying.store (false); return; }
    previewPlaying.store (false);
    {
        const juce::SpinLock::ScopedLockType lock (previewLock);
        if (previewIsWord.load())
        {
            previewBuffer = clipPreview;
            previewIsWord.store (false);
            previewLength.store (previewBuffer.getNumSamples());
        }
        if (previewBuffer.getNumSamples() == 0)
            return;
        previewPos = 0;
        previewPosAtomic.store (0);
    }
    previewPlaying.store (true);
}

float StutterAudioProcessor::getPreviewProgress() const
{
    const int len = previewLength.load();
    return len > 0 ? (float) previewPosAtomic.load() / (float) len : 0.0f;
}

void StutterAudioProcessor::buildPreview (const juce::AudioBuffer<float>& clip, double sr)
{
    juce::AudioBuffer<float> out;
    const double hostSr = currentSampleRate;
    if (std::abs (sr - hostSr) < 1.0)
        out = clip;
    else
    {
        const double ratio = sr / hostSr;
        const int outLen = (int) std::floor (clip.getNumSamples() / ratio);
        out.setSize (2, juce::jmax (1, outLen));
        for (int ch = 0; ch < 2; ++ch)
        {
            juce::LagrangeInterpolator interp;
            interp.process (ratio, clip.getReadPointer (ch), out.getWritePointer (ch), outLen);
        }
    }
    previewPlaying.store (false);
    const juce::SpinLock::ScopedLockType lock (previewLock);
    clipPreview = out;
    std::swap (previewBuffer, out);
    previewIsWord.store (false);
    previewPos = 0;
    previewPosAtomic.store (0);
    previewLength.store (previewBuffer.getNumSamples());
}

void StutterAudioProcessor::selectWord (int index)
{
    {
        const juce::ScopedLock sl (renderLock);
        if (index < 0 || index >= (int) lastWords.size())
            return;
    }
    if (auto* prm = apvts.getParameter (ParamID::word))
    {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 ((float) (index + 1)));
        prm->endChangeGesture();
    }
    playWord (index);
}

void StutterAudioProcessor::playWord (int index)
{
    StutterEngine::Word w;
    {
        const juce::ScopedLock sl (renderLock);
        if (lastSource == nullptr || index < 0 || index >= (int) lastWords.size())
            return;
        w = lastWords[(size_t) index];
    }
    const int len = juce::jmax (1, w.end - w.start);
    juce::AudioBuffer<float> word (2, len);
    const int chs = lastSource->getNumChannels();
    const int fade = juce::jmin (len / 2, (int) (0.005 * lastSourceRate));
    for (int ch = 0; ch < 2; ++ch)
    {
        word.copyFrom (ch, 0, *lastSource, juce::jmin (ch, chs - 1), w.start, juce::jmin (len, lastSource->getNumSamples() - w.start));
        word.applyGainRamp (ch, len - fade, fade, 1.0f, 0.0f);
    }
    juce::AudioBuffer<float> out;
    if (std::abs (lastSourceRate - currentSampleRate) < 1.0)
        out = word;
    else
    {
        const double ratio = lastSourceRate / currentSampleRate;
        const int outLen = juce::jmax (1, (int) std::floor (len / ratio));
        out.setSize (2, outLen);
        for (int ch = 0; ch < 2; ++ch)
        {
            juce::LagrangeInterpolator interp;
            interp.process (ratio, word.getReadPointer (ch), out.getWritePointer (ch), outLen);
        }
    }
    previewPlaying.store (false);
    {
        const juce::SpinLock::ScopedLockType lock (previewLock);
        std::swap (previewBuffer, out);
        previewIsWord.store (true);
        previewPos = 0;
        previewPosAtomic.store (0);
        previewLength.store (previewBuffer.getNumSamples());
    }
    previewPlaying.store (true);
}

// ============================================================ Capture + rendu
juce::File StutterAudioProcessor::getRenderFolder()
{
    // Banc de test : dossier séparé pour ne pas remplir Music/Swell Stutter
    const auto overrideDir = juce::SystemStats::getEnvironmentVariable ("SWELL_STUTTER_FOLDER", {});
    if (overrideDir.isNotEmpty())
        return juce::File (overrideDir);
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Swell Stutter");
}

void StutterAudioProcessor::toggleCapture()
{
    const auto state = getCaptureState();
    if (state == CaptureState::idle)
    {
        if (captureBuffer.getNumSamples() == 0)
        {
            setRenderStatus ("Start playback once before capturing", false);
            return;
        }
        captureStopRequest.store (false);
        captureDone.store (false);
        captureWrite.store (0);
        captureState.store ((int) CaptureState::armed, std::memory_order_release);
        setRenderStatus ("Capture armed: press play in your DAW", false);
    }
    else if (state == CaptureState::armed)
    {
        captureState.store ((int) CaptureState::idle);
        setRenderStatus ("Capture cancelled", false);
    }
    else
        captureStopRequest.store (true);
}

float StutterAudioProcessor::getCaptureSeconds() const
{
    return (float) (captureWrite.load() / juce::jmax (1.0, currentSampleRate));
}

void StutterAudioProcessor::finishCapture()
{
    const int n = captureWrite.load();
    if (n < (int) (0.05 * currentSampleRate))
    {
        setRenderStatus ("Capture too short", false);
        return;
    }
    juce::AudioBuffer<float> src (2, n);
    for (int ch = 0; ch < 2; ++ch)
        src.copyFrom (ch, 0, captureBuffer, ch, 0, n);
    lastSourceFile = juce::File();
    renderFromBuffer (src, currentSampleRate, "vocal");
}

void StutterAudioProcessor::renderFromFile (const juce::File& f)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (f));
    if (reader == nullptr)
    {
        setRenderStatus ("Unsupported format: " + f.getFileName(), false);
        return;
    }
    const int len = (int) juce::jmin (reader->lengthInSamples, (juce::int64) (maxCaptureSeconds * reader->sampleRate));
    juce::AudioBuffer<float> src ((int) juce::jlimit (1u, 2u, reader->numChannels), juce::jmax (1, len));
    reader->read (&src, 0, len, 0, true, true);
    lastSourceFile = f;
    renderFromBuffer (src, reader->sampleRate, f.getFileNameWithoutExtension());
}

static bool writeWav (const juce::File& file, const juce::AudioBuffer<float>& b, double sr)
{
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (std::make_unique<juce::FileOutputStream> (file));
    if (auto* fos = dynamic_cast<juce::FileOutputStream*> (os.get()); fos == nullptr || ! fos->openedOk())
        return false;
    auto opts = juce::AudioFormatWriterOptions{}.withSampleRate (sr).withNumChannels (b.getNumChannels()).withBitsPerSample (24);
    if (auto writer = wav.createWriterFor (os, opts))
        return writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    return false;
}

void StutterAudioProcessor::renderFromBuffer (const juce::AudioBuffer<float>& source, double sr, const juce::String& name)
{
    const auto settings = currentSettings();
    const auto key = renderKey();
    const int  rateIdx = (int) pRate->load();
    setRenderStatus (fr ("Rendering…"), true);

    auto srcCopy = (lastSource != nullptr && lastSource.get() == &source) ? lastSource
                                                                         : std::make_shared<juce::AudioBuffer<float>> (source);
    lastSource      = srcCopy;
    lastSourceRate  = sr;
    lastSourceName  = name;
    pendingRenderKey = key;
    renderKeyTicks  = -1;
    const bool saveSource = ! lastSourceFile.existsAsFile();

    ++jobsRunning;
    renderPool.addJob ([this, srcCopy, sr, settings, key, rateIdx, name, saveSource]
    {
        auto res = StutterEngine::render (*srcCopy, sr, settings);
        auto words = StutterEngine::findWords (*srcCopy, sr);
        auto srcPeaks = StutterEngine::peaks (*srcCopy, 600);

        auto folder = getRenderFolder();
        folder.createDirectory();
        const auto stamp = juce::Time::getCurrentTime().formatted ("%H%M%S")
                         + juce::String (juce::Time::getCurrentTime().getMilliseconds()).paddedLeft ('0', 3);
        const auto safeName = juce::File::createLegalFileName (name).replaceCharacter (' ', '_');

        // La voix d'origine est gardée pour retrouver le clip à la réouverture du projet.
        if (saveSource)
        {
            auto srcFolder = folder.getChildFile ("Sources");
            srcFolder.createDirectory();
            auto f = srcFolder.getChildFile (safeName + "_" + stamp + ".wav");
            if (writeWav (f, *srcCopy, sr))
            {
                const juce::ScopedLock sl (renderLock);
                lastSourceFile = f;
            }
        }

        juce::File file;
        bool ok = false;
        if (res.ok)
        {
            file = folder.getChildFile ("Stutter_" + safeName + "_" + rateNames()[rateIdx].replace ("/", "-") + "x"
                                        + juce::String (settings.repeats) + "_" + juce::String (juce::roundToInt (settings.bpm))
                                        + "bpm_" + stamp + ".wav");
            ok = writeWav (file, res.clip, sr);
            if (ok)
                buildPreview (res.clip, sr);
        }

        {
            const juce::ScopedLock sl (renderLock);
            // L'ancien clip, jamais glissé dans la playlist, ne sert plus : on ne remplit pas le dossier.
            if (ok && renderInfo.file.existsAsFile() && ! lastFileDragged)
                renderInfo.file.deleteFile();
            renderInfo.busy = false;
            if (ok)
            {
                lastFileDragged = false;
                lastRenderedKey = key;
                lastResult = res;
                renderInfo.file = file;
                renderInfo.peaks = StutterEngine::peaks (res.clip, 1200);
                const float len = (float) res.clip.getNumSamples();
                renderInfo.slotMarks.clear();
                for (int s : res.slotStarts)
                    renderInfo.slotMarks.push_back ((float) (res.clipOnset + s) / len);
                renderInfo.stutterEndFrac = (float) res.stutterEnd / len;
                const float srcLen = (float) juce::jmax (1, srcCopy->getNumSamples());
                renderInfo.sourcePeaks = srcPeaks;
                renderInfo.words.clear();
                for (auto& w : words)
                    renderInfo.words.push_back ({ (float) w.start / srcLen, (float) w.end / srcLen });
                renderInfo.wordIndex = res.wordIndex;
                lastWords = words;
                renderInfo.status = "Word " + juce::String (juce::jmin (settings.word, res.wordsFound)) + " of "
                                  + juce::String (res.wordsFound) + fr ("  ·  ") + juce::String (settings.repeats) + fr (" × ")
                                  + rateNames()[rateIdx] + " at " + juce::String (juce::roundToInt (settings.bpm)) + " BPM"
                                  + (res.preroll > 0 ? fr ("  ·  Backverb: drop the clip 1 beat early") : juce::String());
            }
            else
            {
                lastResult = res;
                renderInfo.file = juce::File();
                renderInfo.peaks.clear();
                renderInfo.slotMarks.clear();
                renderInfo.sourcePeaks.clear();
                renderInfo.words.clear();
                renderInfo.wordIndex = -1;
                renderInfo.status = res.ok ? "Could not write to Music/Swell Stutter" : "No voice found in this sound";
            }
            ++renderVersion;
        }
        --jobsRunning;
    });
}

void StutterAudioProcessor::markDragged()
{
    const juce::ScopedLock sl (renderLock);
    lastFileDragged = true;
}

bool StutterAudioProcessor::waitForRender (int timeoutMs)
{
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    while (jobsRunning.load() > 0 && juce::Time::getMillisecondCounter() < end)
        juce::Thread::sleep (5);
    return jobsRunning.load() == 0;
}

StutterEngine::Result StutterAudioProcessor::getLastResult() const
{
    const juce::ScopedLock sl (renderLock);
    return lastResult;
}

void StutterAudioProcessor::setRenderStatus (const juce::String& s, bool busy)
{
    const juce::ScopedLock sl (renderLock);
    renderInfo.status = s;
    renderInfo.busy = busy;
    ++renderVersion;
}

StutterAudioProcessor::RenderInfo StutterAudioProcessor::getRenderInfo() const
{
    const juce::ScopedLock sl (renderLock);
    return renderInfo;
}

// ============================================================ Presets
namespace
{
    struct FactoryPreset { const char* name; std::map<juce::String, float> values; };

    // Valeurs « réelles » des réglages (Rate : index dans 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32).
    // Ce qui n'est pas cité reprend sa valeur par défaut.
    const std::vector<FactoryPreset>& factoryPresets()
    {
        static const std::vector<FactoryPreset> list {
            { "Default",        {} },
            { "Tight 1/32",     { { ParamID::rate, 5 }, { ParamID::repeats, 8 }, { ParamID::gate, 0.7f } } },
            { "Triplet roll",   { { ParamID::rate, 4 }, { ParamID::repeats, 6 }, { ParamID::gate, 0.9f } } },
            { "Slow 1/8",       { { ParamID::rate, 1 }, { ParamID::repeats, 3 }, { ParamID::gate, 0.6f } } },
            { "Build up",       { { ParamID::rate, 3 }, { ParamID::repeats, 8 }, { ParamID::build, 0.7f }, { ParamID::fadeIn, 0.5f } } },
            { "Riser +12",      { { ParamID::rate, 3 }, { ParamID::repeats, 8 }, { ParamID::build, 0.5f }, { ParamID::pitch, 12.0f }, { ParamID::fadeIn, 0.4f } } },
            { "Ping-pong",      { { ParamID::rate, 3 }, { ParamID::repeats, 8 }, { ParamID::gate, 0.8f }, { ParamID::pan, 1.0f } } },
            { "Phone stutter",  { { ParamID::rate, 3 }, { ParamID::repeats, 4 }, { ParamID::phone, 0.8f } } },
            { "Wide chorus",    { { ParamID::rate, 1 }, { ParamID::repeats, 4 }, { ParamID::chorus, 0.7f }, { ParamID::pan, 0.5f } } },
            { "Backverb intro", { { ParamID::rate, 1 }, { ParamID::repeats, 4 }, { ParamID::backverb, 0.8f } } },
        };
        return list;
    }
}

const juce::StringArray& StutterAudioProcessor::presetParamIds()
{
    // Tout sauf Word : le mot dépend de la voix, pas du preset.
    static const juce::StringArray ids { ParamID::rate, ParamID::repeats, ParamID::build, ParamID::pitch, ParamID::gate,
                                         ParamID::output, ParamID::fadeIn, ParamID::pan, ParamID::chorus, ParamID::phone, ParamID::backverb };
    return ids;
}

juce::File StutterAudioProcessor::getPresetFolder() { return getRenderFolder().getChildFile ("Presets"); }

std::vector<StutterAudioProcessor::Preset> StutterAudioProcessor::getPresets() const
{
    std::vector<Preset> list;
    for (auto& f : factoryPresets())
        list.push_back ({ f.name, true, {} });
    auto files = getPresetFolder().findChildFiles (juce::File::findFiles, false, "*.preset");
    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
               { return a.getFileNameWithoutExtension().compareNatural (b.getFileNameWithoutExtension()) < 0; });
    for (auto& f : files)
        list.push_back ({ f.getFileNameWithoutExtension(), false, f });
    return list;
}

std::map<juce::String, float> StutterAudioProcessor::currentPresetValues() const
{
    std::map<juce::String, float> v;
    for (auto& id : presetParamIds())
        if (auto* raw = apvts.getRawParameterValue (id))
            v[id] = raw->load();
    return v;
}

void StutterAudioProcessor::applyPresetValues (const std::map<juce::String, float>& values, const juce::String& name)
{
    for (auto& id : presetParamIds())
        if (auto* param = apvts.getParameter (id))
        {
            const auto it = values.find (id);
            const float norm = it != values.end() ? param->convertTo0to1 (it->second) : param->getDefaultValue();
            param->beginChangeGesture();
            param->setValueNotifyingHost (norm);
            param->endChangeGesture();
        }
    apvts.state.setProperty ("preset", name, nullptr);
    loadedPresetValues = currentPresetValues();
}

bool StutterAudioProcessor::loadPreset (int index)
{
    const auto list = getPresets();
    if (! juce::isPositiveAndBelow (index, (int) list.size()))
        return false;
    const auto& p = list[(size_t) index];
    if (p.factory)
    {
        applyPresetValues (factoryPresets()[(size_t) index].values, p.name);
        return true;
    }
    const auto xml = juce::parseXML (p.file);
    if (xml == nullptr || ! xml->hasTagName ("SwellStutterPreset"))
        return false;
    std::map<juce::String, float> values;
    for (auto* e : xml->getChildWithTagNameIterator ("PARAM"))
        if (presetParamIds().contains (e->getStringAttribute ("id")))
            values[e->getStringAttribute ("id")] = (float) e->getDoubleAttribute ("value");
    // Un réglage absent du fichier (preset d'une ancienne version) garde sa valeur actuelle
    for (auto& [id, v] : currentPresetValues())
        values.emplace (id, v);
    applyPresetValues (values, p.name);
    return true;
}

bool StutterAudioProcessor::saveUserPreset (const juce::String& rawName)
{
    const auto name = juce::File::createLegalFileName (rawName.trim());
    if (name.isEmpty())
        return false;
    juce::XmlElement xml ("SwellStutterPreset");
    xml.setAttribute ("name", name);
    xml.setAttribute ("version", SWELL_VERSION);
    for (auto& [id, v] : currentPresetValues())
    {
        auto* e = xml.createNewChildElement ("PARAM");
        e->setAttribute ("id", id);
        e->setAttribute ("value", v);
    }
    auto folder = getPresetFolder();
    folder.createDirectory();
    if (! xml.writeTo (folder.getChildFile (name + ".preset")))
        return false;
    apvts.state.setProperty ("preset", name, nullptr);
    loadedPresetValues = currentPresetValues();
    return true;
}

bool StutterAudioProcessor::deleteUserPreset (int index)
{
    const auto list = getPresets();
    if (! juce::isPositiveAndBelow (index, (int) list.size()) || list[(size_t) index].factory)
        return false;
    if (getPresetName() == list[(size_t) index].name)
    {
        apvts.state.setProperty ("preset", juce::String(), nullptr);
        loadedPresetValues.clear();
    }
    return list[(size_t) index].file.moveToTrash() || list[(size_t) index].file.deleteFile();
}

juce::String StutterAudioProcessor::getPresetName() const { return apvts.state.getProperty ("preset").toString(); }

bool StutterAudioProcessor::isPresetModified() const
{
    if (loadedPresetValues.empty())
        return false;
    for (auto& [id, v] : currentPresetValues())
    {
        const auto it = loadedPresetValues.find (id);
        if (it == loadedPresetValues.end() || std::abs (it->second - v) > 1.0e-4f)
            return true;
    }
    return false;
}

int StutterAudioProcessor::getNumPrograms() { return (int) factoryPresets().size(); }

int StutterAudioProcessor::getCurrentProgram()
{
    const auto name = getPresetName();
    for (int i = 0; i < (int) factoryPresets().size(); ++i)
        if (name == factoryPresets()[(size_t) i].name)
            return i;
    return 0;
}

void StutterAudioProcessor::setCurrentProgram (int i)
{
    // Certains DAW rappellent le programme courant à l'ouverture d'un projet : on ne touche alors à rien,
    // pour ne jamais écraser les réglages enregistrés avec le projet.
    if (i == getCurrentProgram())
        return;
    if (juce::isPositiveAndBelow (i, (int) factoryPresets().size()))
        loadPreset (i);
}

const juce::String StutterAudioProcessor::getProgramName (int i)
{
    return juce::isPositiveAndBelow (i, (int) factoryPresets().size()) ? juce::String (factoryPresets()[(size_t) i].name) : juce::String();
}

// ============================================================ État
void StutterAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    {
        const juce::ScopedLock sl (renderLock);
        state.setProperty ("source", lastSourceFile.getFullPathName(), nullptr);
        state.setProperty ("sourceName", lastSourceName, nullptr);
    }
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void StutterAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            const juce::File src (tree.getProperty ("source").toString());
            apvts.replaceState (tree);
            loadedPresetValues.clear();   // à la réouverture, le nom reste affiché tel quel
            if (src.existsAsFile())
                juce::MessageManager::callAsync ([this, src, safe = juce::WeakReference<StutterAudioProcessor> (this)]
                {
                    if (safe != nullptr) renderFromFile (src);
                });
        }
}

juce::AudioProcessorEditor* StutterAudioProcessor::createEditor()
{
    return swell::LicenceGuard::attach (new StutterAudioProcessorEditor (*this));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new StutterAudioProcessor();
}
