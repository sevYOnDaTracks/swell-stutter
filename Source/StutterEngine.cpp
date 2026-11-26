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
    const int len = source.getNumSamples();
    const int hop = juce::jmax (1, (int) std::round (0.005 * sr));
    const int hops = len / hop;
    if (hops < 2)
        return onsets;

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
                onsets.push_back (s);
                inWord = true;
                quiet = 0;
            }
        }
        else
        {
            quiet = e < off ? quiet + 1 : 0;
            if (quiet >= minGapHops)
                inWord = false;
        }
    }
    return onsets;
}

Result render (const juce::AudioBuffer<float>& source, double sr, const Settings& s)
{
    Result r;
    const int srcLen = source.getNumSamples();
    const int srcCh  = source.getNumChannels();
    if (srcLen < 16 || srcCh == 0 || sr <= 0.0)
        return r;

    const auto onsets = findWordOnsets (source, sr);
    r.wordsFound = (int) onsets.size();
    if (onsets.empty())
        return r;

    const int wordIdx = juce::jlimit (0, (int) onsets.size() - 1, s.word - 1);
    const int onset = onsets[(size_t) wordIdx];
    const int lead  = onsets.front();          // le clip démarre au premier mot
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

        for (int n = 0; n < play; ++n)
        {
            const double sp = onset + n * speed;
            const int i0 = (int) sp;
            if (i0 + 1 >= srcLen)
                break;
            const float frac = (float) (sp - i0);
            const float w = rampUp (n, fi) * rampUp (play - 1 - n, fo);
            for (int ch = 0; ch < 2; ++ch)
            {
                const auto* src = source.getReadPointer (srcCh2 (ch));
                r.clip.setSample (ch, dst + n, (src[i0] + frac * (src[i0 + 1] - src[i0])) * w);
            }
        }
    }

    // 3) La phrase reprend depuis le début du mot
    for (int ch = 0; ch < 2; ++ch)
        for (int n = 0; n < rest; ++n)
            r.clip.setSample (ch, r.stutterEnd + n, source.getSample (srcCh2 (ch), onset + n) * rampUp (n, fadeIn));

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
