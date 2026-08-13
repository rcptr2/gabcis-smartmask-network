# Changelog

Verziószámozás: a `versioning-and-changelog-rule` szerint minden fejlesztési kör emeli a MINOR verziót.

## Kiadás lezárása (v0.13.0 mellett) — 2026-08-04

Nincs kódváltozás, csak a végleges disztribúciós csomag összeállítása — a fejlesztés
ezzel egyelőre szünetel (a felhasználó kérésére).

- A v0.13.0 forrásból friss VST3, AU és Standalone build készült, és bekerült a projekt
  saját `Build/` mappájába (`Build/VST3`, `Build/AU`, `Build/Standalone`) a forrás mellé
  — korábban csak a felhasználó rendszerszintű plugin mappájában létezett telepített
  példány.
- Két rövid, de alapos PDF-ismertető készült a projekt gyökerében:
  `Gabcis_SmartMaskNetwork_Ismerteto.pdf` (magyar) és
  `Gabcis_SmartMaskNetwork_Overview.pdf` (angol) — funkciók, teljesítmény-mérések,
  belső architektúra, vizuális kezelőfelület-útmutató, felhasznált külső források és a
  hivatalos attribúció.

## 0.13.0 — 2026-08-03

Prioritás-lista: teljes munkameneti szerkeszthetőség bármelyik nyitott példányból + csere ütközéskor.

- **Csak a saját sor volt húzható** (felhasználói kérés, 2026-08-03: "bármelyik éppen
  megnyitott pluginből lehessen szerkeszteni a teljes prioritási listát"). Architekturális
  ok: egy plugin-példánynak nincs közvetlen mutatója egy másik betöltött példány saját
  `AudioProcessorValueTreeState`-jére, tehát korábban valóban nem lehetett máshonnan írni
  egy másik sáv prioritását. Megoldás: új, keresztpéldányos **kérés-csatorna** a
  regiszterben (`Source/SmartMaskRegistry.h/.cpp`,
  `requestPriorityChange()`/`takeRequestedPriorityChange()`, ugyanaz az egy-cellás
  atomic-alapú minta, mint a spektrum/mask-gain dupla-pufferelésnél) — bármely példány
  "kérhet" egy prioritásváltást egy másik slotnak, de sosem írja közvetlenül annak
  paraméterét (ami megkerülné a hoszt saját automatizáció/undo-kezelését arra a
  példányra nézve). A KÉRÉS célja saját maga alkalmazza a saját `AudioProcessorValueTreeState`
  -jén keresztül.
- A kérést fogadó oldal **nem a szerkesztő-ablakban**, hanem magában a `SmartMaskAudioProcessor`
  -ban lett bekötve (`Source/PluginProcessor.h/.cpp`, új `juce::Timer`, 30Hz): ha csak a
  `PriorityListComponent`-ben fogadtuk volna, egy olyan sáv prioritása, aminek épp nincs
  nyitva a szerkesztő-ablaka, sosem tudott volna változni. A processzor-szintű timer a
  plugin teljes élettartama alatt fut, függetlenül attól, nyitva van-e az ablaka.
- **Egymásra húzás → csere, nem ütközés** (felhasználói kérés): ha egy prioritási helyre
  húzol egy sávot, ahol már van másik, a két sáv helyet cserél, ahelyett hogy ugyanazon
  a prioritáson maradnának egymás mellett. Ehhez a `PriorityListComponent` teljes
  átírása kellett: minden sorban minden egyes sáv címkéje most saját, egyedi
  hitbox-szal rendelkezik (`lastPaintedEntries`, `paint()`-ban minden `paint()` hívásnál
  újraépítve), így `mouseDown()` pontosan meg tudja állapítani, MELYIK sávot (sajátot
  vagy másikét) kattintottad meg — nem csak azt, hogy melyik SORBAN kattintottál. Korábban
  bárhová kattintva mindig csak a saját sávot lehetett mozgatni, most bármelyik látható
  sáv címkéje egyedileg fogható meg és húzható.
- Regresszió: mind a három tesztbinárison (`SmartMaskRegistryTests` 101,
  `MaskingProcessorTests` 10876, `PluginProcessorTests` 83 assertion) zöld.

## 0.12.0 — 2026-08-03

A 0.11.0-s prioritás-sorrend-javítás nem látszott élesben: a regiszter csak lejátszás közben szinkronizálódott.

- **Gyökérok**: `SmartMaskRegistry::setPriority()`/`setAmount()` eddig kizárólag
  `PluginProcessor::processBlock()`-ból lett hívva, ami viszont csak akkor fut le, ha a
  hoszt ténylegesen lejátszást futtat. Megállított transport mellett (pl. frissen felrakott,
  még el sem indított plugin) a megosztott regiszter soha nem kapta meg sem a 0.11.0-ban
  bevezetett automatikus induló prioritást, sem egy kézi húzást a prioritás-listában — a
  UI ehelyett a regiszter saját, `registerTrack()`-ben beállított belső alapértékét
  (prioritás 10) mutatta, és úgy tűnt, mintha a húzás "nem fogna".
  [Felhasználói visszajelzés, 2026-08-03: mindkét friss példány a 10. helyen jelent meg,
  áthelyezhetetlenül.]
- **Javítás** (`Source/PluginProcessor.cpp` konstruktor, `Source/PriorityListComponent.cpp`
  `mouseUp()`): mindkét helyen, közvetlenül a `setValueNotifyingHost()` hívás után, szinkron
  módon is közvetlenül publikáljuk az új értéket a regiszterbe
  (`SmartMaskRegistry::setPriority()`/`setAmount()`), lejátszási állapottól függetlenül.
  `processBlock()` továbbra is minden blokkban újraküldi ugyanezt — ez most redundáns, de
  ártalmatlan (egyetlen relaxed atomic store), és nem bontottuk meg, hogy lejátszás közbeni
  viselkedés ne változzon.
- Regresszió: mind a három tesztbinárison (`SmartMaskRegistryTests` 101,
  `MaskingProcessorTests` 10876, `PluginProcessorTests` 83 assertion) zöld.

## 0.11.0 — 2026-08-03

Élő FL Studio-teszt közben talált két valós hiba javítása: alapértelmezett prioritás-ütközés + félrevezető sávnevek.

- **Alapértelmezett prioritás-ütközés** (`Source/PluginProcessor.cpp`, konstruktor): minden
  új példány eddig a `priority` paraméter statikus alapértelmezett indexére (4 → prioritás
  5) állt be, függetlenül attól, hányadikként töltötték be — tehát ha a felhasználó 5
  sávra egymás után rakta fel a pluginet, mind az 5 ugyanarra a prioritás-sorra került. A
  javítás: a konstruktorban, `registerTrack()` után, a `priority` paramétert explicit
  `registrySlotIndex`-alapú (0..9-re korlátozott) értékre állítjuk — ez az adott
  munkamenetben "hányadikként lett betöltve" sorszámot ad, tehát az 1. plugin az 1.,
  a 2. a 2. prioritásra kerül, stb., pontosan ahogy a felhasználó kérte. Mentett projekt
  visszatöltésekor ez nem okoz problémát: a host ezután hívja meg a `setStateInformation`
  -t, ami a valódi mentett prioritással felülírja ezt az alapértéket, ugyanúgy mint bármely
  más paramétert.
- **Prioritás-lista sorai ténylegesen egymásra rajzolták a szöveget**
  (`Source/PriorityListComponent.cpp`): ha két sáv osztozott egy prioritáson (akár a fenti
  hiba miatt, akár szándékosan), a régi kód mindkettő nevét pontosan ugyanarra a helyre
  rajzolta ugyanabban a sorban. Javítva: a "This track" címke (ha ott van) fehérrel balra,
  utána a többi azonos prioritású sáv neve vesszővel elválasztva, világosszürkével,
  a saját címke tényleges (mért) szélessége utáni pozíciótól — nem fedik többé egymást.
- **Félrevezető "Track #N" sávnevek** (`Source/SmartMaskRegistry.h/.cpp`,
  `Source/PluginProcessor.h/.cpp`, `Source/PriorityListComponent.cpp`): a felhasználó
  screenshoton mutatta, hogy a 2-es sávon megnyitott plugin a másik példány listájában
  "Track #6"-ként (egy belső, regisztrációs sorrend szerinti azonosító) jelent meg, nem
  a mixerben látható valódi néven (pl. "Insert 5"). Megoldás: `AudioProcessor::
  updateTrackProperties()` felülírás — ez a VST3 szabvány opcionális IInfoListener
  csatorna-kontextus mechanizmusán keresztül a hoszt (ha támogatja) közli a pluginnel a
  saját sávjának valódi nevét/színét. A kapott nevet a regiszter egy új, ugyanazzal a
  dupla-pufferelt atomic-pointer-csere mintával publikálja, mint a spektrum/mask-gain
  adatokat (nem `juce::String`, hanem egy fix méretű `char` tömb, hogy a
  `SmartMaskRegistry.h` JUCE-független maradjon, ahogy a Fázis 1 óta volt). A prioritás
  -lista mostantól ezt a nevet mutatja, ha a hoszt megadta; ha nem (nem minden hoszt
  támogatja ezt a VST3-mechanizmust), a régi "Track #N" cím marad tartalék megoldásként —
  tehát nincs regresszió olyan hosztokon, amelyek nem küldik el ezt az adatot.
- Regresszió: mind a három Catch2 tesztbinárison lefuttatva (`SmartMaskRegistryTests` 101,
  `MaskingProcessorTests` 10876, `PluginProcessorTests` 83 assertion) — mindegyik zöld.

## 0.10.0 — 2026-08-03

Felhasználói FL Studio-teszt utáni finomítások: Bypass gomb + valódi Hz-frekvenciatengelyes vizualizáló.

- **Bypass paraméter** (`Source/PluginProcessor.h/.cpp`): új `AudioParameterBool "bypass"`
  (alapértelmezett: kikapcsolva). Bekapcsolva ez a példány a saját maszk-gain-jeit
  egységnyire (1.0) kényszeríti `processBlock()`-ban, ahelyett hogy megkerülné a
  teljes STFT-feldolgozási láncot — így a jelentett latency (`kLatencySamples`)
  végig állandó marad, tehát lejátszás közben is kattanásmentesen kapcsolható. A
  regiszterbe ekkor is az őszinte (egységnyi) gain-eket publikáljuk, hogy a
  vizualizáló ne mutasson hamis maszkolást, és a többi példány maszkolása erre a
  sávra nézve érintetlen maradjon. UI-oldalon egy `juce::ToggleButton` +
  `ButtonAttachment` a fejlécben (`Source/PluginEditor.h/.cpp`).
- **Spektrum-vizualizáló átdolgozása** (`Source/SpectrumVisualizerComponent.h/.cpp`):
  a felhasználó közvetlen visszajelzése alapján ("nem túl részletes... meg sem
  lehet állapítani, melyik frekvencián dominál") a korábbi, felirat nélküli,
  lineáris Bark-sáv-indexes X tengelyt egy valódi, címkézett Hz-frekvenciatengely
  váltja fel (logaritmikus, 20 Hz-20 kHz, évtizedes rácsvonalak 100/300/1k/3k/10k
  Hz-nél). Mivel a Zwicker-formulának nincs zárt alakú inverze, az egyes
  Bark-sávok középfrekvenciáját egyszeri bisection-nel határozzuk meg (a
  vizualizáló-komponensben, csak a message threadről hívva, nem az audio útvonalon
  — így ez nem sérti a 4. fejezet valós idejű szabályait). Emellett:
  - dB-rácsvonalak és -feliratok a bal szélen (10 dB-enként);
  - egérrel követhető crosshair, ami kiírja a pontos frekvenciát és — ha ez a
    saját sáv aktív — a saját sáv pontos dB-szintjét az adott frekvencián
    (a két legközelebbi Bark-sáv közti lineáris interpolációval);
  - a saját sáv esetén egy második, szaggatott vonal mutatja a nyers
    (maszkolás előtti) energiát is, és a nyers/maszkolt görbe közti rés
    áttetsző piros kitöltést kap — ez a "collision" nézet pontosan megmutatja,
    mely frekvenciákon és mennyire csillapítja épp a maszkolás a saját sávot
    (a felhasználó által mutatott FabFilter Pro-Q4 "Show Collisions"
    funkciójához hasonló elgondolás, de ez a nézet a saját sáv tényleges
    audio-kimenetét tükrözi, nem egy heurisztikus ütközés-becslést).
- Nem változtatott, de tisztázott kérdés (a válasz a felhasználónak adott
  szöveges válaszban, nem kódban): a beépülők szerkesztő-ablakainak egymásra
  helyezése/elrendezése a hosztprogram (FL Studio) ablakkezelésének felelőssége —
  a VST3/AU specifikáció nem ad a beépülőnek eszközt arra, hogy saját ablakának
  képernyőn belüli pozícióját a hoszt keretén belül szabályozza vagy más
  beépülők ablakaival ütközést kerüljön; ez nem javítható a plugin oldaláról.
- Regresszió: `MaskingProcessorTests` (10876 assertion) és `PluginProcessorTests`
  (83 assertion, beleértve az RT-safety allokáció-őrt és a két-példányos
  integrációs tesztet) továbbra is zöld a Bypass-ág hozzáadása után.

## 0.9.0 — 2026-08-03

Fázis 6: SIMD Optimalizáció & Terheléses Tesztelés.

- **Ableton Live / Reaper helyett**: egyik sincs telepítve ezen a gépen (ellenőrizve).
  Helyette egy szigorú, automatizált 32-példányos terhelésteszt (`Tests/LoadTests.cpp`):
  32 valós `SmartMaskAudioProcessor` egy processzben, szétosztott 1-10 prioritással
  (valódi versengő maszkolás minden blokkon, nem néma állás), mindegyik saját
  `processBlock()`-jának tényleges falióra-idejét mérve. Mért adat, nem becslés.
- `Source/MaskingProcessor.h/.cpp`: `juce::FloatVectorOperations::multiply` a
  gain-szorzásokhoz (spec 1. lépése) — a bin-gain-eket duplikálva (interleavelve, a
  komplex re/im párokhoz illesztve) egyetlen SIMD-hívás váltja ki az 1024-elemes
  skalár ciklust.
- **Mért eredmény — első kör**: `Release` build (-O3) nélkül a mérés értelmetlen lett
  volna (a build könyvtár korábban optimalizálatlan volt) — újrakonfiguráltam
  `-DCMAKE_BUILD_TYPE=Release`-szel. Alapállapot (csak a SIMD gain-szorzással):
  **1.06% CPU/példány** 96kHz-en, 512 mintás blokkokkal — a cél 0.3% majdnem
  3.5×-öse.
- **Gyökérok-elemzés**: csatornánként 6 db 2048-pontos FFT történt hoponként (2
  csatorna × [1 elemző FFT a SpectralEngine-ben + 1 REDUNDÁNS elemző FFT a
  MaskingProcessor-ban ugyanazon ablakozott mintákon + 1 inverz FFT]) — a
  SpectralEngine és a MaskingProcessor mindig ugyanazt a mintafolyamot kapta
  azonos hop-időzítéssel, tehát a két "elemző" FFT kimenete mindig számszerűen
  azonos volt. Ez egy valódi, mérhető redundancia volt, nem álprobléma.
  - `SmartMaskRegistry::getGlobalMask()` (32 slot pásztázása) **nem** bizonyult
    számottevő költségnek (Instruments-mérésben csak ~7/196 minta) — a korábbi
    feltételezésem, hogy ez lenne a fő költség, tévesnek bizonyult a tényleges
    profilozás után.
- **Javítás — megosztott FFT**: `SpectralEngine` mostantól megőrzi a hoponkénti
  komplex spektrumot (`getPendingSpectrum()`), a `MaskingProcessor::process()` pedig
  ezt használja fel a saját (redundáns) elemző FFT-je helyett — 6 FFT/hop helyett 4.
  - Ehhez a `MaskingProcessor::process()` szignatúrája megváltozott: nyers minták
    helyett a hozzá tartozó `SpectralEngine`-t kapja paraméterként.
  - **Mért hiba egy köztes lépésben**: a pufferelt-spektrum-sor méretét elsőre
    túlbiztosítottam (`kMaxHopsPerCall = 32`, 512KB/csatorna) — ez ténylegesen
    **rontott** a teljesítményen (2.16%-ra, feltehetően cache-nyomás miatt), mivel a
    valós esetben szinte sosem kell 1-2 hopnál többet pufferelni. Csökkentve
    `8`-ra (32KB/csatorna) → **0.958%**.
  - Új regressziós teszt (`Tests/MaskingProcessorTests.cpp`): a korábbi tesztek mind
    pontosan `kHopSize` méretű darabokban dolgoztak (mindig pontosan 1 hop/hívás) —
    sosem gyakorolták be a 0 vagy 2+ hop/hívás esetet, amit ez a refaktor most
    ténylegesen érint. Új teszt 128 és `3*kHopSize+37` mintás blokkmérettel —
    10876 assertion, mind zöld, a pontos (1e-3 tűrésű) helyreállítási teszt
    változatlanul teljesít.
- **Javítás — valódi profilozással irányított finomhangolás**: `xcrun xctrace`
  (Time Profiler sablon) + macOS `sample` paranccsal ténylegesen megprofilozva a
  terheléstesztet (nem találgatással). Kiderült: a `SpectralEngine::pushSamples()`
  körkörös puffer írása (mintánkénti modulo-indexeléssel) valós, nem elhanyagolható
  költség volt. Javítás: a kör-puffer írása/olvasása (mind `pushSamples()`, mind
  `processFrame()`) legfeljebb 2 összefüggő `std::copy`-ra bontva a hullámzási pont
  körül (nem elemenkénti modulo-indexelés), + a `MaskingProcessor` OLA-akkumulátor
  összeadása `juce::FloatVectorOperations::add`-dal, ugyanezzel a "hullámzás körüli
  2 részre bontás" technikával. → **0.824%**.
- **Összesített eredmény**: 1.06% → 0.824% (~22%-os valós javulás), de még mindig
  ~2.7×-öse a 0.3%-os célnak. A megmaradó költség a Instruments-mérés szerint túlnyomó
  részt a KÉT, immár nem redundáns FFT-művelet (elemző + inverz, csatornánként) —
  ezek a spec által előírt 2048-pontos/75%-os átfedésű STFT-hez eleve szükségesek,
  további csökkentésük vagy a spec-előírt paraméterek megsértését igényelné (kisebb
  FFT-méret vagy kevesebb átfedés — nem opció), vagy egy jóval invazívabb átalakítást
  (pl. a két sztereó csatorna egyetlen komplex FFT-be csomagolása a klasszikus
  "valós-jel-párosítás" trükkel, ami a FFT-számot felezné, de a rekonstrukciós
  pipeline jelentős, kockázatosabb átírását igényelné) — ezt tudatosan NEM kezdtem
  el ebben a körben, a jól tesztelt, működő kód biztonságát előnyben részesítve a
  további, egyre kockázatosabb optimalizációval szemben. A felhasználó eldöntheti,
  szeretné-e ezt a nagyobb átírást egy külön körben.
- Mért eredmény: mind az 5 teszt-suite zöld (101+15+10876+83 = 11075 assertion a
  4 funkcionális suite-ban, + a LoadTests saját mérése), ténylegesen újrafordítva
  `-DCMAKE_BUILD_TYPE=Release`-szel és lefuttatva.

## 0.8.0 — 2026-08-03

Kritikus vizuális/UX hiba javítása: a spektrum-kijelző nem mutatta a maszkolást.

- **A hiba** (egy másik AI code review-ja találta, a 0.7.0 changelog átvizsgálásakor):
  a `SpectrumVisualizerComponent` keretenkénti RELATÍV skálázást használt (mindig az
  éppen leghangosabb sáv érte el a tetőt, 30x/másodperc újraszámolva). Ez elrejti a
  valós dinamikát — pont a maszkolás vizuális igazolását teszi lehetetlenné, mert a
  referenciapont folyamatosan ugrál.
- **Mélyebb, a kritikában nem nevesített, de ugyanoda vezető hiba, amit saját
  vizsgálattal találtam**: a kijelző a `registry.updateSpectrum()`-mal publikált NYERS
  (bemeneti, maszkolás ELŐTTI) energiát rajzolta ki, nem a ténylegesen kimenő
  (maszkolt) jelet. Emiatt még egy FIX skála mellett sem látszott volna a maszkolás —
  a kijelzett adat maga sosem tükrözte, mit csinál valójában a `MaskingProcessor`.
- **A javasolt formula (fix -60dB..0dB tartomány) jó irány, de a mértékegység nem
  illik rá közvetlenül**: a kódbázis saját Bark-energia mértékegysége nem kalibrált
  dBFS-re (egy 1.0 amplitúdójú teli szinusz csúcssávja kb. 60-70 `10*log10(energia)`
  egységet mér, nem 0-t) — ezért `0..70` egységet választottam fix tartománynak,
  ugyanazzal a "fix, nem keretenkénti" elvvel, csak a kódbázis saját egységeire
  kalibrálva.
- **A tényleges javítás mindkét problémára**:
  - `Source/MaskingProcessor.h/.cpp`: a `process()` mostantól előre kiszámított
    `bandGains`-t vár paraméterként (nem saját maga hívja a `computeBandGains()`-t) —
    ezt a hívó (`PluginProcessor`) egyszer számolja ki blokkonként (a korábbi duplikált
    számítás helyett, mivel minden csatorna ugyanazt az own/competing/amount hármast
    látta), és ugyanezt a görbét publikálja is a registrynek.
  - `Source/SmartMaskRegistry.h/.cpp`: új `updateMaskGains()` + `SlotSnapshot::
    maskGains` — a ténylegesen alkalmazott csillapítási görbét is publikálja, nem csak
    a nyers bemeneti energiát. Friss regisztrációnál alapértelmezetten egység-erősítés
    (1.0), nem néma (0.0), hogy a GUI ne olvassa félre "nincs adat"-ként "elnémítva"-nak.
  - `Source/SpectrumVisualizerComponent.h/.cpp`: fix `0..70` egységes tartomány (nem
    keretenkénti relatív skála), és a kirajzolt görbe mostantól a MASZKOLT energia
    (`bandEnergies * maskGains²` — a gain amplitúdóra hat, energiára négyzetesen), nem
    a nyers bemenet.
- `Tests/SmartMaskRegistryTests.cpp`: új assertek — friss slot alapértelmezett
  egység-erősítése, `updateMaskGains()` helyes tárolása/visszaolvasása.
- `Tests/PluginProcessorTests.cpp`: a két-példányos integrációs tesztbe új assert —
  a registry által publikált `maskGains` a pad versengő sávjában ténylegesen < 0.6
  (nem csak a hallható RMS csökken, a GUI által olvasott adat is helyesen tükrözi a
  maszkolást). Ez pontosan az a regresszió-védelem, ami a "kijelző nyers bemenetet
  mutat, nem a maszkolást" hibát megbuktatná, ha visszatérne.
- Mért eredmény: mind a 4 teszt-suite zöld (101+15+3708+83 = 3907 assertion),
  vizuálisan is ellenőrizve a Standalone appban — csendben a kijelző most helyesen az
  alja felé ül (korábban tévesen a tetejére "ütött" a hibás relatív skálázás miatt).

## 0.7.0 — 2026-08-03

Fázis 5: Vektoros GUI és Spektrális Hálózati Kijelző.

- `Source/SmartMaskRegistry.h/.cpp`: új `SlotSnapshot` + `getActiveSlotsSnapshot()` —
  a GUI-nak (nem a real-time útvonalnak) szüksége van arra, hogy fel tudja sorolni az
  ÖSSZES aktív track-et (prioritás, energia, trackId), nem csak a sajátját. Lock-free
  atomic olvasásokkal, ugyanúgy mint a real-time metódusok, de a szerződés szerint
  csak a GUI ~30Hz timeréből hívandó.
- `Source/SpectrumVisualizerComponent.h/.cpp`: élő, több-sávos Bark-spektrum kijelző —
  minden aktív registry-slot egy színes görbe (prioritás szerinti színkódolással: 1=meleg
  piros, 10=hideg kék), 30Hz `juce::Timer`-rel újrarajzolva (sosem az audio szálról).
  Relatív, keretenkénti log-skála (a legjelenleg leghangosabb sáv éri el a tetőt), nem
  kalibrált dBFS-mérő — a cél a sávok közti versengés megjelenítése, nem abszolút szint.
- `Source/PriorityListComponent.h/.cpp`: drag-and-drop prioritás-átrendező lista.
  **Tervezési döntés** (a spec nem tér ki rá): csak a SAJÁT track sora húzható —
  máshogy nem lehetne biztonságosan írni egy MÁSIK, külön betöltött példány saját
  APVTS-paraméterét. A többi aktív track sora csak olvasható kontextusként jelenik meg.
- `Source/PluginEditor.h/.cpp`: az új editor, felváltja a Fázis 4-es
  `GenericAudioProcessorEditor` ideiglenes megoldást. Tartalmazza a spektrum-kijelzőt,
  a prioritási listát, Amount/Attack/Release forgó csúszkákat (APVTS
  SliderAttachment-tel), valamint a felhasználó által utólag kért branding-et:
  - "SMN" logó-jelvény (vektorosan rajzolva, nem képfájl) + "Gabci's SmartMaskNetwork"
    felirat a fejlécben.
  - About panel (kattintásra bárhol bezáródik) az aláírás-szöveggel.
  - **Minden megjelenített szöveg és paraméternév angolul** (Priority/Amount/Attack/
    Release, "This track", "No active tracks yet", stb.) — a felhasználó külön kérte
    ezt; az aláírás-szöveg maga kivétel, mert azt szó szerint, magyarul diktálta.
- **Mért hiba és javítás — UTF-8 mojibake**: az About panel elsőre "A kÃ³dot Tomori
  GÃ¡bor Ã©s Gemini..." formában jelent meg. Ellenőriztem: a JUCE `String(const char*)`
  konstruktora csak ASCII-t kezel (a JUCE saját dokumentációja kifejezetten
  `CharPointer_UTF8(...)`-ot javasol UTF-8 forráshoz) — enélkül a magyar ékezetes
  karakterek minden bájtja külön (hibás) karakterként jelent meg. Javítva:
  `juce::String (juce::CharPointer_UTF8 (kAttributionText))`.
- **Mért hiba és javítás — About gomb**: az első kattintás(ok) nem nyitották meg a
  panelt megbízhatóan. Manuálisan (Standalone build, számítógép-vezérléssel) tesztelve
  és megerősítve: végül helyesen működik minden kattintásra.
- **Felhasználói visszajelzés alapján javítva — ablakméret**: az editor mostantól
  átméretezhető (`setResizable` + `setResizeLimits(600, 420, 1600, 1000)`), az
  alapértelmezett méret 720×480-ról 900×620-ra nőtt, és a `resized()` elrendezés
  arányosan skálázódik (nem fix pixelben), így a csúszkák és a prioritási lista is
  ténylegesen nagyobb lesz nagyobb ablaknál, nem csak a spektrum-kijelző.
- Mért eredmény: mind a 4 teszt-suite zöld (74+15+3708+81 = 3878 assertion),
  vizuálisan is ellenőrizve a ténylegesen felépített Standalone alkalmazásban.

## 0.6.0 — 2026-08-03

Aláírás elhelyezése a kódban + Fázis 5 branding-igény rögzítése.

- `Source/PluginProcessor.h`: fájl tetején aláírás-komment ("A kódot Tomori Gábor és
  Gemini ötlete alapján készítette a Claude Code (Sonnet 5), a folyamat felügyeletét
  a Gemini végezte, 2026."), valamint egy `kAttributionText` megosztott konstans
  ugyanezzel a szöveggel — ezt fogja majd a Fázis 5 About-panelje szó szerint
  felhasználni, hogy ne kelljen máshol duplikálni/elszakadni tőle.
- **Fázis 5-ös felhasználói igény rögzítve** (még nincs megvalósítva, ez csak Fázis 4
  utáni gyors kódszintű kiegészítés, a GUI maga Fázis 5 feladata): a kezelőfelületen
  nem túl zavaró helyen jelenjen meg egy "Gabci's SmartMaskNetwork" felirat + logó
  (professzionális, minimalista, "SMN" rövidítéssel), és az About részben ugyanez az
  aláírás-szöveg + forrásmegjelölés.
- Mért eredmény: mind a 4 teszt-suite továbbra is zöld (3866 assertion), a build
  változatlanul tisztán fordul.

## 0.5.0 — 2026-08-03

Kritikus DSP-hiba javítása: fáziskioltás a sztereó mono-lekeverésnél.

- **A hiba** (egy másik AI code review-ja találta, 0.4.0 changelog átvizsgálásakor):
  `SmartMaskAudioProcessor::processBlock()` a bal és jobb csatornát egyszerű
  időtartománybeli összeadással (`L[i] + R[i]`) keverte monóba a `SpectralEngine`
  elemzéséhez. Ha egy sztereó jel (pl. chorusolt pad, széles sztereó effekt) csatornái
  részben vagy teljesen ellenfázisúak, ez az összeadás kioltja őket — az analizátor
  nulla (vagy a valósnál drasztikusan kisebb) energiát mér, hiába szól hangosan a
  hangszer, és a maszkolás emiatt nem (vagy hibásan) aktiválódik. Ellenőriztem a
  konkrét kódban (`PluginProcessor.cpp` akkori 97-103. sora) — a probléma valós volt.
- **A javasolt formula (L²+R² a nyers mintákon, FFT előtt) technikailag NEM helyes**:
  a négyzetre emelés/egyenirányítás az FFT elé kerülve eltorzítja magát az elemzett
  jelet (frekvenciákat duplázza, felharmonikusokat és DC-eltolást visz be) — ez nem
  "fáziskioltás-mentesítés", hanem az elemzett jel maga sérülne meg.
- **A ténylegesen helyes javítás**: minden csatornát ÖNÁLLÓAN, saját nyers mintáin
  elemez egy-egy külön `SpectralEngine` (nincs többé időtartománybeli lekeverés), és
  csak az EBBŐL adódó, már eleve nemnegatív sáv-energiákat összegezzük csatornánként.
  Energia sosem tud kioltani (magnitúdó-négyzet mindig ≥0) — ugyanez az elv, amit a
  valós hangosság-mérés (pl. ITU-R BS.1770) is használ: csatornánkénti teljesítményt
  összegez, sosem kever előbb időtartományban.
  - `Source/PluginProcessor.h/.cpp`: `spectralEngine` (1 db) → `spectralEngines`
    (csatornánként 1-1, `kMaxSupportedChannels` méretű tömb, ugyanaz a minta, mint a
    `maskingProcessors`-nál már eddig is). A `monoScratch` puffer megszűnt, nincs rá
    szükség.
  - `Tests/PluginProcessorTests.cpp`: új regressziós teszt — egy "széles pad" példány
    tökéletesen ellenfázisú sztereó hangot kap (L=sin, R=-sin), egy különálló,
    alacsonyabb prioritású "listener" példány pedig ugyanazt a hangot monóban; a
    listenernek mérhetően le kell halkulnia a widePad versengő energiája miatt —
    pontosan ez a teszt buktatta volna meg a régi, hibás kódot (a régi mono-összeadás
    mellett widePad energiája pontosan nulla lett volna minden sávban).
  - Mért eredmény: mind a 4 teszt-suite zöld (62+15+3708+81 = 3866 assertion).

## 0.4.0 — 2026-08-03

Fázis 4: JUCE AudioProcessor & APVTS Integráció + tényleges pluginolható VST3/AU/Standalone build.

- `Source/PluginProcessor.h/.cpp` (`SmartMaskAudioProcessor`): összeköti az 1-3. Fázis
  három modulját.
  - APVTS paraméterek: `priority` (Choice 1-10), `amount` (Float 0-100%), `attack` és
    `release` (Float 5-200ms mindkettő). A spec 5. fejezete külön "Smoothing"-ot is
    említ az Attack/Release mellett, de a 3. fejezet ugyanazt az "Attack/Release
    Smooth" fogalmat írja le egyben — mivel a spec sehol nem ad önálló jelentést egy
    negyedik "Smoothing" paraméternek, ez a kód az Attack+Release-t magát tekinti a
    kért simításnak, nem talál ki egy dokumentálatlan ötödik paramétert.
  - Csatorna-kezelés (a spec ezt egyáltalán nem tárgyalja, saját tervezési döntés):
    mono/stereo I/O, EGY közös `SpectralEngine` a mono-lekevert bemeneten (a
    maszkolási döntés eleve frekvencia-alapú, nem sztereókép-alapú), és
    csatornánként egy-egy önálló `MaskingProcessor` a tényleges audio
    rekonstruálásához — így a sztereókép megmarad.
  - `SpectralEngine::prepare()`/`setSmoothingTimes()`: az Attack/Release mostantól
    futásidőben állítható (korábban 10ms/50ms fixen be volt égetve) — külön metódus
    a puffer-törlő `prepare()`-től, hogy a paraméterek automatizálása ne
    okozzon audio-glitch-et minden változtatásnál.
  - `setLatencySamples (MaskingProcessor::kLatencySamples)` — NEM a spec betű szerinti
    2048, ami a saját, Fázis 3-ban megcáfolt első becslésünk volt.
- `Tests/PluginProcessorTests.cpp` (Catch2):
  - APVTS paraméter-lista ellenőrzés.
  - Mono/stereo `processBlock` füst-teszt: véges kimenet, helyes látencia.
  - **Real-Time tesztelés** (a spec kérése: "Real-Time teszterrel ellenőrzés"): mivel
    a JUCE nem ad ilyen eszközt, globális `operator new/delete` felülírással mérjük,
    hogy a `processBlock()` alatt tényleg nulla heap-allokáció történik-e — ez
    szigorúbb és automatizálhatóbb, mint egy kézi DAW-teszt.
  - Két-példányos integrációs teszt: két `SmartMaskAudioProcessor` (ének prioritás 1,
    pad prioritás 10) ugyanazt a hangot kapja — a pad kimenete mérhetően halkul az
    ének versengő energiája miatt. Ez az első teszt, ami az 1-4. Fázist end-to-end
    együtt gyakorolja be.
  - Mért eredmény: mind a 4 teszt-suite zöld (62+15+3708+80 = 3865 assertion).
- **Tényleges plugin build** (`juce_add_plugin`, FORMATS VST3 AU Standalone): a mag
  (`SmartMaskCore`) tartalmaz mindent (a `createPluginFilter()` gyárfüggvényt is), a
  plugin-target csak a JUCE formátum-wrappert adja hozzá.
  - **Mért hiba és javítás — VST2/VST3 automatizáció-ütközés**: a VST3-cél
    `#error You may have a conflict with parameter automation between VST2 and VST3`
    hibával állt le fordításkor. Ez egy vadonatúj plugin, sosem volt sem VST2, sem
    VST3 kiadása — a JUCE saját kódkommentje pontosan erre az esetre
    `JUCE_VST3_CAN_REPLACE_VST2=0`-t javasol, ezt állítottuk be.
  - **Mért hiba és javítás — apostrof a mappanévben töri a VST3 post-build lépést**:
    a `SmartMaskNetworkPlugin_VST3` cél a manifest-generáló lépésnél
    `/bin/sh: unexpected EOF while looking for matching \`'\`` hibával halt el.
    Lemértem: a CMake a POST_BUILD parancsláncok elé generált nyers shell-parancsban
    a szóközöket backslash-sel escape-eli (`Gabci's\ Smart\ Mask\ Network`), de az
    apostrofot NEM — a "Gabci's" mappanévben lévő magányos `'` emiatt egy sosem záruló
    idézőjelet nyit a shell-ben. Ugyanez a hiba jelentkezett Make ÉS Ninja generátorral
    is (tehát nem generátor-specifikus, hanem CMake path-escaping hiba). A végleges
    megoldás: a build könyvtárat a projekt mappáján KÍVÜLRE kell tenni (a forráskód
    maga maradhat itt — a fordítási lépések már eddig is jól kezelték az apostrofot,
    csak a POST_BUILD parancsláncok törtek el). A `CMakeLists.txt` tetejére egy
    gyors-hibázó ellenőrzést tettem (`CMAKE_BINARY_DIR MATCHES "'"` → `FATAL_ERROR`),
    hogy ezt legközelebb ne kelljen újra ~2 órán át diagnosztizálni.
  - Emiatt mellékesen telepítve lett a `ninja` build tool (Homebrew-n keresztül) —
    bár végül nem a generátor volt a hiba oka, a Ninja-váltás jó darabig a fő
    gyanú volt, és egyébként is gyorsabb/megbízhatóbb, mint a GNU Make ennél a
    projektnél (kevesebb redundáns újrafordítás JUCE-modulonként).
  - Épített formátumok: VST3, AU, Standalone (a Standalone azért, hogy a felhasználó
    saját maga is ki tudja próbálni hangkártyán, DAW nélkül).

## 0.3.0 — 2026-08-03

Fázis 3: Prioritási Mátrix és Dinamikus Szűrő (`MaskingProcessor`).

- `Source/MaskingProcessor.h/.cpp`: a Fázis 3 négy lépése egy osztályban (a spec
  5. fejezete így írja elő, még ha a 3. fejezet architektúra-leírása két külön
  koncepcionális modulra — PsychoacousticCalculator + SpectralFilterBank — bontja is
  ugyanezt).
  - `computeBandGains()`: sávankénti csillapítás a saját energia / versengő
    (magasabb prioritású) energia arányából, lágy-térdes görbével, `amount`-tal
    skálázva `[1-amount, 1]` tartományba. **Fontos**: a spec csak a kvalitatív
    szabályt adja meg (Fejezet 3), konkrét formulát nem — ellentétben a Bark-skálával
    és az attack/release időállandókkal, amik pontosan rögzítettek. Ez a görbe saját
    tervezési döntés, dokumentálva a kódban.
  - `interpolateBandGainsToBins()`: a 24 sáv-gain simán (lineárisan, nem lépcsősen)
    interpolálva 1024 lineáris bin szorzóvá, a bin folytonos Bark-pozíciója alapján.
  - Saját STFT front-end (különálló a SpectralEngine-től, mert a rekonstrukcióhoz kell
    a fázis, amit a Fázis 2 elemzés eldob): ablakozás, FFT, gain-szorzás komplex
    binenként (fázis automatikusan megmarad — "Zero-phase reconstruction"), IFFT,
    Overlap-Add.
  - **Mért hiba és javítás — Gain Normalization Factor**: a spec 4. fejezete egy
    fix `0.375` konstanst ad meg Hann-ablak + 75%-os átfedésre. Lemértem: a JUCE
    tényleges `WindowingFunction`-je (alapértelmezett DC-normalizálással) NEM ezt a
    "tankönyvi" ablakot adja — a mért overlap-add összeg ~11%-os hullámzással bír, nem
    egy sima 0.375-tel korrigálható konstans. Emiatt a kód a valós ablakból méri ki
    pontosan a pozíciónkénti korrekciós envelope-ot (`buildOlaNormalisation()`) fix
    konstans helyett — ez pontosan eléri a spec által megcélzott 0 dB-es átlátszó
    átvitelt, függetlenül az ablak-implementáció részleteitől.
  - **Mért hiba és javítás — látencia**: a kód elsőre `kFftSize` (2048) mintás
    késleltetést feltételezett. Egy round-trip unit teszt (0 dB passthrough, versengő
    energia nélkül) ennél megmagyarázhatatlanul rosszul hasalt el — fázisban eltolt,
    rossz előjelű, rossz amplitúdójú minták. Lag-kereséssel (`FftProbe`, ideiglenes
    debug-executable) kiderült: a valódi, pontos (rms hiba ~8e-8) illeszkedés
    `2*kFftSize - kHopSize = 3584` mintás késleltetésnél van, nem 2048-nál — mert
    csak-elemzési-ablakos (szintézis-ablak nélküli), 4×-es átfedésű OLA-rekonstrukciónál
    a késleltetést KÉT egymásra épülő "beállási" szakasz adja: az elemző csúszóablak
    feltöltődése (kFftSize) ÉS az utána szükséges overlap-add összegződés (további
    kFftSize-kHopSize). Ez most `MaskingProcessor::kLatencySamples` néven explicit
    konstans — ezt kell majd a Fázis 4-ben a `setLatencySamples()`-nek átadni, NEM
    `kFftSize`-ot.
- `Tests/MaskingProcessorTests.cpp` (Catch2):
  - `computeBandGains` egzakt numerikus tesztek (nulla versengés, egyenlő
    energia+teljes amount → pontosan 0.5-ös gain, amount-clamping, domináns
    versengő → gain a padló közelében).
  - Teljes csővezeték round-trip teszt: `SpectralEngine` + `MaskingProcessor` együtt,
    ugyanarra a bemenetre hop-szinkronban (pont úgy, ahogy a Fázis 4 összekötné őket) —
    versengő energia nélkül a kimenet pontosan (1e-3 tűréssel) a bemenet
    `kLatencySamples`-sel késleltetett másolata.
  - Egyenletes, teljes-amount-os versengés (competing == own minden sávban) kb.
    felére csökkenti a kimeneti RMS-t.
  - Mért eredmény: mind a 3 teszteset (3708 assertion) zöld natívan lefordítva és
    lefuttatva.

## 0.2.0 — 2026-08-03

Fázis 2: STFT & Bark-Scale Pszichoakusztikus DSP Engine + JUCE-verzió korrekció.

- **JUCE-verzió**: a specifikáció (JUCE 8) írásakor még nem jelent meg a JUCE 9 — a
  felhasználó kérésére a projekt a legfrissebb elérhető verziót követi, ezért a
  FetchContent-pin `8.0.15`-ről `9.0.0`-ra változott (`CMakeLists.txt`). Innentől a
  build ténylegesen behúzza és fordítja is a JUCE-t (`juce_dsp`, `juce_core`,
  `juce_audio_basics` modulok), mert a Fázis 2 DSP-hez kell a `juce::dsp::FFT` —
  ezért a "JUCE csak Fázis 4-ben" korábbi terv is módosult.
- `Source/SpectralEngine.h/.cpp`: 2048-pontos STFT + 24 Bark-sávos energiaszámító.
  - Csúszóablakos RingBuffer, 512 mintás hop (75%-os átfedés), Hann-ablakozás +
    `juce::dsp::FFT::performRealOnlyForwardTransform`.
  - 1024 lineáris bin → 24 Bark-sáv leképezés Zwicker-formulával
    (`z = 13*atan(0.00076*f) + 3.5*atan((f/7500)^2)`), sample rate-enként előre
    kiszámítva `prepare()`-ben.
  - Attack (10ms) / Release (50ms) egypólusú simítás sávanként,
    `juce::ScopedNoDenormals`-szal védve (a lecsengő simító épp az a fajta rekurzív
    szűrő, ami denormal CPU-tüskét okozhat — l. 4. fejezet).
  - Zéró heap-allokáció `pushSamples()`-ben: minden puffer `prepare()`-ben előre
    lefoglalva.
- `Tests/SpectralEngineTests.cpp` (Catch2):
  - Tiszta szinusz teszt 3 sample rate-en (44100/48000/96000 Hz): a domináns Bark-sáv
    pontosan egyezik egy független Zwicker-formula-referenciával, és egy távoli sáv
    energiája elhanyagolható mellette.
  - Attack/release viselkedés teszt — **fontos mért felismerés**: az első próba téves
    feltevésen bukott el (26302 vs elvárt >471881), mert a 2048/512-es ablak-hop
    arány miatt az ablak 4 hopon át tölt fel új jellel, tehát a nyers célérték maga
    is rámpázik az első 4 hopban, a simítási együtthatótól függetlenül. A javított
    teszt külön ellenőrzi az ablakfeltöltési rámpát és az azt követő simítási
    konvergenciát — mindkettő helyesen viselkedik.
  - Mért eredmény: mind a 2 teszteset (15 assertion) zöld.

## 0.1.0 — 2026-08-03

Fázis 1: Lock-Free SmartMaskRegistry.

- `Source/SmartMaskRegistry.h/.cpp`: fix 32 slotos, lock-free globális állapotregiszter.
  - `registerTrack()` / `unregisterTrack()`: CAS-alapú slot-foglalás, nincs mutex; csak a
    message threadről hívható (nem a processBlock-ból).
  - `updateSpectrum()`: dupla pufferelt `std::atomic<BarkSpectrum*>` pointer-csere — zéró
    heap-allokáció, wait-free, hívható a processBlock-ból.
  - `getGlobalMask()`: az összes aktív, nálunk magasabb prioritású (kisebb `priority`
    értékű) sáv Bark-sávonkénti maximum energiáját adja vissza. Ez a nyers, versengő
    energia-vektor — a tényleges csillapítási görbét (amount/attack/release alapján) a
    Fázis 3 MaskingProcessor/PsychoacousticCalculator fogja ebből számolni.
- `Tests/SmartMaskRegistryTests.cpp` (Catch2, FetchContent v3.7.1):
  - 16 szálas konkurrens `registerTrack` egyediség-teszt.
  - Prioritás-logika egységteszt (az alacsonyabb prioritású sáv látja a magasabbat — pl.
    a pad látja az énekhang energiáját —, fordítva nem: a legmagasabb prioritású sáv
    semmit sem lát). [Javítva 2026-08-03: ez a bejegyzés korábban fordítva volt
    megfogalmazva; a kód és a teszt maga mindvégig helyesen ezt az irányt
    implementálta, csak ez a leírás mondta az ellenkezőjét.]
  - 16 szál × 2000 iterációs write/read stressz-teszt, torn-read / NaN / [0,1]
    tartományon-kívüli érték detektálással.
  - Mért eredmény: mind a 3 teszteset (62 assertion) zöld natívan **és**
    ThreadSanitizer alatt is (`-fsanitize=thread`, `SMARTMASK_ENABLE_TSAN=ON`) — nincs
    észlelt adatverseny.
- CMake-vázlat (`CMakeLists.txt`, `Source/CMakeLists.txt`, `Tests/CMakeLists.txt`): a mag
  (`SmartMaskCore`) egyelőre JUCE-független (tiszta C++20 `<atomic>`); a JUCE 8
  FetchContent-bekötés (`8.0.15`) a Fázis 4-ben (PluginProcessor/APVTS) kerül be.
