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
        float  fadeIn    = 0.0f;   // 0..1 : les répétitions montent en volume (de -30 dB à 0 au maximum)
        float  pan       = 0.0f;   // 0..1 : les répétitions alternent gauche / droite
        // Effets sur les répétitions seulement : la phrase revient propre.
        float  chorus    = 0.0f;   // 0..1
        float  phone     = 0.0f;   // 0..1 : voix téléphone (bande étroite, un peu de grain)
        float  backverb  = 0.0f;   // 0..1 : reverb inversée qui monte juste avant le mot (le clip démarre 1 temps plus tôt)
    };

    struct Word { int start = 0, end = 0; };   // en échantillons dans la source

    struct Result
    {
        juce::AudioBuffer<float> clip;
        std::vector<int> slotStarts;   // début de chaque répétition dans le clip
        std::vector<int> slotLengths;
        int    stutterEnd   = 0;       // dans le clip : là où la phrase reprend
        int    clipOnset    = 0;       // dans le clip : début du mot haché
        int    sourceOnset  = 0;       // dans la source
        int    preroll      = 0;       // échantillons ajoutés en tête pour la Backverb (0 sinon)
        int    wordIndex    = 0;       // mot haché (0 = le premier)
        int    wordsFound   = 0;
        bool   ok           = false;
    };

    // Débuts de mots (échantillons), détectés sur l'énergie avec hystérésis.
    std::vector<int> findWordOnsets (const juce::AudioBuffer<float>& source, double sr);
    // Mêmes mots, avec leur fin (pour les afficher et les écouter seuls).
    std::vector<Word> findWords (const juce::AudioBuffer<float>& source, double sr);

    Result render (const juce::AudioBuffer<float>& source, double sr, const Settings& s);
    void applyEffects (Result&, double sr, const Settings&);
    void addBackverb (Result&, double sr, const Settings&);

    // Vraie forme d'onde : (min, max) par colonne.
    std::vector<float> peaks (const juce::AudioBuffer<float>& b, int columns);

    // Enveloppe anti-clic (cosinus) appliquée en tête et en fin de chaque répétition.
    constexpr double fadeInMs  = 1.5;
    constexpr double fadeOutMs = 4.0;
}
