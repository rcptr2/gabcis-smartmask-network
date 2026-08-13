# Gabci's SmartMask Network

> A különböző sávokon lévő példányok beszélnek egymással, és prioritás szerint oldják fel a spektrális elfedést.

[![Licenc: AGPL v3](https://img.shields.io/badge/licenc-AGPL--3.0-blue.svg)](LICENSE)
[![Build](https://github.com/rcptr2/gabcis-smartmask-network/actions/workflows/build.yml/badge.svg)](https://github.com/rcptr2/gabcis-smartmask-network/actions/workflows/build.yml)
![Platform](https://img.shields.io/badge/platform-Windows%20x64%20%7C%20macOS%20Intel-lightgrey)
![Formátum](https://img.shields.io/badge/form%C3%A1tum-VST3%20%7C%20Standalone-green)

🇬🇧 *English documentation: [README.md](README.md)*

![A Gabci's SmartMask Network kezelőfelülete](docs/images/smartmask-network-ui.png)

*Pirossal az elfedett tartomány a sáv és egy magasabb prioritású sáv között, jobbra a hálózat közös prioritási listája.*

A spektrális elfedés az egész mix problémája, egy hagyományos plugin viszont csak egyetlen sávot lát.
A SmartMask Network olyan példányokból áll, amelyek látják egymást: tegyél egyet minden sávra, amely
ugyanazért a helyért verseng, adj mindegyiknek prioritást, és egyeztetnek egymással. Ahol egy
magasabb prioritású sáv elfed egy alacsonyabbat, ott csak az ütköző frekvenciasávok halkulnak le az
alacsonyabb prioritású sávon — minden más érintetlen marad.

[JUCE](https://juce.com) alapon készült.

## Hogyan működik

- **Közös regiszter** — minden példány beregisztrálja magát egy folyamatszintű regiszterbe, közzéteszi
  a saját spektrumát és prioritását, és olvassa a többiekét.
- **Spektrális motor** — FFT-alapú analizátor állítja elő a sávonkénti szinteket, amelyeken az
  összehasonlítás fut.
- **Elfedés-processzor** — minden sávra megnézi, hogy éppen dominálja-e egy magasabb prioritású
  példány, és csak ott alkalmaz erősítéscsökkentést, saját felfutással és elengedéssel.
- **Keresztpéldányos prioritásszerkesztés** — a teljes prioritási lista *bármelyik* megnyitott
  példányból szerkeszthető, nem csak abból, ami épp a képernyőn van. Mivel egy plugin-példánynak
  nincs mutatója egy másik példány paraméterfájára, ez a regiszterben lévő kérés-csatornán megy
  keresztül; ütközéskor a két prioritás helyet cserél, nem pedig némán felülíródik az egyik.
- **Spektrum-megjelenítő és prioritási lista** — a teljes hálózat állapota minden példányból látható.

## Paraméterek

| Paraméter | Tartomány | Alapérték | Leírás |
|---|---|---|---|
| Priority | 1 – 10 | 5 | A sáv rangja a hálózatban. Az alacsonyabb prioritású sávok adják át a helyet a magasabbaknak. |
| Amount | 0 – 100 % | 100 % | Az elfedett sávokra alkalmazott erősítéscsökkentés mértéke. |
| Attack | 5 – 200 ms | 10 ms | Milyen gyorsan lép be a halkítás. |
| Release | 5 – 200 ms | 50 ms | Milyen gyorsan enged vissza. |
| Bypass | be / ki | ki | Teljes kihagyás; a példány regisztrálva marad. |

## Használat

1. Tegyél egy példányt minden sávra, amely ugyanazért a spektrális helyért verseng.
2. Állíts be prioritást mindegyiken — vezető ének magasat, pad alacsonyat, például.
3. Ízlés szerint állítsd az Amount, Attack és Release értékeket. A prioritási lista bármelyik
   példányból átrendezhető.

## Telepítés

A kész binárisok a [Releases](https://github.com/rcptr2/gabcis-smartmask-network/releases)
oldalon találhatók.

### Windows x64

1. Töltsd le a `SmartMaskNetwork-vX.Y.Z-Windows-x64-VST3.zip` fájlt.
2. Csomagold ki, és másold a `SmartMask Network.vst3` mappát ide: `C:\Program Files\Common Files\VST3\`.
3. Futtass plugin-újrakeresést a DAW-odban.

### macOS (Intel)

A macOS bináris **x86_64 (Intel)**. Intel Maceken natívan fut, Apple Siliconon Rosetta 2-vel, Intel
módban futó hosztban; arm64 változat nincs.

1. Töltsd le a `SmartMaskNetwork-vX.Y.Z-macOS-Intel-VST3.zip` fájlt.
2. Csomagold ki, és másold a `SmartMask Network.vst3`-at ide: `/Library/Audio/Plug-Ins/VST3/`
   (vagy `~/Library/Audio/Plug-Ins/VST3/`, ha csak a saját felhasználódnak kell).
3. A build nincs notarizálva, ezért töröld róla a karantén jelzőt:
   ```bash
   xattr -dr com.apple.quarantine "/Library/Audio/Plug-Ins/VST3/SmartMask Network.vst3"
   ```
4. Futtass plugin-újrakeresést a DAW-odban.

> A példányok egyetlen hoszt-folyamaton belül kommunikálnak. Külön folyamatban futtatott sávok —
> egyes DAW-ok külön homokozóba teszik a pluginokat — nem látják egymást.

## Fordítás forrásból

### Követelmények

- CMake 3.24 vagy újabb
- C++20-as fordító — Windowson **Visual Studio 2022** („Desktop development with C++"),
  macOS-en **Xcode 15+**
- Git

A JUCE 9.0.0 verziója rögzítve van a `CMakeLists.txt`-ben, és a CMake `FetchContent` konfiguráláskor
automatikusan letölti. MinGW nem támogatott: a JUCE kifejezetten tiltja, és a Windows-backendje
MSVC-intrinsiceket, valamint a Direct2D/DirectWrite fejléceket igényel.

> **A build-mappa útvonalában nem lehet aposztróf.** A JUCE által generált VST3 `POST_BUILD` lépések
> nem escape-elik az aposztrófot az általuk kiadott shell-parancsláncokban. A `CMakeLists.txt` ezt
> ellenőrzi, és érthető hibaüzenettel áll le ahelyett, hogy később hasalna el.

### Windows

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DSMARTMASK_BUILD_TESTS=OFF
cmake --build build --config Release --target SmartMaskNetworkPlugin_VST3
```

### macOS

```bash
cmake -S . -B build -G Xcode -DCMAKE_OSX_ARCHITECTURES=x86_64 -DSMARTMASK_BUILD_TESTS=OFF
cmake --build build --config Release --target SmartMaskNetworkPlugin_VST3
```

A kész csomag ide kerül:
`build/SmartMaskNetworkPlugin_artefacts/Release/VST3/SmartMask Network.vst3`.

### Tesztek

A tesztkészlet [Catch2](https://github.com/catchorg/Catch2)-t használ (automatikusan letöltődik), és
regisztrál a CTest-be. Lefedi a regisztert, a spektrális motort, az elfedés-processzort, magát a
processzort és a CPU-terhelést:

```bash
cmake -S . -B build -DSMARTMASK_BUILD_TESTS=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

## Mappastruktúra

```
Source/          A plugin forrása — processzor, szerkesztő, regiszter, spektrális motor,
                 elfedés-processzor, spektrum-megjelenítő, prioritáslista-komponens
Tests/           Catch2 egységtesztek — regiszter, spektrális motor, elfedés, processzor, terhelés
docs/            PDF-ismertetők (EN/HU) és fejlesztési terv
CMakeLists.txt   Build-definíció; rögzíti a JUCE 9.0.0-t
CHANGELOG.md     Fejlesztési előzmények
```

## Tesztelve

- **macOS** (Intel, x86_64) — FL Studio 2026
- **Windows 11 x64** — FL Studio 2026

## Állapot

0.13.0-s verzió. A főverzió `0` marad, amíg a fejlesztés funkcionálisan kész, tesztelt állapotban
szünetel; részletek a [CHANGELOG.md](CHANGELOG.md)-ben.

## Teljesítmény

![FL Studio plugin-teljesítménymérő](docs/images/performance-monitor.png)

FL Studio 2026-ban mérve, Windows 11 x64 alatt, egy ASUS ZenBook 13-on, Intel
Core i7-1065G7 processzorral — ez egy alacsony fogyasztású, négymagos laptop-CPU,
nem munkaállomás. Mind a hét plugin egyszerre futott ugyanabban a projektben, két
gyári Image-Line pluginnal együtt, viszonyítási alapnak. A számok az FL Studio
sajátjai, *Reset on transport* bekapcsolva, hogy az egyszeri indulási tüskék
kimaradjanak.

| Plugin | CPU % | Time | Peak |
|---|---:|---:|---:|
| Gabci's AeroDynamics Pro | 17 | 251 | 353 |
| FLEX Bass *(Image-Line, viszonyítás)* | 9 | 125 | 275 |
| Gabci's MasterClear | 4 | 53 | 264 |
| **Gabci's SmartMask Network *(1. példány)*** | **3** | **43** | **554** |
| Gabci's PhaseLock Sub | 3 | 41 | 1306 |
| Emphasizer *(Image-Line, viszonyítás)* | 2 | 34 | 117 |
| Gabci's Acoustic Cloak | 2 | 36 | 191 |
| Gabci's MorphicPhaser | 2 | 27 | 152 |
| **Gabci's SmartMask Network *(2. példány)*** | **1** | **16** | **498** |
| Gabci's SpectralCarve Pro | 1 | 19 | 751 |

## Licenc

**GNU Affero General Public License v3.0 vagy újabb** alatt jelenik meg — lásd a [LICENSE](LICENSE)
fájlt.

Ez a választás nem önkényes. A SmartMask Network JUCE 9-cel készült, amely kettős licencű: AGPLv3
vagy kereskedelmi JUCE-licenc. Az AGPLv3 ág az, ami ingyenesen engedi a forrásból épített bináris
terjesztését — cserébe minden származtatott művet ugyanezen feltételek alatt, elérhető forrással kell
kiadni.

## Attribúció

- [JUCE](https://juce.com) — © Raw Material Software Limited, itt AGPLv3 alatt használva.
- [Catch2](https://github.com/catchorg/Catch2) — Boost Software License 1.0 (csak teszt-buildekhez).
- A VST® a Steinberg Media Technologies GmbH bejegyzett védjegye. A JUCE-szal szállított VST 3 SDK-t
  a Steinberg MIT licenc alatt terjeszti.

## Szerző

Tomori Gábor — *Gabci Audio*
