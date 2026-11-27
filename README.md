# Swell Stutter

Stutter vocal sec, en clip : **Capture → le plugin hache le premier mot en rythme → glisser le clip dans la playlist.**
« I-I-I-I want », « ta-ta-ta-ta ». Pas de reverb, pas de gate : une vraie répétition de la tranche, calée sur le tempo.

VST3 · AU · Standalone — macOS universel (Apple Silicon + Intel). JUCE 8.

## Utilisation
1. Mettre Swell Stutter sur la piste de la voix (le son qui le traverse n'est pas modifié).
2. **Capture**, puis lecture dans le DAW : l'enregistrement démarre au premier son et s'arrête avec la lecture.
   (Ou déposer un sample de voix sur la grande zone.)
3. Le plugin trouve le début du mot et rend le clip. Chaque réglage refait le clip tout seul.
   Pour un autre mot : cliquer dessus dans la bande au-dessus du clip (on l'entend seul, et il devient le mot haché).
4. **Drag to playlist**.

| Réglage | Rôle |
|---|---|
| Rate | durée d'une répétition, calée sur le tempo : 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32 |
| Repeats | nombre de répétitions avant que la phrase continue (1–16) |
| Build | les répétitions accélèrent (jusqu'à 4× plus vite à la fin) |
| Pitch | les répétitions montent jusqu'à +12 demi-tons |
| Gate | 100 % : répétitions collées ; moins : coups plus courts et secs |
| More → Word | hacher le 2e, 3e… mot au lieu du premier (ou cliquer le mot dans la bande) |
| More → Fade in | les répétitions montent en volume (de −30 dB à 0 au maximum) |
| More → Pan | les répétitions alternent gauche / droite (0 % : centré ; puissance constante ; une voix stéréo est repliée en mono à mesure que le Pan s'ouvre) |
| More → Output | niveau du clip |
| FX → Chorus | répétitions élargies, doublées |
| FX → Phone | voix téléphone (bande étroite, un peu de grain), niveau gardé |
| FX → Backverb | reverb inversée qui monte pendant 1 temps et finit sur le mot : le clip démarre 1 temps plus tôt |

**Presets** (menu au centre de l'en-tête) : 10 presets d'usine (Default, Tight 1/32, Triplet roll, Slow 1/8, Build up,
Riser +12, Ping-pong, Phone stutter, Wide chorus, Backverb intro) et les tiens avec **Save preset…** (rangés dans
`Music/Swell Stutter/Presets`, un fichier `.preset` chacun, donc disponibles dans tous les projets). Un preset ne change que
les réglages du son : jamais la voix capturée ni le mot choisi. Un « * » après le nom veut dire qu'un réglage a bougé.
Les presets d'usine sont aussi visibles dans le menu de presets du DAW.

Les effets ne colorent que les répétitions (ouverture en 3 ms, fermeture en 40 ms) : la phrase revient propre.

Chaque répétition a une enveloppe anti-clic (1,5 ms d'attaque, 4 ms de fin, en cosinus) et démarre sur un passage par zéro.
Les clips vont dans `Music/Swell Stutter` ; la voix capturée est gardée dans `Sources/` pour retrouver le clip à la réouverture du projet.
Un clip jamais glissé est supprimé quand un nouveau le remplace.

## Compiler
```bash
scripts/build_mac.sh
scripts/install_mac.sh
```
JUCE : le checkout de `../Swell-Reverse/JUCE` est réutilisé s'il existe, sinon JUCE 8.0.10 est téléchargé.

## Banc de test hors-ligne
```bash
cmake -B build-test -DCMAKE_BUILD_TYPE=Release -DSWELL_BUILD_TESTS=ON
cmake --build build-test --target SwellStutterTest
./build-test/SwellStutterTest_artefacts/Release/SwellStutterTest test-out
```
Vérifie la détection des mots, le calage tempo à l'échantillon près (triolets et tempo non entier compris),
l'absence de clic à chaque jonction, Build, Pitch, Word, la capture et le rendu du plugin. Écrit des WAV et des captures PNG.

Licence : module commun `Source/Licence` (slug boutique `swell-stutter`).
