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
    p.push_back (std::make_unique<AudioParameterInt> (ParameterID { ParamID::word, 1 }, "Word", 1, 8, 1,
        AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int)
        { static const char* n[] = { "1st", "2nd", "3rd" }; return v <= 3 ? String (n[v - 1]) : String (v) + "th"; })));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ParamID::output, 1 }, "Output",
        NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; })));
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
      << s.word << "|" << s.gainDb << "|" << juce::roundToInt (s.bpm * 10);
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
    if (previewPlaying.load()) { previewPlaying.store (false); return; }
    {
        const juce::SpinLock::ScopedLockType lock (previewLock);
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
    std::swap (previewBuffer, out);
    previewPos = 0;
    previewPosAtomic.store (0);
    previewLength.store (previewBuffer.getNumSamples());
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
                renderInfo.status = "Word " + juce::String (juce::jmin (settings.word, res.wordsFound)) + " of "
                                  + juce::String (res.wordsFound) + fr ("  ·  ") + juce::String (settings.repeats) + fr (" × ")
                                  + rateNames()[rateIdx] + " at " + juce::String (juce::roundToInt (settings.bpm)) + " BPM";
            }
            else
            {
                lastResult = res;
                renderInfo.file = juce::File();
                renderInfo.peaks.clear();
                renderInfo.slotMarks.clear();
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
