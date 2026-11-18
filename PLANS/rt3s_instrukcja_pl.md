# RT3S — instrukcja obsługi funkcji z forka

Dotyczy obrazu `OpenGD77_MDUV380.bin` z gałęzi `feature/sk-shortcuts-waterfall`.
Te funkcje **nie były jeszcze testowane na radiu**.

Oznaczenia klawiszy:

* **SK1**: dolny boczny przycisk.
* **SK2**: górny boczny przycisk.
* **Gałka**: pokrętło kanałów.
* **GREEN / RED**: zielony i czerwony klawisz menu.
* **„SK1 + 0”**: trzymaj SK1 i krótko naciśnij 0.

RT3S nie ma pomarańczowego przycisku. Firmware zastępuje go kombinacją **SK1 + GREEN**.

---

## 1. Szybkie klawisze (Quickkeys)

### Przypisanie akcji do klawisza

1. Na ekranie kanału lub VFO naciśnij **GREEN**, żeby otworzyć menu.
2. Wejdź w **Opcje**, a potem w **Szybkie klawisze** (ostatnia pozycja na liście).
3. Widzisz sloty `0`–`9` i to, co jest do nich przypisane. `Brak` oznacza pusty slot.
4. Wybierz slot strzałkami **▲/▼** albo naciskając cyfrę slotu.
5. Kręć **gałką**, żeby przełączać przypisanie. Do wyboru są:
   * Brak;
   * APRS beacon;
   * APRS on/off;
   * Day/Night;
   * Band scope;
   * Scope: to peak;
   * menu: RSSI, Last heard, Zones, Contacts, Radio info, Satellite, GPS, APRS.

   Zmieniony slot jest oznaczony gwiazdką `*`.
6. Naciśnij **GREEN**, żeby zapisać i wyjść. **RED** wychodzi bez zapisywania.

### Uruchomienie akcji

Na ekranie kanału lub VFO: **SK2 + cyfra slotu**. Na przykład: trzymaj SK2 i krótko naciśnij `5`.

* Akcja nie może się wykonać, np. „Scope: to peak” poza waterfallem → radio da krótki sygnał błędu (NACK).
* Kasowanie slotu: **SK2 + długie naciśnięcie cyfry**. Możesz też ustawić „Brak” w menu.

### Co robi każda akcja

| Akcja | Działanie | Odpowiednik bez Quickkeys |
|---|---|---|
| APRS beacon | Wysyła beacon APRS teraz. | `SK1 + 2` |
| APRS on/off | Włącza i wyłącza beaconing APRS. | `SK1 + 1` |
| Day/Night | Przełącza motyw dzienny i nocny. | `SK1 + RED` |
| Band scope | Uruchamia analizator pasma / waterfall. Z ekranu kanału przełącza na VFO i zatrzymuje skanowanie. | długie `#` w VFO |
| Scope: to peak | Stroi VFO na najsilniejszy sygnał. Działa tylko, gdy waterfall jest aktywny. | `SK1 + 0` w waterfallu |
| (menu) | Otwiera wskazane menu, np. RSSI albo GPS. | — |

Slot z akcją nie zostanie przypadkiem nadpisany gestem „SK2 + cyfra w menu”. Żeby nagrać w nim coś innego, najpierw go skasuj.

---

## 2. Waterfall / analizator pasma (VFO)

1. Przejdź w tryb VFO.
2. **Długie `#`**: start analizatora. Środek pasma to częstotliwość VFO.
3. **`*`**: przełącza widok między waterfallem a słupkami.
4. **Gałka**: przestraja środek pasma.
5. Na ekranie zobaczysz:
   * **kropki nad słupkami**, czyli peak-hold. Pokazują ostatnie maksimum i powoli opadają z każdym przebiegiem;
   * **trójkąt u góry**, który wskazuje najsilniejszy sygnał z ostatniego przebiegu.
6. **SK1 + 0**: ustawia VFO na częstotliwość pod trójkątem, zaokrągloną do kroku VFO. Historia waterfalla przesuwa się razem z nią, nie jest kasowana.
7. **SK1 + 3**: skok do następnego presetu pasma. Presety to:
   * 144.800 APRS;
   * 145.500 2 m;
   * 433.925 LPD;
   * 438.800 przemienniki 70 cm;
   * 446.100 PMR446.

   Każde naciśnięcie przechodzi do następnego presetu powyżej bieżącej częstotliwości. Po ostatnim wraca na początek listy.
8. **RED**: wyjście z analizatora.

---

## 3. Polowanie na lisa (ekran RSSI)

1. Wejdź w **Menu → Sygnał** (RSSI). Możesz też przypisać RSSI do szybkiego klawisza.
2. **`#`**: włącza i wyłącza tryb lisa. Na ekranie pojawi się komunikat.
3. W trybie lisa:
   * co 0,6 s słychać krótki pisk;
   * im silniejszy sygnał, tym wyższy ton: 300 Hz przy −130 dBm, 2,4 kHz przy −40 dBm;
   * `Max: … dBm` to najsilniejszy dotychczasowy odczyt, oznaczony też kreską na S-metrze;
   * przy fixie GPS radio pamięta, gdzie był ten maksymalny odczyt. Pokazuje wtedy `Max @ 120m 045 deg`, czyli odległość i azymut od Twojej pozycji do tego miejsca.
4. **`0`**: kasuje maksimum. Maksimum kasuje się też przy każdym wejściu na ekran.

Podczas nadawania i gdy odtwarzany jest komunikat głosowy, pisk jest wstrzymany.

---

## 4. Waypoint (menu GPS, przedostatnia strona)

1. Wejdź w **Menu → GPS**.
2. Strzałkami **▲/▼** przejdź na **przedostatnią stronę**, czyli stronę waypointu.
3. Przy fixie GPS naciśnij **GREEN**. Pojawi się komunikat „Waypoint set”, a radio zapamięta bieżącą pozycję.
4. Strona pokazuje:
   * odległość do punktu (m lub km);
   * azymut z kierunkiem świata, np. `045 NE`;
   * strzałkę do punktu:
     * w ruchu, z napisem **Ahead=up**: „do góry” oznacza kierunek, w którym idziesz lub jedziesz;
     * w miejscu, z napisem **North=up**: „do góry” oznacza północ;
     * w promieniu 5 m od punktu strzałka zmienia się w kropkę.

Waypoint jest trzymany tylko w RAM, więc znika po wyłączeniu radia.

---

## 5. Licznik trasy (menu GPS, ostatnia strona)

Radio zbiera te dane w tle, niezależnie od tego, jaki ekran jest otwarty:

* `Dist`: przebyty dystans;
* `Move`: czas w ruchu;
* `Avg`: średnia prędkość w ruchu;
* `Max`: prędkość maksymalna;
* `Alt`: suma podejść i zejść.

Radio liczy tylko ruch szybszy niż ok. 2 km/h, więc drganie pozycji GPS na postoju nie dodaje dystansu. Zmiany wysokości mniejsze niż 5 m są pomijane jako szum.

**Długie GREEN** na tej stronie zeruje licznik. Dane są tylko w RAM i znikają po wyłączeniu radia.

---

## Wgrywanie

1. Zrób kopię codeplugu i kalibracji.
2. Włącz radio w trybie bootloadera: trzymaj PTT i przycisk nad nim podczas włączania.
3. Uruchom:

   ```sh
   cd MDUV380_firmware
   python3 tools/opengd77_stm32_firmware_loader.py -m MD-UV380 -f dist/OpenGD77_MDUV380.bin -s <oryginalny_firmware_TYT_Retevis.bin>
   ```

`-s` podajesz tylko za pierwszym razem. Loader wycina z niego kodek AMBE.

## Lista testów

Szczegółowa lista testów jest w [`rt3s_quickkeys_and_waterfall.md`](rt3s_quickkeys_and_waterfall.md).
