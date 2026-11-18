# Firmware – sterownik dawkowania oleju (Arduino Nano + OLED + enkoder)

Arduino przejmuje sterowanie pompką: YETI mówi tylko **„grzej / nie grzej”** (wyjście „Dmuchawa”),
a Arduino co ustawiony **interwał** podaje **jedną dawkę oleju**. Interwał ustawiasz enkoderem
bezpośrednio na ekranie głównym.

```
┌────────────────────────────┐
│PRACA                  245C │   stan + temperatura płomienia/spalin
├────────────────────────────┤
│DAWKA                       │
│CO              6.0 s       │   ← kręcisz enkoderem = zmieniasz interwał
│>>>                         │   (>>> = pompka teraz podaje)
│10.0/min 0.60l/h            │   dawek na minutę, przepływ (po kalibracji)
│[██████████░░░░░░░░░░░░░░░] │   czas do następnej dawki
└────────────────────────────┘
```

- **Interwał** = czas od początku jednej dawki do początku następnej (0,5 – 120 s).
  Krok enkodera: 0,1 s do 10 s, 0,5 s do 30 s, potem 1 s. Zapis do EEPROM 3 s po ostatnim ruchu.
- Mniejszy interwał → więcej oleju → większa moc palnika.

## Tryby dawki

| Tryb | Jak liczona jest dawka | Kiedy |
|---|---|---|
| **czas** | silnik pracuje przez „Czas dawki” (0,1 – 30 s) | pompa zębata / perystaltyczna / brak krańcówki |
| **obroty** | silnik robi N pełnych obrotów, liczonych **krańcówką (stykiem parkowania) wycieraczki** | pompa tłoczkowa / mimośrodowa – 1 obrót = 1 skok = stała dawka |

Tryb **obroty** jest dokładniejszy: dawka nie zależy od napięcia, temperatury i gęstości oleju.
Dodatkowo, jeśli silnik pracuje, a krańcówka przez 8 s nie zgłosi obrotu
(zablokowana pompa, zerwany przewód), sterownik przechodzi w **blokadę „Pompa/krancowka”**.

## Menu (klik na ekranie głównym)

| Pozycja | Zakres | Domyślnie | Opis |
|---|---|---|---|
| Interwal | 0,5 – 120 s | 6,0 s | to samo co enkoder na ekranie głównym |
| Tryb dawki | czas / obroty | czas | patrz wyżej |
| Czas dawki | 0,1 – 30 s | 0,5 s | tryb „czas” |
| Obroty/daw. | 1 – 20 | 1 | tryb „obroty” |
| ml/dawke | 0 – 20 ml | brak | kalibracja → ekran pokazuje l/h i zużycie |
| Moc pompy | 20 – 100 % | 100 % | PWM silnika (wolniejsze obroty) |
| Kopniecie | 0 – 2000 ms | 200 ms | pełna moc na starcie każdej dawki |
| Czujnik pl. | brak / termostat / termopara | brak | kontrola płomienia |
| Prog plom. | 40 – 600 °C | 120 °C | termopara: powyżej = płomień jest |
| Czas zapl. | 1 – 30 min | 5 min | tyle może trwać rozpalanie bez płomienia → blokada |
| Zanik plom. | 5 – 300 s | 60 s | tyle płomienia może nie być w trakcie pracy → blokada |
| Informacje | | | YETI, 12 V, czas pracy, liczba dawek, litry, stan krańcówki |
| Odpowietrzanie | | | pompka pracuje, dopóki trzymasz przycisk (max 60 s) |

Edycja: klik = zapisz, przytrzymanie = anuluj. Blokadę kasuje się **przytrzymaniem przycisku**
na ekranie głównym (pierwszy klik wycisza buzzer). Blokada przetrwa wyłączenie zasilania.

### Kalibracja (ml/dawkę)
1. Menu → Odpowietrzanie – napełnij przewody olejem.
2. Ustaw interwał np. 5 s, wylot pompki do menzurki, odczekaj 20 dawek (Informacje → licznik dawek).
3. ml/dawkę = zebrane ml / 20 → wpisz w menu. Od teraz ekran pokazuje **l/h**.

## Schemat połączeń

```
                      +12 V z zasilacza (na stałe)
                            │
                     [bezpiecznik 10 A]
                            │
   KANAŁ 1 (YETI):  styk NO przekaźnika 230 V (cewka ← YETI „Dmuchawa” przez STB 95°C)
                            │
            +12V_SW ────────┼──────────────────────────── A0: dzielnik 10k/3,3k (pomiar 12 V)
                            │
   KANAŁ 2 (Arduino): styk NO przekaźnika samochodowego 12 V 30 A
                            │      cewka: +12 V ─ cewka ─ C BC547 E ─ GND, dioda 1N4148 na cewce,
                            │             baza ← A2 przez 1k, 10k baza–emiter
            +12V_SAFE ──────┼──────────────────┬──────────────┐
                            │                  │              │
                         S  │                [1k]          [10k]
                     IRF4905 (P-MOSFET)        │              │
                         G ─┼──────────────────┤          (nic – źródło
                         D  │                  │C           klucza)
                            │               BC547
                            │                  │E
                            │      D9 ─[1k]─ B │
                            │                 GND   (10k B–E)
                            │
          SR560 (K) ────────┤ MOTOR+ ──┬──► silnik wycieraczki, zacisk 53 (bieg wolny)
          SR560 (A) ── GND             └──► A6: dzielnik 10k/3,3k (napięcie na silniku)
                              MOTOR−  → zacisk 31 / obudowa → GND
                              park 31b → [1k] → D8   (53a NIE podłączać)
```

**Trzy niezależne odcięcia w szeregu:** przekaźnik YETI (sprzęt, z STB) → przekaźnik bezpieczeństwa
(Arduino) → tranzystor (Arduino, dawki). Pompa dostaje prąd tylko, gdy wszystkie trzy są zamknięte.

- **Zawieszone Arduino:** watchdog → reset → piny A2 i D9 jako wejścia → oba BC547 zatkane (10 kΩ B–E)
  → przekaźnik bezpieczeństwa i tranzystor otwarte.
- **Zwarty tranzystor** (najczęstsza awaria MOSFET-a): Arduino widzi na A6 napięcie, choć pompa ma stać
  → po 0,3 s otwiera przekaźnik bezpieczeństwa i blokuje się („Klucz zwarty”).
- **Sklejony styk przekaźnika bezpieczeństwa:** przy każdym uruchomieniu pompy test – przekaźnik otwarty,
  tranzystor na chwilę załączony, na silniku musi być 0 V. Napięcie → blokada („Przek. bezp.”).
- **Sklejony przekaźnik YETI:** 12 V przy wyłączonym YETI → blokada („Przekaznik/12V”).

**Dlaczego klucz od strony plusa (high-side)?** W silniku wycieraczki obudowa = masa (31),
a krańcówka parkowania zwiera się właśnie do obudowy. Przy kluczu od strony masy obudowa „wisiałaby”
i krańcówki nie dałoby się odczytać. Przy okazji obudowa przykręcona do kotła jest pewnie uziemiona.

### Tabela pinów Arduino Nano

| Pin | Podłączenie |
|---|---|
| D2, D3 | enkoder A, B (KY-040: CLK, DT) |
| D4 | przycisk enkodera (SW) |
| D5 | wyjście modułu transoptora 230 V (YETI „Dmuchawa”) |
| D6 | termostat spalin NO → GND (opcjonalnie) |
| D7 | buzzer aktywny 5 V |
| D8 | krańcówka wycieraczki 31b (przez 1 kΩ, + 100 nF do GND) |
| D9 | → 1 kΩ → baza BC547 (klucz pompki) |
| A2 | → 1 kΩ → baza BC547 przekaźnika bezpieczeństwa |
| A6 | dzielnik 10 kΩ / 3,3 kΩ z wyjścia klucza (napięcie na silniku) |
| D10, D12, D13 | MAX6675: CS, SO, SCK (opcjonalnie) |
| A0 | dzielnik 10 kΩ / 3,3 kΩ z +12V_SW |
| A4, A5 | OLED SDA, SCL |
| VIN / 5V | zasilanie z przetwornicy 12→5 V (np. mini-360 / LM2596), GND wspólna |

### Krańcówka wycieraczki – sprawdź miernikiem
Typowe oznaczenia: **31** masa, **53** bieg wolny, **53b** szybki, **53a** +12 V do parkowania,
**31b** wyjście parkowania. Kręcąc wałkiem ręcznie sprawdź, że **31b–31 zwiera się tylko w pozycji
parkowania** (wtedy Informacje → „Krancowka: PARK”). Jeśli u Ciebie jest odwrotnie, zmień w kodzie
`PARK_ACTIVE_LEVEL` na `HIGH`. Zacisku **53a nie podłączaj** – inaczej na 31b pojawi się 12 V.

### Silnik z kostką 5-pinową – jak rozpoznać piny
Kolejność pinów zależy od producenta, więc trzeba ją ustalić miernikiem. Odłącz wszystko i sprawdź:

1. **Masa 31:** pin, który ma ~0 Ω do metalowej obudowy przekładni (w części tanich silników
   masa nie jest na obudowie – wtedy to wspólny pin szczotek z kroku 2).
2. **Szczotki 53 i 53b:** między masą a tymi pinami jest kilka omów (~0,5–3 Ω).
   Zasil na chwilę 12 V **przez żarówkę 21 W albo zasilacz z ograniczeniem prądu**:
   wolniejszy = **53 (bieg wolny)**, szybszy = **53b**.
3. **Krańcówka 53a / 31b:** dwa pozostałe piny. Kręcąc silnikiem (zasilanym na 53), mierz ciągłość
   między nimi i masą: **31b** raz na obrót przełącza się między 53a a masą. 53a zostaw niepodłączony,
   31b → D8 przez 1 kΩ.

Zapisz wynik (np. „pin 1 = 31, pin 3 = 53, pin 5 = 31b”) – przydaje się przy naprawach.

## Lista części

| Część | Uwagi |
|---|---|
| Arduino Nano (ATmega328P, nowy bootloader) | klon z CH340 jest OK |
| OLED 0.96" SSD1306 128×64 I2C | dla 1.3" SH1106 ustaw `OLED_SH1106 1` |
| Enkoder KY-040 | jeśli przeskakuje o 2 – `ENC_STEPS_PER_DETENT 2` |
| Moduł transoptora 230 V AC (1 kanał) | wejście równolegle do cewki przekaźnika |
| Przekaźnik z cewką 230 V AC, styk ≥ 10 A | twarde odcięcie 12 V niezależne od Arduino |
| IRF4905 (P-MOSFET) na radiatorze, BC547 ×2, rezystory 1k ×4, 10k ×4, 3.3k ×2, dioda 1N4148 | klucz pompki, sterowanie przekaźnika bezpieczeństwa, pomiary |
| Przekaźnik samochodowy 12 V 30 A + gniazdo | przekaźnik bezpieczeństwa (drugi kanał odcięcia) |
| Dioda Schottky SR560 / SB560 | równolegle do silnika (gaszenie przepięć) |
| Przetwornica 12 V → 5 V | zasilanie Arduino |
| (opcja) MAX6675 + termopara K | kontrola płomienia – termopara blisko płomienia, nie w kominie |
| (opcja) buzzer aktywny 5 V | alarm blokady |

## Kompilacja i wgranie

Arduino IDE: zainstaluj bibliotekę **U8g2** (Menedżer bibliotek), otwórz
`sterownik_oleju/sterownik_oleju.ino`, płytka **Arduino Nano**, procesor **ATmega328P**, Wgraj.

Zajętość: ok. 82 % Flash, 51 % RAM.

## Odporność na zawieszenia

**W programie:**

| Zabezpieczenie | Co robi |
|---|---|
| Pętla programu nigdy nie stoi dłużej niż ~15 ms | ekran rysowany po jednym pasku na obieg pętli, przycisk obsługiwany w przerwaniu (krótkie kliknięcia nie giną) |
| Watchdog **250 ms** od pierwszej instrukcji | zawieszony program = reset po ¼ s; reset ustawia piny jako wejścia, rezystor 10 kΩ zamyka klucz pompy. Zawieszenie w trakcie dawki dokłada najwyżej ~0,25 s pracy pompy (sprawdzone w emulatorze: 255 ms, ~2 ml) |
| Licznik restartów w pamięci `.noinit` | 3 restarty watchdoga bez 10 min stabilnej pracy = **blokada „Restarty MCU”** (sterownik, który ciągle się wiesza, przestaje podawać olej). Liczba restartów w menu Informacje (`WDT:`) |
| Pierwsza dawka po starcie dopiero po pełnym interwale | restart nie daje dodatkowej dawki oleju |
| Timeout I2C (5 ms) + ponowna inicjalizacja OLED | zakłócona magistrala nie zawiesi programu; ekran odświeżany co 10 min i po każdym błędzie I2C (tylko gdy pompa stoi) |
| Kontrola sumy ustawień w RAM co 5 s | uszkodzone przez zakłócenia ustawienia są wczytywane ponownie z EEPROM |
| Kontrola obrotu pompy (krańcówka, w każdym trybie dawki) | silnik ma prąd, a przez ~3 s pracy (sumowanej przez dawki) nie ma obrotu = **blokada „Pompa zablok.”** – zablokowany silnik bierze 15–20 A i się grzeje. Menu `Kontr.obrot` = nie wyłącza (tylko gdy krańcówka nie jest podłączona) |
| Test przekaźnika bezpieczeństwa także co 5 min pracy | sklejenie styku w trakcie pracy wykryte najpóźniej po 5 min |
| Domyślnie kontrola płomienia termoparą | brak lub odłączona termopara = **blokada „Blad czujnika”** po ok. 1 s; wyłączenie kontroli wymaga świadomego ustawienia `Czujnik pl. = brak` |
| Strażnik czasu pracy pompy (32 s) | pojedyncze załączenie dłuższe niż najdłuższa dawka = **blokada „Pompa za dlugo”** |
| Blokady zapisane w EEPROM | zanik zasilania nie kasuje blokady |

**Wymaganie:** bootloader **Optiboot** („nowy bootloader”). Sprawdzenie: jeśli program wgrywa się z ustawieniem
procesora *ATmega328P* (a nie *ATmega328P (Old Bootloader)*), jest dobrze. Ze starym bootloaderem restart
przez watchdoga potrafi zapętlić płytkę – wtedy wgraj Optiboot (Narzędzia → Wypal bootloader, przez drugie Arduino).

**W sprzęcie (zakłócenia to główna przyczyna „kapryśnego” Arduino):**
- Rezystor **10 kΩ baza–emiter BC547** jest obowiązkowy – trzyma pompę wyłączoną podczas resetu.
- Arduino zasilaj z **osobnej przetwornicy 12→5 V**, przy płytce **470 µF + 100 nF**.
- Na zaciskach silnika: **kondensator 100 nF ceramiczny** + dioda Schottky; na przewodzie silnika koralik ferrytowy.
- Masa „gwiazdą”: prąd silnika wraca do zasilacza własnym przewodem, nie przez GND Arduino.
- Przewody OLED (I2C) krótkie, do ~20 cm, z dala od silnika i 230 V; enkoder i krańcówka – skrętka.
- Część 230 V (przekaźnik, transoptor, zasilacz) oddzielona od elektroniki 5 V, najlepiej przegrodą w obudowie.

## Logika bezpieczeństwa

- 12 V do pompki idzie **przez przekaźnik sterowany z YETI i STB** – zawieszone Arduino nie poleje oleju,
  gdy YETI nie grzeje.
- Watchdog 250 ms + licznik restartów (szczegóły wyżej).
- **Blokady** (pompa stop, buzzer, zapis w EEPROM): brak zapłonu w zadanym czasie, zanik płomienia,
  uszkodzona termopara, 12 V przy wyłączonym YETI (sklejony przekaźnik), brak obrotu pompy.
- Bez czujnika płomienia ekran pokazuje „bez kontr” – **ustaw czujnik**, zanim zostawisz piec bez nadzoru.
