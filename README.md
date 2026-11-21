# MasterHead — by BoodmanFX

Plugin VST3 d'aide au mastering (effet stéréo, à poser sur le master bus), organisé comme la référence :
7 modules avec interrupteur ON, du haut vers le bas.

| Module | Réglages |
|---|---|
| SPECTRAL | Amount — EQ dynamique 6 bandes qui rapproche le spectre d'une forme cible, courbe affichée |
| COMP | Glue, Detail — compresseur 3 bandes (L/M/H) avec remontée des passages calmes |
| ANALOG | Color — saturation douce (x4) |
| STEREO | Width, LM (graves mono) |
| EQ | Bass, Air (en %) |
| CLIP | Gain — écrêtage doux (x4) |
| LIMITER | Gain, TP (détection inter-échantillons estimée) |

Barre du bas : Monitor (None / Mid / Side / Delta), spectre, LUFS S, LUFS I (clic sur le picto = reset),
PEAK (clic = reset), DYNAMICS (crête récente - LUFS S), STEREO.
Barre du haut : Input + vu-mètre, UNDO / REDO, A / B / A→B, Bypass (compensé en latence).
Gros bouton rond : **ASSIST** (choisis la cible puis laisse jouer 10 s la partie la plus forte).

## Compiler
Pousse ce dossier sur GitHub : **Actions** compile pour Windows. Récupère `MasterHead.vst3`
dans les *Artifacts*, puis copie le dossier dans `C:\Program Files\Common Files\VST3`.
Local : `cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --config Release`

## Réglages heuristiques (à ajuster à l'oreille)
`kSpecTarget` (Dsp.h), formules de `finishAnalysis()` (PluginProcessor.cpp), seuils GOOD/LOUD/DYNAMICS (PluginEditor.cpp).

## Limites connues
- Limiteur de crête avec détection inter-échantillons estimée (cubique x4) : pas un true-peak certifié.
- LUFS approché (filtre K simplifié) : indicatif.
- Latence ~3,3 ms à 48 kHz (déclarée au DAW).
- Analog est placé après Stereo/EQ (un seul bloc suréchantillonné).
