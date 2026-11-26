// Banc de test hors-ligne de Swell Stutter.
// Une « voix » synthétique (2 mots) passe dans le moteur ; on vérifie :
//   - la détection des mots,
//   - le calage tempo exact des répétitions (aucune dérive, triolets compris),
//   - l'absence de clic à chaque jonction (début/fin de répétition, reprise de la phrase),
//   - que chaque répétition rejoue bien le début du mot,
//   - Build (accélération) et Pitch (montée),
//   - le chemin complet du plugin (rendu en tâche de fond, fichier WAV, capture, son traversant intact).
// Écrit aussi des WAV à écouter et des captures PNG de l'interface.
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include <cstdlib>

using namespace juce;

static bool ok = true;
static void check (bool cond, const String& what)
{
    std::printf ("  [%s] %s\n", cond ? "ok" : "FAIL", what.toRawUTF8());
    ok &= cond;
}

static void setParam (StutterAudioProcessor& p, const char* id, float plainValue)
{
    auto* param = p.apvts.getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 (plainValue));
}

// Voix synthétique : harmoniques d'une fondamentale avec vibrato, formants grossiers, attaque/relâchement doux
static void addWord (AudioBuffer<float>& b, double sr, double start, double dur, double f0, Random& rng)
{
    const int s0 = (int) (start * sr), n = (int) (dur * sr);
    double phase = 0.0;
    for (int i = 0; i < n && s0 + i < b.getNumSamples(); ++i)
    {
        const double t = i / sr;
        const double f = f0 * (1.0 + 0.01 * std::sin (2.0 * MathConstants<double>::pi * 5.5 * t));
        phase += 2.0 * MathConstants<double>::pi * f / sr;
        double v = 0.0;
        for (int h = 1; h <= 12; ++h)
        {
            const double fh = f0 * h;
            const double formant = std::exp (-std::pow ((fh - 700.0) / 300.0, 2.0)) + 0.6 * std::exp (-std::pow ((fh - 1200.0) / 400.0, 2.0)) + 0.15;
            v += formant * std::sin (phase * h) / h;
        }
        v += 0.02 * (rng.nextFloat() * 2.0f - 1.0f);
        const double env = jmin (1.0, t / 0.012) * jmin (1.0, (dur - t) / 0.04);
        b.addSample (0, s0 + i, (float) (0.35 * v * env));
        b.addSample (1, s0 + i, (float) (0.33 * v * env));
    }
}

static AudioBuffer<float> makeVoice (double sr)
{
    AudioBuffer<float> b (2, (int) (2.2 * sr));
    b.clear();
    Random rng (7);
    addWord (b, sr, 0.30, 0.35, 190.0, rng);   // « I »
    addWord (b, sr, 0.85, 0.55, 165.0, rng);   // « want »
    return b;
}

static void writeWav (const File& f, const AudioBuffer<float>& b, double sr)
{
    f.deleteFile();
    WavAudioFormat wav;
    std::unique_ptr<OutputStream> os (std::make_unique<FileOutputStream> (f));
    auto opts = AudioFormatWriterOptions{}.withSampleRate (sr).withNumChannels (b.getNumChannels()).withBitsPerSample (24);
    if (auto w = wav.createWriterFor (os, opts))
        w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

// Détecteur indépendant : un son qui démarre après au moins 3 ms de quasi-silence
static std::vector<int> soundStarts (const AudioBuffer<float>& b, double sr)
{
    std::vector<int> starts;
    const int minQuiet = (int) (0.003 * sr);
    int quiet = minQuiet;
    for (int n = 0; n < b.getNumSamples(); ++n)
    {
        const float a = std::abs (b.getSample (0, n));
        if (a < 1.0e-4f) ++quiet;
        else
        {
            if (quiet >= minQuiet) starts.push_back (n);
            quiet = 0;
        }
    }
    return starts;
}

static float maxStep (const AudioBuffer<float>& b, int from, int to)
{
    float m = 0.0f;
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int n = jmax (1, from); n < jmin (b.getNumSamples(), to); ++n)
            m = jmax (m, std::abs (b.getSample (ch, n) - b.getSample (ch, n - 1)));
    return m;
}

static double correlation (const float* a, const float* b, int n)
{
    double ab = 0, aa = 0, bb = 0;
    for (int i = 0; i < n; ++i) { ab += a[i] * b[i]; aa += a[i] * a[i]; bb += b[i] * b[i]; }
    return ab / std::sqrt (jmax (1.0e-12, aa * bb));
}

// Fondamentale estimée par autocorrélation (60..1200 Hz)
static double estimateF0 (const AudioBuffer<float>& b, int from, int len, double sr)
{
    const float* x = b.getReadPointer (0, from);
    int bestLag = 0; double best = -1.0;
    for (int lag = (int) (sr / 1200.0); lag <= (int) (sr / 60.0); ++lag)
    {
        double acc = 0.0;
        for (int i = 0; i + lag < len; ++i) acc += x[i] * x[i + lag];
        if (acc > best) { best = acc; bestLag = lag; }
    }
    return bestLag > 0 ? sr / bestLag : 0.0;
}

int main (int argc, char** argv)
{
    swell::Licence::unlockForTesting();
    ScopedJuceInitialiser_GUI init;
    const File outDir (argc > 1 ? String::fromUTF8 (argv[1]) : File::getCurrentWorkingDirectory().getFullPathName());
    outDir.createDirectory();
    const auto clipsDir = outDir.getChildFile ("clips");
    clipsDir.deleteRecursively();
    setenv ("SWELL_STUTTER_FOLDER", clipsDir.getFullPathName().toRawUTF8(), 1);

    const double sr = 48000.0;
    const auto voice = makeVoice (sr);
    writeWav (outDir.getChildFile ("00_voice.wav"), voice, sr);

    // Pas maximal « naturel » de la voix : sert de référence pour les clics
    const float voiceStep = maxStep (voice, 0, voice.getNumSamples());

    // --- 1. Détection des mots
    std::printf ("[1] word detection\n");
    const auto onsets = StutterEngine::findWordOnsets (voice, sr);
    check (onsets.size() == 2, "2 words found (got " + String ((int) onsets.size()) + ")");
    if (onsets.size() >= 2)
    {
        check (std::abs (onsets[0] / sr - 0.30) < 0.010, "word 1 at " + String (onsets[0] / sr, 4) + " s (expected 0.300)");
        check (std::abs (onsets[1] / sr - 0.85) < 0.010, "word 2 at " + String (onsets[1] / sr, 4) + " s (expected 0.850)");
    }

    // --- 2. Calage tempo : 1/16 à 120 BPM = 6000 échantillons, 4 répétitions
    std::printf ("[2] tempo lock, 1/16 at 120 BPM\n");
    StutterEngine::Settings s;
    s.bpm = 120.0; s.rateBeats = 0.25; s.repeats = 4; s.gate = 0.85f;
    auto r = StutterEngine::render (voice, sr, s);
    writeWav (outDir.getChildFile ("01_stutter_16th_x4.wav"), r.clip, sr);
    check (r.ok, "render ok");
    bool exact = r.slotStarts.size() == 4;
    for (int k = 0; k < (int) r.slotStarts.size(); ++k)
        exact &= r.slotStarts[(size_t) k] == k * 6000;
    check (exact, "slots start at exactly 0, 6000, 12000, 18000 samples");
    check (r.stutterEnd == r.clipOnset + 24000, "phrase resumes exactly 1 beat later (sample " + String (r.stutterEnd) + ")");

    // Contrôle indépendant sur l'audio rendu : les sons démarrent bien sur la grille
    {
        const auto starts = soundStarts (r.clip, sr);
        int matched = 0;
        double worstMs = 0.0;
        for (int k = 0; k <= 4; ++k)
        {
            const int expected = r.clipOnset + k * 6000;
            int best = -1;
            for (int st : starts)
                if (best < 0 || std::abs (st - expected) < std::abs (best - expected)) best = st;
            if (best >= 0 && std::abs (best - expected) <= (int) (0.002 * sr))
            {
                ++matched;
                worstMs = jmax (worstMs, 1000.0 * std::abs (best - expected) / sr);
            }
        }
        check (matched == 5, "audio hits on the grid: " + String (matched) + "/5, worst offset " + String (worstMs, 2) + " ms");
    }

    // --- 3. Pas de clic aux jonctions
    std::printf ("[3] no clicks at junctions\n");
    {
        const int w = (int) (0.001 * sr);
        float worst = 0.0f, worstLevel = 0.0f;
        std::vector<int> junctions;
        for (size_t k = 0; k < r.slotStarts.size(); ++k)
        {
            junctions.push_back (r.clipOnset + r.slotStarts[k]);
            junctions.push_back (r.clipOnset + r.slotStarts[k] + (int) std::round (s.gate * r.slotLengths[k]));
        }
        junctions.push_back (r.stutterEnd);
        for (int j : junctions)
        {
            worst = jmax (worst, maxStep (r.clip, j - w, j + w));
            for (int ch = 0; ch < 2; ++ch)
                worstLevel = jmax (worstLevel, std::abs (r.clip.getSample (ch, jmin (j, r.clip.getNumSamples() - 1))));
        }
        std::printf ("      worst step near junctions %.4f, voice max step %.4f, level at junction %.4f\n", worst, voiceStep, worstLevel);
        check (worst <= voiceStep * 0.5f, "steps at junctions stay below half the voice's own steepest step");
        check (worstLevel < 0.02f, "signal is near zero right at every junction");

        // Même mesure avec Gate 100 % (répétitions collées) : la pire jonction pour les clics
        auto s2 = s; s2.gate = 1.0f;
        auto r2 = StutterEngine::render (voice, sr, s2);
        float worst2 = 0.0f;
        for (int k = 1; k < (int) r2.slotStarts.size(); ++k)
            worst2 = jmax (worst2, maxStep (r2.clip, r2.clipOnset + r2.slotStarts[(size_t) k] - w, r2.clipOnset + r2.slotStarts[(size_t) k] + w));
        worst2 = jmax (worst2, maxStep (r2.clip, r2.stutterEnd - w, r2.stutterEnd + w));
        check (worst2 <= voiceStep * 0.5f, "gate 100 %: junction step " + String (worst2, 4) + " also clean");

        // Témoin : sans enveloppe (coupe franche au milieu du mot), le test DOIT voir le clic
        const int cut = r.sourceOnset + 3000;
        const float hard = std::abs (voice.getSample (0, cut - 1));
        std::printf ("      (reference: a hard cut mid-word would jump by %.4f)\n", hard);
        check (hard > voiceStep * 0.5f, "click detector is sensitive enough (hard cut would fail)");
    }

    // --- 4. Chaque répétition rejoue le début du mot
    std::printf ("[4] repeats replay the start of the word\n");
    {
        const int off = (int) (0.003 * sr), len = (int) (0.030 * sr);
        double worstCorr = 1.0;
        for (int k = 0; k < (int) r.slotStarts.size(); ++k)
            worstCorr = jmin (worstCorr, correlation (r.clip.getReadPointer (0, r.clipOnset + r.slotStarts[(size_t) k] + off),
                                                      voice.getReadPointer (0, r.sourceOnset + off), len));
        check (worstCorr > 0.99, "correlation with the word start >= 0.99 (worst " + String (worstCorr, 4) + ")");
    }

    // --- 5. Triolets, tempo non entier : aucune dérive
    std::printf ("[5] triplets at 93.5 BPM, 12 repeats\n");
    {
        auto s3 = s; s3.bpm = 93.5; s3.rateBeats = 0.5 / 1.5; s3.repeats = 12;  // 1/8T
        auto r3 = StutterEngine::render (voice, sr, s3);
        const double len = s3.rateBeats * 60.0 / s3.bpm * sr;
        double worst = 0.0;
        for (int k = 0; k < (int) r3.slotStarts.size(); ++k)
            worst = jmax (worst, std::abs (r3.slotStarts[(size_t) k] - k * len));
        const double endErr = std::abs ((r3.stutterEnd - r3.clipOnset) - 12 * len);
        check (worst <= 0.5 && endErr <= 0.5, "max error " + String (worst, 2) + " sample(s), end error " + String (endErr, 2));
        writeWav (outDir.getChildFile ("02_stutter_8T_x12.wav"), r3.clip, sr);
    }

    // --- 6. Build et Pitch
    std::printf ("[6] build + pitch\n");
    {
        auto s4 = s; s4.repeats = 8; s4.rateBeats = 0.5; s4.build = 1.0f; s4.pitch = 12.0f; s4.gate = 0.9f;
        auto r4 = StutterEngine::render (voice, sr, s4);
        bool decreasing = true;
        for (size_t k = 1; k < r4.slotLengths.size(); ++k)
            decreasing &= r4.slotLengths[k] < r4.slotLengths[k - 1];
        const double ratio = (double) r4.slotLengths.front() / r4.slotLengths.back();
        check (decreasing && std::abs (ratio - 4.0) < 0.05, "repeats speed up, first/last = " + String (ratio, 2) + " (expected 4)");
        const int zl = (int) (0.025 * sr);
        const double f0 = estimateF0 (r4.clip, r4.clipOnset + r4.slotStarts.front() + 200, zl, sr);
        const double fN = estimateF0 (r4.clip, r4.clipOnset + r4.slotStarts.back() + 200, zl, sr);
        check (std::abs (fN / f0 - 2.0) < 0.06, "pitch climbs one octave: " + String (f0, 0) + " Hz -> " + String (fN, 0) + " Hz");
        writeWav (outDir.getChildFile ("03_build_pitch_up.wav"), r4.clip, sr);

        auto s5 = s; s5.word = 2; s5.repeats = 6; s5.rateBeats = 1.0 / 6.0;
        auto r5 = StutterEngine::render (voice, sr, s5);
        check (r5.sourceOnset == onsets.back() && r5.clipOnset == onsets.back() - onsets.front(), "Word 2 chops the second word");
        writeWav (outDir.getChildFile ("04_word2_16T_x6.wav"), r5.clip, sr);
    }

    // --- 7. Plugin complet
    std::printf ("[7] plugin\n");
    StutterAudioProcessor p;
    p.setPlayConfigDetails (2, 2, sr, 512);
    p.prepareToPlay (sr, 512);
    {
        // Le son qui traverse le plugin n'est pas touché
        AudioBuffer<float> b (2, 512); MidiBuffer m;
        b.copyFrom (0, 0, voice, 0, (int) (0.4 * sr), 512);
        b.copyFrom (1, 0, voice, 1, (int) (0.4 * sr), 512);
        AudioBuffer<float> ref (b);
        p.processBlock (b, m);
        float diff = 0; for (int n = 0; n < 512; ++n) diff = jmax (diff, std::abs (b.getSample (0, n) - ref.getSample (0, n)));
        check (diff == 0.0f, "audio passes through unchanged");

        // Capture : armer, jouer, arrêter
        p.toggleCapture();
        for (int pos = 0; pos + 512 <= voice.getNumSamples(); pos += 512)
        {
            AudioBuffer<float> blk (2, 512);
            for (int ch = 0; ch < 2; ++ch) blk.copyFrom (ch, 0, voice, ch, pos, 512);
            p.processBlock (blk, m);
        }
        check (p.getCaptureState() == StutterAudioProcessor::CaptureState::recording, "capture started on the first sound");
        p.toggleCapture();
        b.clear(); p.processBlock (b, m);
        MessageManager::getInstance()->runDispatchLoopUntil (300);   // le timer clôt la capture et lance le rendu
        p.waitForRender (5000);
        MessageManager::getInstance()->runDispatchLoopUntil (50);
        const auto info = p.getRenderInfo();
        check (info.file.existsAsFile(), "clip written: " + info.file.getFileName());
        const auto res = p.getLastResult();
        check (res.ok && res.slotStarts.size() == 4 && res.slotStarts[1] == 6000, "captured vocal stuttered on the grid");
        check (clipsDir.getChildFile ("Sources").getNumberOfChildFiles (File::findFiles, "*.wav") == 1, "source vocal kept for the project");

        // Un réglage change -> nouveau clip automatique, l'ancien (jamais glissé) est supprimé
        const auto first = info.file;
        setParam (p, ParamID::repeats, 8.0f);
        MessageManager::getInstance()->runDispatchLoopUntil (400);
        p.waitForRender (5000);
        MessageManager::getInstance()->runDispatchLoopUntil (50);
        const auto second = p.getRenderInfo().file;
        check (second.existsAsFile() && second != first && ! first.existsAsFile(), "knob change re-renders, unused old clip removed");
        check (p.getLastResult().slotStarts.size() == 8, "new clip has 8 repeats");

        // Pré-écoute
        p.togglePreview();
        float mx = 0;
        for (int i = 0; i < 100; ++i) { b.clear(); p.processBlock (b, m); mx = jmax (mx, b.getMagnitude (0, 512)); }
        check (mx > 0.05f, "preview plays the clip");
        p.stopPreview();
    }

    // --- Captures de l'interface
    std::printf ("[ui] snapshots\n");
    {
        setParam (p, ParamID::repeats, 6.0f);
        setParam (p, ParamID::build, 0.6f);
        MessageManager::getInstance()->runDispatchLoopUntil (400);
        p.waitForRender (5000);
        std::unique_ptr<AudioProcessorEditor> ed (p.createEditor());
        ed->setBounds (0, 0, ed->getWidth(), ed->getHeight());
        auto* main = dynamic_cast<StutterAudioProcessorEditor*> (ed.get());
        MessageManager::getInstance()->runDispatchLoopUntil (300);
        main->getView().refreshNow();
        auto snap = [&] (Component& c, const char* name)
        {
            auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 2.0f);
            PNGImageFormat png; auto f = outDir.getChildFile (name); f.deleteFile();
            FileOutputStream os (f); png.writeImageToStream (img, os);
        };
        snap (*ed, "ui.png");
        main->getView().setMoreOpen (true);
        MessageManager::getInstance()->runDispatchLoopUntil (50);
        snap (*ed, "ui_more.png");
        main->getView().setMoreOpen (false);
        main->getView().showHelp();
        MessageManager::getInstance()->runDispatchLoopUntil (50);
        snap (*ed, "ui_help.png");

        StutterAudioProcessor pe;
        pe.setPlayConfigDetails (2, 2, sr, 512);
        pe.prepareToPlay (sr, 512);
        std::unique_ptr<AudioProcessorEditor> ee (pe.createEditor());
        ee->setBounds (0, 0, ee->getWidth(), ee->getHeight());
        MessageManager::getInstance()->runDispatchLoopUntil (200);
        snap (*ee, "ui_empty.png");
    }

    std::printf (ok ? "RESULT: PASS\n" : "RESULT: FAIL\n");
    return ok ? 0 : 1;
}
