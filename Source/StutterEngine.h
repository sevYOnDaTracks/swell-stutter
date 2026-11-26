#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

// Moteur hors ligne : trouve le début d'un mot dans une voix et rend un clip « I-I-I-I want ».
// Une tranche qui part du début du mot est rejouée N fois, calée sur le tempo, puis la phrase continue.
namespace StutterEngine
{
    struct Settings
    {
        double bpm       = 120.0;
        double rateBeats = 0.25;   // longueur d'une répétition, en noires (1/16 = 0.25)
        int    repeats   = 4;      // nombre de tranches avant la phrase
        float  build     = 0.0f;   // 0 : régulier, 1 : les répétitions accélèrent jusqu'à 4x plus vite
        float  pitch     = 0.0f;   // demi-tons atteints à la dernière répétition (montée progressive)
        float  gate      = 0.85f;  // part de chaque tranche qui sonne (le reste est un silence sec)
        int    word      = 1;      // quel mot hacher (1 = le premier)
        float  gainDb    = 0.0f;
    };

    struct Result
    {
        juce::AudioBuffer<float> clip;
        std::vector<int> slotStarts;   // début de chaque répétition dans le clip
        std::vector<int> slotLengths;
        int    stutterEnd   = 0;       // dans le clip : là où la phrase reprend
        int    clipOnset    = 0;       // dans le clip : début du mot haché
        int    sourceOnset  = 0;       // dans la source
        int    wordsFound   = 0;
        bool   ok           = false;
    };

    // Débuts de mots (échantillons), détectés sur l'énergie avec hystérésis.
    std::vector<int> findWordOnsets (const juce::AudioBuffer<float>& source, double sr);

    Result render (const juce::AudioBuffer<float>& source, double sr, const Settings& s);

    // Vraie forme d'onde : (min, max) par colonne.
    std::vector<float> peaks (const juce::AudioBuffer<float>& b, int columns);

    // Enveloppe anti-clic (cosinus) appliquée en tête et en fin de chaque répétition.
    constexpr double fadeInMs  = 1.5;
    constexpr double fadeOutMs = 4.0;
}
