#include "StutterEngine.h"
#include <cmath>

namespace StutterEngine
{
static float monoAt (const juce::AudioBuffer<float>& b, int n)
{
    const int chs = b.getNumChannels();
    float v = 0.0f;
    for (int ch = 0; ch < chs; ++ch)
        v += b.getSample (ch, n);
    return chs > 0 ? v / (float) chs : 0.0f;
}

// Demi-fenêtre en cosinus : 0 -> 1 sur 'len' échantillons
static float rampUp (int x, int len)
{
    if (len <= 0 || x >= len) return 1.0f;
    if (x <= 0) return 0.0f;
    return 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * (float) x / (float) len);
}

std::vector<int> findWordOnsets (const juce::AudioBuffer<float>& source, double sr)
{
    std::vector<int> onsets;
    for (auto& w : findWords (source, sr))
        onsets.push_back (w.start);
    return onsets;
}

std::vector<Word> findWords (const juce::AudioBuffer<float>& source, double sr)
{
    std::vector<Word> words;
    const int len = source.getNumSamples();
    const int hop = juce::jmax (1, (int) std::round (0.005 * sr));
    const int hops = len / hop;
    if (hops < 2)
        return words;

    std::vector<float> env ((size_t) hops);
    float peak = 0.0f;
    for (int i = 0; i < hops; ++i)
    {
        double sum = 0.0;
        for (int n = 0; n < hop; ++n)
        {
            const float v = monoAt (source, i * hop + n);
            sum += (double) v * v;
        }
        env[(size_t) i] = (float) std::sqrt (sum / hop);
        peak = juce::jmax (peak, env[(size_t) i]);
    }

    const float on  = juce::jmax (peak * 0.12f, 0.002f);   // ~ -18 dB sous la crête
    const float off = on * 0.4f;
    const int minGapHops = 10;                              // 50 ms de creux = nouveau mot
    const int maxBackHops = 8;                              // remonte l'attaque douce (40 ms max)

    bool inWord = false;
    int quiet = 0;
    for (int i = 0; i < hops; ++i)
    {
        const float e = env[(size_t) i];
        if (! inWord)
        {
            if (e > on)
            {
                int j = i;
                while (j > 0 && i - j < maxBackHops && env[(size_t) j - 1] > off * 0.5f)
                    --j;
                int s = j * hop;
                // cale sur un passage par zéro juste avant (3 ms max)
                const int limit = juce::jmax (1, s - (int) (0.003 * sr));
                for (int n = s; n > limit; --n)
                {
                    const float a = monoAt (source, n - 1), b = monoAt (source, n);
                    if ((a <= 0.0f && b >= 0.0f) || (a >= 0.0f && b <= 0.0f)) { s = n; break; }
                }
                words.push_back ({ s, len });
                inWord = true;
                quiet = 0;
            }
        }
        else
        {
            quiet = e < off ? quiet + 1 : 0;
            if (quiet >= minGapHops)
            {
                inWord = false;
                words.back().end = juce::jmin (len, (i - minGapHops + 2) * hop);
            }
        }
    }
    return words;
}

// ------------------------------------------------------------ effets

namespace
{
    // Filtre du 2e ordre (formules RBJ), forme directe transposée.
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        void set (bool highpass, double f, double sr)
        {
            const double w = juce::MathConstants<double>::twoPi * f / sr, c = std::cos (w), alpha = std::sin (w) / (2.0 * 0.7071);
            const double a0 = 1.0 + alpha;
            if (highpass) { b0 = (1.0 + c) / 2.0; b1 = -(1.0 + c); b2 = b0; }
            else          { b0 = (1.0 - c) / 2.0; b1 = 1.0 - c;    b2 = b0; }
            b0 /= a0; b1 /= a0; b2 /= a0; a1 = -2.0 * c / a0; a2 = (1.0 - alpha) / a0;
        }
        float process (float x)
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return (float) y;
        }
    };
}

// Les effets colorent les répétitions ; ils s'ouvrent en 3 ms au début du mot et se referment en 40 ms
// quand la phrase reprend : la phrase revient propre.
void applyEffects (Result& r, double sr, const Settings& s)
{
    const float phone = juce::jlimit (0.0f, 1.0f, s.phone), chorus = juce::jlimit (0.0f, 1.0f, s.chorus);
    if (phone <= 0.001f && chorus <= 0.001f)
        return;

    const int len = r.clip.getNumSamples();
    const int rise = juce::jmax (1, (int) (0.003 * sr)), fall = juce::jmax (1, (int) (0.040 * sr));
    const int from = juce::jmax (0, r.clipOnset - (int) (0.030 * sr));   // les filtres se mettent en place avant le mot
    const int to = juce::jmin (len, r.stutterEnd + fall);
    auto weight = [&] (int n)
    {
        if (n < r.clipOnset) return 0.0f;
        if (n < r.clipOnset + rise) return rampUp (n - r.clipOnset, rise);
        if (n < r.stutterEnd) return 1.0f;
        return 1.0f - rampUp (n - r.stutterEnd, fall);
    };

    if (phone > 0.001f)
    {
        constexpr float drive = 3.0f, makeup = 1.8f;   // la bande étroite fait perdre du niveau : on le rend
        for (int ch = 0; ch < 2; ++ch)
        {
            Biquad hp1, hp2, lp1, lp2;
            hp1.set (true, 450.0, sr); hp2 = hp1; lp1.set (false, 3200.0, sr); lp2 = lp1;
            auto* d = r.clip.getWritePointer (ch);
            for (int n = from; n < to; ++n)
            {
                const float x = d[n];
                float y = lp2.process (lp1.process (hp2.process (hp1.process (x))));
                y = std::tanh (drive * makeup * y) / drive;   // un peu de grain ; pente 1 aux faibles niveaux
                d[n] = x + weight (n) * phone * (y - x);
            }
        }
    }

    if (chorus > 0.001f)
    {
        const int maxDelay = (int) (0.030 * sr) + 4;
        const double base = 0.014 * sr, depth = (0.002 + 0.006 * chorus) * sr;
        for (int ch = 0; ch < 2; ++ch)
        {
            std::vector<float> line ((size_t) maxDelay, 0.0f);
            int wp = 0;
            auto* d = r.clip.getWritePointer (ch);
            const double phase0 = ch == 0 ? 0.0 : juce::MathConstants<double>::halfPi;   // gauche et droite décalées : de la largeur
            for (int n = from; n < to; ++n)
            {
                const float x = d[n];
                line[(size_t) wp] = x;
                const double lfo = std::sin (juce::MathConstants<double>::twoPi * 0.9 * (n - from) / sr + phase0);
                const double delay = base + depth * 0.5 * (1.0 + lfo);
                double rp = wp - delay;
                while (rp < 0) rp += maxDelay;
                const int i0 = (int) rp, i1 = (i0 + 1) % maxDelay;
                const float frac = (float) (rp - i0);
                const float wet = line[(size_t) i0] + frac * (line[(size_t) i1] - line[(size_t) i0]);
                wp = (wp + 1) % maxDelay;
                const float w = weight (n) * chorus;
                d[n] = x * (1.0f - 0.25f * w) + wet * 0.65f * w;
            }
        }
    }
}

// Backverb : la reverb de la première répétition, jouée à l'envers, monte pendant 1 temps et finit pile
// sur le mot. Le clip gagne 1 temps en tête (à poser un temps plus tôt dans la playlist).
void addBackverb (Result& r, double sr, const Settings& s)
{
    const float amount = juce::jlimit (0.0f, 1.0f, s.backverb);
    const int P = (int) std::llround (60.0 / juce::jlimit (20.0, 999.0, s.bpm) * sr);
    const int sliceLen = juce::jlimit ((int) (0.08 * sr), P, r.slotLengths.empty() ? P : r.slotLengths.front());
    if (P < 64)
        return;

    juce::AudioBuffer<float> wet (2, P);
    wet.clear();
    for (int ch = 0; ch < 2; ++ch)
        wet.copyFrom (ch, 0, r.clip, ch, r.clipOnset, juce::jmin (sliceLen, r.clip.getNumSamples() - r.clipOnset));
    juce::Reverb rv;
    juce::Reverb::Parameters prm;
    prm.roomSize = 0.6f + 0.38f * amount;
    prm.damping = 0.35f;
    prm.wetLevel = 1.0f;
    prm.dryLevel = 0.0f;
    prm.width = 1.0f;
    rv.setParameters (prm);
    rv.setSampleRate (sr);
    rv.processStereo (wet.getWritePointer (0), wet.getWritePointer (1), P);

    // Niveau : la reverb inversée culmine un peu sous le mot lui-même.
    float slicePeak = 0.0f;
    for (int ch = 0; ch < 2; ++ch)
        slicePeak = juce::jmax (slicePeak, r.clip.getMagnitude (ch, r.clipOnset, juce::jmin (sliceLen, r.clip.getNumSamples() - r.clipOnset)));
    const float wetPeak = juce::jmax (wet.getMagnitude (0, 0, P), wet.getMagnitude (1, 0, P), 1.0e-6f);
    const float gain = amount * 0.85f * slicePeak / wetPeak;

    juce::AudioBuffer<float> out (2, r.clip.getNumSamples() + P);
    out.clear();
    for (int ch = 0; ch < 2; ++ch)
        out.copyFrom (ch, P, r.clip, ch, 0, r.clip.getNumSamples());

    const int tail = juce::jmax (1, (int) (0.003 * sr));
    for (int j = 0; j < P; ++j)
    {
        const float rise = rampUp (j, P);
        const float env = rise * rise * rampUp (P - 1 - j, tail);   // part du silence et monte jusqu'au mot
        for (int ch = 0; ch < 2; ++ch)
            out.addSample (ch, r.clipOnset + j, wet.getSample (ch, P - 1 - j) * env * gain);
    }

    r.clip = std::move (out);
    r.preroll = P;
    r.clipOnset += P;
    r.stutterEnd += P;
}

Result render (const juce::AudioBuffer<float>& source, double sr, const Settings& s)
{
    Result r;
    const int srcLen = source.getNumSamples();
    const int srcCh  = source.getNumChannels();
    if (srcLen < 16 || srcCh == 0 || sr <= 0.0)
        return r;

    const auto words = findWords (source, sr);
    r.wordsFound = (int) words.size();
    if (words.empty())
        return r;

    const int wordIdx = juce::jlimit (0, (int) words.size() - 1, s.word - 1);
    const int onset = words[(size_t) wordIdx].start;
    const int lead  = words.front().start;     // le clip démarre au premier mot
    r.wordIndex = wordIdx;
    const int pre   = onset - lead;
    r.sourceOnset = onset;
    r.clipOnset   = pre;

    const int N = juce::jlimit (1, 64, s.repeats);
    const double baseLen = s.rateBeats * 60.0 / juce::jlimit (20.0, 999.0, s.bpm) * sr;

    // Positions en double, arrondies une seule fois : aucune dérive par rapport à la grille.
    std::vector<double> lens ((size_t) N);
    double pos = 0.0;
    for (int k = 0; k < N; ++k)
    {
        const double t = N > 1 ? (double) k / (N - 1) : 0.0;
        lens[(size_t) k] = baseLen * std::pow (2.0, -2.0 * s.build * t);
        r.slotStarts.push_back ((int) std::llround (pos));
        pos += lens[(size_t) k];
    }
    const int stutterLen = (int) std::llround (pos);
    for (int k = 0; k < N; ++k)
        r.slotLengths.push_back ((k + 1 < N ? r.slotStarts[(size_t) k + 1] : stutterLen) - r.slotStarts[(size_t) k]);

    const int rest = srcLen - onset;
    r.stutterEnd = pre + stutterLen;
    r.clip.setSize (2, pre + stutterLen + rest);
    r.clip.clear();

    const int fadeIn  = juce::jmax (1, (int) std::round (fadeInMs  * 0.001 * sr));
    const int fadeOut = juce::jmax (1, (int) std::round (fadeOutMs * 0.001 * sr));
    auto srcCh2 = [&] (int ch) { return juce::jmin (ch, srcCh - 1); };

    // 1) Ce qui précède le mot haché (rien si c'est le premier mot), fondu en sortie
    for (int ch = 0; ch < 2; ++ch)
        for (int n = 0; n < pre; ++n)
            r.clip.setSample (ch, n, source.getSample (srcCh2 (ch), lead + n) * rampUp (pre - 1 - n, fadeOut));

    // 2) Les répétitions
    for (int k = 0; k < N; ++k)
    {
        const int slot = r.slotLengths[(size_t) k];
        const int minPlay = juce::jmin (slot, (int) (0.012 * sr));
        const int play = juce::jlimit (minPlay, slot, (int) std::round (s.gate * slot));
        const double t = N > 1 ? (double) k / (N - 1) : 0.0;
        const double speed = std::pow (2.0, s.pitch * t / 12.0);
        const int fo = juce::jmin (fadeOut, play / 2), fi = juce::jmin (fadeIn, play / 2);
        const int dst = pre + r.slotStarts[(size_t) k];
        // Fade in : de -30 dB (au maximum) à 0 dB sur la dernière répétition.
        const float level = juce::Decibels::decibelsToGain (-30.0f * juce::jlimit (0.0f, 1.0f, s.fadeIn) * (float) (1.0 - t));
        // Pan : une répétition à gauche, la suivante à droite (loi à puissance constante, gain 1 au centre).
        // À 0 % les gains valent exactement 1 : le clip est identique, échantillon pour échantillon, à un rendu sans Pan.
        const float width = juce::jlimit (0.0f, 1.0f, s.pan);
        float chGain[2] { level, level };
        if (width > 0.0f)
        {
            const float side = (k % 2 == 0 ? -1.0f : 1.0f) * width;
            const float angle = (side + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
            chGain[0] = level * std::cos (angle) * juce::MathConstants<float>::sqrt2;
            chGain[1] = level * std::sin (angle) * juce::MathConstants<float>::sqrt2;
        }
        // Voix stéréo : on la replie vers le mono à mesure que le Pan s'ouvre, sinon une répétition
        // envoyée à gauche perdrait tout ce que contenait le canal droit.
        const bool stereoIn = srcCh > 1 && width > 0.0f;

        for (int n = 0; n < play; ++n)
        {
            const double sp = onset + n * speed;
            const int i0 = (int) sp;
            if (i0 + 1 >= srcLen)
                break;
            const float frac = (float) (sp - i0);
            const float w = rampUp (n, fi) * rampUp (play - 1 - n, fo);
            float in[2];
            for (int ch = 0; ch < 2; ++ch)
            {
                const auto* src = source.getReadPointer (srcCh2 (ch));
                in[ch] = src[i0] + frac * (src[i0 + 1] - src[i0]);
            }
            if (stereoIn)
            {
                const float mid = 0.5f * (in[0] + in[1]);
                in[0] += width * (mid - in[0]);
                in[1] += width * (mid - in[1]);
            }
            for (int ch = 0; ch < 2; ++ch)
                r.clip.setSample (ch, dst + n, in[ch] * w * chGain[ch]);
        }
    }

    // 3) La phrase reprend depuis le début du mot
    for (int ch = 0; ch < 2; ++ch)
        for (int n = 0; n < rest; ++n)
            r.clip.setSample (ch, r.stutterEnd + n, source.getSample (srcCh2 (ch), onset + n) * rampUp (n, fadeIn));

    applyEffects (r, sr, s);
    if (s.backverb > 0.001f)
        addBackverb (r, sr, s);

    r.clip.applyGain (juce::Decibels::decibelsToGain (s.gainDb));
    r.ok = true;
    return r;
}

std::vector<float> peaks (const juce::AudioBuffer<float>& b, int columns)
{
    std::vector<float> out;
    const int len = b.getNumSamples();
    if (len == 0 || columns <= 0)
        return out;
    out.resize ((size_t) columns * 2, 0.0f);
    for (int c = 0; c < columns; ++c)
    {
        const int s0 = (int) ((juce::int64) c * len / columns);
        const int s1 = juce::jmax (s0 + 1, (int) ((juce::int64) (c + 1) * len / columns));
        float mn = 0.0f, mx = 0.0f;
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
        {
            const auto* p = b.getReadPointer (ch);
            for (int n = s0; n < s1 && n < len; ++n)
            {
                mn = juce::jmin (mn, p[n]);
                mx = juce::jmax (mx, p[n]);
            }
        }
        out[(size_t) c * 2]     = mn;
        out[(size_t) c * 2 + 1] = mx;
    }
    return out;
}
}
