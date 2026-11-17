# Sterownik pompki oleju na bazie sterownika YETI

Wykorzystanie typowego sterownika kotła na paliwo stałe (YETI: dmuchawa + pompa CO + czujnik)
do sterowania **pompką oleju** zrobioną z **silnika wycieraczek 12 V** (np. z ciągnika).

```
Sterownik YETI ── wyjście „Dmuchawa” 230 V ──► [moduł wykonawczy] ──► 12 V ──► silnik wycieraczki = pompka oleju
```

> **Wersja z mikrokontrolerem:** dawkowanie oleju (interwał ustawiany enkoderem, wyświetlacz OLED,
> kontrola płomienia, blokady) robi Arduino Nano – opis, schemat i kod w [`firmware/`](firmware/README.md).
> Ten plik opisuje część 230 V (YETI, przekaźnik, ustawienia YETI, bezpieczeństwo) – obowiązuje dalej.

---

## 1. Najważniejsze: wyjście „Dmuchawa” to NIE jest zwykły włącznik 230 V

W tych sterownikach wyjście dmuchawy jest sterowane **triakiem z regulacją fazową**
(parametry *MAX/MIN OBROTY DMUCHAWY [bieg]*). Na niższych biegach na wyjściu jest „pocięta” sinusoida,
a nie pełne 230 V. Skutki przy bezpośrednim podłączeniu zasilacza 12 V:

| Problem | Skutek |
|---|---|
| Zasilacz impulsowy (SMPS) na pociętej sinusoidzie | nie startuje, „pyka”, grzeje się, może paść |
| Prąd rozruchowy zasilacza (kondensatory) | może uszkodzić triak lub przepalić bezpiecznik 1,25 A w YETI |
| Zasilacz startuje 0,5–1 s po podaniu napięcia | przy krótkich impulsach pompka nie zadziała |
| Regulacja obrotów | i tak nie działa – SMPS daje 12 V albo nic |

Dlatego są dwa warianty:

### Wariant A (ZALECANY) – przekaźnik z cewką 230 V AC

Zasilacz 12 V jest zasilany **na stałe** z sieci, a wyjście „Dmuchawa” steruje tylko **cewką przekaźnika**.
Styk przekaźnika załącza 12 V na silnik.

```
            SIEĆ 230 V (na stałe)
              │
              ▼
      ┌───────────────┐   +12V   ┌──────┐   ┌───────────┐   ┌──────────────┐
      │ Zasilacz 12 V │────────► │ BEZP.│──►│ styk NO   │──►│ (opcjonalnie)│──► + silnik (bieg wolny)
      │  10–15 A      │          │ 10 A │   │ przekaźn. │   │  regulator   │
      └───────────────┘          └──────┘   └───────────┘   │  PWM 12 V    │──► − silnik (masa/obudowa)
              │ −12V ─────────────────────────────────────────►└──────────────┘
                                                  ▲
 YETI „Dmuchawa” L ──[STB 95°C, ręczny reset]──► cewka 230 V AC ──► N
```

Zalety: pełne 12 V zawsze od razu, triak w YETI obciążony tylko cewką (kilka mA – nic mu nie grozi),
zasilacz nie jest ciągle włączany/wyłączany.

### Wariant B – zasilacz bezpośrednio na wyjściu „Dmuchawa” (Twój pierwotny pomysł)

Da się, ale tylko gdy:
- w YETI ustawisz **MIN = MAX obroty = najwyższy bieg** (pełna sinusoida),
- zasilacz jest małej mocy albo jest to **klasyczny zasilacz transformatorowy** (trafo + mostek + kondensator),
  a nie impulsowy z dużym prądem rozruchowym,
- pobór zasilacza z sieci nie przekracza tego, co wytrzymuje wyjście dmuchawy (bezpiecznik 1,25 A
  na całość, więc realnie < ~150 W).

Ryzyko uszkodzenia triaka jest realne – wariant A kosztuje kilkanaście zł więcej (przekaźnik) i jest pewny.

---

## 2. Lista części (wariant A)

| Element | Uwagi |
|---|---|
| Zasilacz 12 V DC, **10–15 A** (np. typu „LED/MeanWell LRS-150/200-12”) | silnik wycieraczki: 2–5 A pracy, **15–20 A przy rozruchu/zablokowaniu** |
| Przekaźnik z **cewką 230 V AC**, styk **≥10 A** (np. Finder 40.61 / RM85 230 V AC, lub przekaźnik modułowy na szynę DIN) | styk pracuje na 12 V DC z obciążeniem indukcyjnym – brać zapas |
| Bezpiecznik 10 A na linii +12 V (samochodowy płaski + oprawka) | chroni przed spaleniem przy zablokowaniu pompki |
| Dioda 1N5408 (lub TVS) równolegle do silnika, katodą do „+” | gasi przepięcie przy rozłączaniu, oszczędza styk przekaźnika |
| (opcjonalnie) Regulator PWM silnika DC 12 V, ≥10 A | ustawienie wydajności pompki (dawki oleju) potencjometrem |
| (zalecane) Ogranicznik temperatury **STB 90–95 °C z ręcznym resetem** | w szereg z cewką przekaźnika – twarde odcięcie oleju przy przegrzaniu |
| (zalecane) Elektrozawór 12 V NC na linii oleju | równolegle do silnika – patrz pkt 5 |
| Filtr oleju przed pompką | silnik wycieraczki z pompą nie lubi zanieczyszczeń |

---

## 3. Podłączenie silnika wycieraczki

Typowy silnik wycieraczek ma wyprowadzenia:
- **masa** (często obudowa),
- **+ bieg wolny** (szczotka „1”),
- **+ bieg szybki** (szczotka „2”),
- **wyłącznik krańcowy / park** (+ stałe i wyjście parkowania).

Do pompki:
- **+12 V → bieg wolny** (szybki zwykle za szybki dla dawkowania oleju),
- **− → masa/obudowa**,
- **wyłącznika krańcowego (park) NIE podłączać** – inaczej silnik będzie się „dojeżdżał” po wyłączeniu.

Wydajność dobierasz: biegiem (wolny/szybki), przełożeniem/budową samej pompki, albo regulatorem PWM.

> Uwaga: regulacja biegów dmuchawy w YETI **nie zmieni** obrotów pompki w wariancie A (przekaźnik jest tylko
> wł./wył.). Modulacja dawki w zależności od temperatury wymagałaby osobnego mikrokontrolera –
> to można dorobić później.

---

## 4. Ustawienia sterownika YETI (KONFIGURACJA – przytrzymaj START/STOP 6 s)

| Parametr | Ustawienie | Dlaczego |
|---|---|---|
| **CZAS PRZEDMUCHU [s]** | **0** | **Bardzo ważne.** W podtrzymaniu (po osiągnięciu temp. zadanej) YETI co „czas przerwy” włącza dmuchawę na „czas przedmuchu”. Przy pompce oznacza to **wtrysk oleju do palnika, gdy kocioł nie potrzebuje grzania** – olej może się zbierać i potem gwałtownie zapalić. |
| CZAS PRZERWY [min] | dowolny (bez znaczenia przy przedmuchu = 0) | |
| **MAX OBROTY DMUCHAWY** | najwyższy bieg | pełna sinusoida dla cewki / zasilacza |
| **MIN OBROTY DMUCHAWY** | **taki sam jak MAX** | żeby przy dochodzeniu do temp. zadanej przekaźnik nie „brzęczał” na pociętej fazie |
| TEMP. ZAŁĄCZENIA POMPY [°C] | jak dotychczas (np. 40 °C) | pompa obiegu CO bez zmian |
| **TEMP. WYŁĄCZENIA REG. [°C]** | ok. 30–35 °C | gdy płomień zgaśnie i kocioł wystygnie poniżej tej wartości, sterownik uzna „brak paliwa” i przejdzie w STOP – to **jedyne wbudowane zabezpieczenie przed lejącym się olejem bez płomienia** (działa z opóźnieniem!) |

Jeżeli przekaźnik mimo MIN = MAX brzęczy lub „trzyma” po wyłączeniu (triak może nie gasnąć przy bardzo małym
prądzie cewki), dodaj równolegle do cewki **snubber RC: 100 nF X2 275 V AC + 220 Ω 1 W**
albo użyj przekaźnika/stycznika o większej cewce.

---

## 5. Bezpieczeństwo – przeczytaj zanim odpalisz

YETI jest sterownikiem do **kotłów na paliwo stałe**. **Nie ma czujnika płomienia.** Sterownik nie wie,
czy olej się pali – widzi tylko temperaturę wody. Jeśli palnik zgaśnie, pompka **będzie dalej podawać olej**
dopóki woda nie wystygnie poniżej „TEMP. WYŁĄCZENIA REG.”. Minimum, które warto dodać:

1. **STB 90–95 °C z ręcznym resetem** w szereg z cewką przekaźnika (na kotle/zasilaniu CO).
2. **Kontrola płomienia** – najprościej termostat bimetaliczny NO (np. KSD301 ~60–80 °C) na czopuchu/rurze
   spalin: po rozgrzaniu zwiera, jeśli spaliny wystygną – rozwiera i odcina pompkę. Wymaga obejścia
   na czas rozpalania (przycisk lub przekaźnik czasowy, np. 5–10 min).
3. **Grawitacyjny wyciek oleju** – jeśli zbiornik jest wyżej niż palnik, olej może przeciekać przez stojącą
   pompkę. Załóż **elektrozawór 12 V NC** podłączony równolegle do silnika (otwiera się tylko gdy pompka pracuje).
4. **Bezpiecznik na +12 V** i porządny przekrój przewodów (min. 1,5 mm² do silnika).
5. Instalację 230 V wykonaj w zamkniętej puszce, z PE na obudowie zasilacza; przewody z dala od gorących
   elementów kotła i od oleju.
6. Nie zostawiaj układu bez nadzoru, dopóki kilka cykli (rozpalenie → temp. zadana → wygaszenie →
   ponowne załączenie → zgaśnięcie płomienia) nie przejdzie poprawnie.

---

## 6. Logika pracy po przeróbce

| Stan YETI | Wyjście „Dmuchawa” | Pompka oleju |
|---|---|---|
| STOP | wył. | stoi |
| Praca, temp. < zadana | wł. | pompuje |
| Praca, temp. ≥ zadana (podtrzymanie) | wył. (przedmuch = 0) | stoi |
| Temp. spadła poniżej „wyłączenia reg.” | wył. → STOP | stoi |
| STB / termostat spalin rozwarty | (wł.), ale cewka bez zasilania | stoi |

---

## 7. Hydraulika: pompa zatapiana + zawór iglicowy + powrót do zbiornika

```
                       odpowietrzenie / zrywacz syfonu
                       (koniec rurki NAD lustrem oleju)
                                  ▲
                       rurka powrotna wznosi się 20–40 cm,
                       potem opada z powrotem do zbiornika
                                  │
  pompa ──wąż──► [filtr] ──► TRÓJNIK ──► zawór iglicowy ──► elektrozawór 12V NC ──► palnik
  (w zbiorniku)                (nad zbiornikiem)            (przy palniku, opcja)
```

- **Zawór iglicowy na linii DO PALNIKA, a powrót otwarty.** Gdy zaworek zarośnie szlamem, do palnika
  pójdzie *mniej* oleju (płomień zgaśnie → blokada sterownika), a nie więcej. Zaworek na powrocie
  działałby odwrotnie: zatkany = cały wydatek pompy do palnika.
- **Rurka powrotna** robi dwie rzeczy: nadwyżka oleju wraca do zbiornika (pompa nigdy nie tłoczy
  w zamknięte wyjście), a jej wysokość ustala stałe ciśnienie przed zaworkiem – przepływ do palnika
  mniej zależy od obrotów pompy.
- **Koniec powrotu nad lustrem oleju, nie zanurzony** – po zatrzymaniu pompy wpuszcza powietrze do trójnika
  i przerywa syfon, gdy palnik stoi niżej niż zbiornik.
- **Elektrozawór NC** zasilaj z +12 V za przekaźnikiem (otwarty cały czas, gdy dmuchawa jest załączona),
  a nie z wyjścia PWM pompy – na PWM by brzęczał.

Części: wąż olejoodporny (paliwowy NBR) 6–8 mm, opaski, trójnik mosiężny z króćcami na wąż,
zawór iglicowy mosiężny 1/4" BSP + 2 króćce 1/4" → wąż, filtr paliwa (ciągnikowy/motocyklowy) przed trójnikiem.

**Regulacja:** zaworek zgrubnie (raz), enkoder (interwał) na bieżąco. Gęstość przepracowanego oleju
mocno zależy od temperatury – zaworek ustawiaj, gdy olej ma temperaturę pracy.
