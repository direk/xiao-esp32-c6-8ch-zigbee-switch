# XIAO ESP32-C6 — osiem kanałów Zigbee

Wersja **2.1.2** dla Seeed Studio XIAO ESP32-C6 ze stałym zasilaniem USB i
anteną zewnętrzną U.FL albo wewnętrzną (osobne obrazy firmware’u). Urządzenie pracuje jako router Zigbee, bez usypiania
i bez heartbeat. Każdy kanał ma własny endpoint, konfigurację i bindingi.

[Kod źródłowy](https://github.com/direk/xiao-esp32-c6-8ch-zigbee-switch).
[Wydanie 2.1.2 — firmware, konwerter i kompletna paczka](https://github.com/direk/xiao-esp32-c6-8ch-zigbee-switch/releases/tag/v2.1.2).
Skrypt pakujący generuje również lokalne `dist/xiao-8ch-v2.1.2.zip`.
[Wydanie 1.2.0 na GitHubie](https://github.com/direk/xiao-esp32-c6-8ch-zigbee-switch/releases/tag/v1.2.0)
jest starszą wersją czterokanałową.

## Podłączenie

Każdy przycisk lub przełącznik podłącz między swoim pinem a **GND**.
Wejścia mają wewnętrzne podciąganie do 3,3 V i debounce 35 ms.
Do GPIO wolno podłączać wyłącznie styki bezpotencjałowe, nigdy 230 V.

| Kanał / endpoint | Pin XIAO | GPIO |
|---|---|---|
| CH1 / 1 | D0 | 0 |
| CH2 / 2 | D1 | 1 |
| CH3 / 3 | D2 | 2 |
| CH4 / 4 | D3 | 21 |
| CH5 / 5 | D4 | 22 |
| CH6 / 6 | D5 | 23 |
| CH7 / 7 | D8 | 19 |
| CH8 / 8 | D9 | 20 |

![Schemat pinów Seeed Studio XIAO ESP32-C6](docs/images/xiao-esp32-c6-pinout.png)

Schemat oznaczeń płytki; przypisanie kanałów CH1–CH8 podaje tabela powyżej.

D6/TX (GPIO16) i D7/RX (GPIO17) obsługują komendy UART 3,3 V:
115200 baud, 8N1, bez kontroli przepływu. D10 (GPIO18) pozostaje wolny. CH7/CH8 zajmują D8/D9, więc te piny nie są dostępne dla SPI.
Przy aktualizacji poprzedniej wersji 8CH przenieś przewód CH7 z D6 na D8,
a CH8 z D7 na D9. Numery kanałów, ustawienia i bindingi pozostają te same.
Te same komendy są dostępne przez USB-C (USB Serial/JTAG, GPIO12/13). GPIO3=LOW włącza przełącznik antenowy; GPIO14=HIGH wybiera
antenę zewnętrzną, GPIO14=LOW wewnętrzną; **D3 to GPIO21**, niezależny od GPIO3.

## Przyciski przez UART i USB-C

Urządzenie zewnętrzne może symulować CH1–CH8 przez `PRESS`, `RELEASE`,
`CLICK`, `DOUBLE`, `TRIPLE`, `HOLD` oraz `SET` dla rocker. Komendy są tekstowe,
kończone LF; np. `CLICK 2\n` symuluje kliknięcie CH2. Obowiązują te same
tryby i binding priority co dla fizycznych przycisków. UART i USB-C mają
osobne zadania FreeRTOS i niezależne sesje identyfikatorów; komendy trafiają do Zigbee przez ograniczoną
kolejki, bez czekania na porty w obsłudze Zigbee. Odpowiedź wraca na port
żądania. Pełna kolejka USB nie blokuje UART.
`GET 2\n` zwraca jedną linię JSON z pełną konfiguracją CH2, dozwolonymi
komendami UART (`commands`) i gestami dającymi efekt (`gestures`). Odczyt
nie zmienia kanału i działa również przed gotowością sieci Zigbee.

W protokole UART v2 `INFO` zwraca wersję firmware’u/protokołu i numer uruchomienia,
a `STATUS` stan sieci, bindingi, wejścia i liczniki błędów. Opcjonalne rosnące
identyfikatory (np. `@42 CLICK 2`) chronią przed ponownym wykonaniem przy utracie
odpowiedzi. Błędy odbioru UART odrzucają uszkodzoną linię do następnego LF.
`RESET` kasuje dane sieci Zigbee i bindingi, zachowuje ustawienia kanałów i
restartuje urządzenie, które automatycznie rozpoczyna parowanie. W Z2M trzeba
włączyć Permit join. Zwykłe komendy UART z 2.0.0 nadal działają bez zmian.
`LOG 0` wyłącza, a `LOG 1` włącza logi tylko na porcie, z którego wysłano
komendę. Domyślnie oba porty mają logi wyłączone; po restarcie znów są wyłączone.
Logi mają prefiks `#LOG `, a INFO/STATUS pokazują port i `logs_enabled`.
Konwerter Z2M z wydania 2.0.0 pozostaje zgodny; klaster ustawień nie zmienił się.
Warianty 2.1.2 mają ten sam układ partycji i konfiguracji; zmiana anteny nie wymaga resetu sieci.
[Historia zmian](CHANGELOG.md) · [Zakres wykonanych testów](docs/VALIDATION.md).

Podłącz TX urządzenia do D7/RX, RX do D6/TX i wspólną masę.
Prawdziwy RS-232 wymaga konwertera poziomów (np. MAX3232); nie podłączaj go
bezpośrednio do GPIO. [Pełny opis protokołu i odpowiedzi](docs/UART.md).

## Ustawienia kanału

- `input_mode`: `push` dla przycisku chwilowego, `rocker` dla przełącznika
  dwupozycyjnego. `rocker + toggle` wysyła Toggle przy każdej zmianie styku
  (zwarcie i rozwarcie), niezależnie od stanu lampy ustawionego przez inne
  automatyzacje. `rocker + momentary` daje ON przy zwarciu i OFF przy rozwarciu.
  Pozostałe funkcje wymagają `push`.
- `invert`: odwraca aktywny poziom wejścia.
- `binding_priority`: `ON` (domyślnie) pomija `action`, gdy polecenie idzie
  przez binding. `OFF` zachowuje sterowanie przez binding i dodatkowo wysyła
  `action`. Opcja jest niezależna dla CH1–CH8 i zapisywana na urządzeniu.
  Bez pasującego bindingu kanał zawsze wysyła `action`.
- `switch_type`: funkcja z tabeli poniżej, osobna dla każdego kanału.
- `switch_actions`: `off`, `on` lub `toggle` dla wejścia push w funkcjach
  `toggle` i `momentary`. Domyślnie `toggle`.
- `min_level`, `max_level`: zakres jasności 1–254, domyślnie 1 i 254.
- `transition_time`: czas przejścia w 0,1 s, domyślnie 25 (2,5 s), zakres 0–600.
- `color_temp_min`, `color_temp_max`: granice temperatury w miredach,
  domyślnie 153 i 500; dopasuj do możliwości lampy.
- `scene_id`, `group_id`: numer sceny i grupy dla funkcji `scene`.

Przed wybraniem zaawansowanej funkcji ustaw `input_mode=push`. Próba użycia jej
z wejściem rocker zostanie odrzucona. Przy zmianie granic min/max najpierw ustaw
tę granicę, która pozwoli zachować poprawny zakres. Zmiany zapisują się w NVS
po sekundzie od ostatniej zmiany; zmiana parametrów wejścia lub funkcji
zatrzymuje trwającą regulację. `binding_priority` działa od następnego polecenia
i nie przerywa przytrzymania. Po aktualizacji istniejące urządzenia mają tę
opcję `ON` na wszystkich kanałach; pozostałe ustawienia są zachowane.

## Funkcje przycisków

Dla funkcji rozpoznających gesty próg przytrzymania i okno wielokliku wynoszą
500 ms. Puszczenie przycisku zatrzymuje regulację jasności/temperatury.
Krok jasności to **25 jednostek**, nie 25%; krok temperatury to 87 miredów.

| `switch_type` | Krótko | Dwuklik | Przytrzymanie |
|---|---|---|---|
| `toggle` | Wybrana komenda ON/OFF/Toggle przy naciśnięciu | Każde naciśnięcie działa osobno | Brak dodatkowej komendy |
| `momentary` | Wybrana komenda przy naciśnięciu, odwrotna po puszczeniu | Każde naciśnięcie działa osobno | Brak dodatkowej komendy |
| `multifunction` | `single` | `double` | `hold`, potem `release`; również `triple` |
| `brightness_level` | Toggle | Brak | Naprzemiennie jaśniej/ciemniej |
| `brightness_level_up` | ON | Krok jaśniej, z ON | Do `max_level`, z ON |
| `brightness_level_down` | OFF | Krok ciemniej, może wyłączyć na minimum | Do `min_level`, bez wyłączania |
| `move_to_color_temperature` | Brak | Brak | Naprzemiennie cieplej/chłodniej |
| `move_to_color_temperature_up` | Krok w stronę większej wartości mired | Brak | Do `color_temp_max` |
| `move_to_color_temperature_down` | Krok w stronę mniejszej wartości mired | Brak | Do `color_temp_min` |
| `scene` | Recall Scene przy naciśnięciu | Każde naciśnięcie działa osobno | Brak dodatkowej komendy |
| `brightness_level_no_onoff` | Brak | Brak | Naprzemiennie jaśniej/ciemniej, bez ON/OFF |
| `brightness_level_up_no_onoff` | Brak | Krok jaśniej, bez ON/OFF | Do `max_level`, bez ON/OFF |
| `brightness_level_down_no_onoff` | Brak | Krok ciemniej, bez ON/OFF | Do `min_level`, bez ON/OFF |

Tryby `*_no_onoff` używają Level Control bez wariantu With On/Off, z jawnym
`ExecuteIfOff=false`. Zgaszone lampy mają ignorować regulację; zapalone zmieniają
jasność. Zachowanie zależy od implementacji lampy i wymaga testu na jej firmware.

## Bindingi i `action`

Bindingi muszą odpowiadać **rodzajowi polecenia**, nie tylko numerowi kanału:

| Polecenie | Klaster do bindowania |
|---|---|
| ON/OFF/Toggle | `genOnOff` (0x0006) |
| Jasność i Stop | `genLevelCtrl` (0x0008) |
| Temperatura barwowa i Stop | `lightingColorCtrl` (0x0300) |
| Sceny | `genScenes` (0x0005) |

Polecenie z pasującym bindingiem jest wysyłane bezpośrednio. Przy
`binding_priority_chN=ON` nie generuje `action`, a przy `OFF` dodatkowo
raportuje `action`.
Polecenie bez pasującego bindingu generuje `action` do Z2M. Dlatego kanał
`brightness_level_up` może np. przełączać lampę przez `genOnOff`, a regulację
wysyłać jako `action`, jeśli nie ma bindingu `genLevelCtrl`.

Wyjątki wynikające z wybranej funkcji:

- `multifunction` zawsze raportuje gesty jako `action`, niezależnie od bindingów.
- `scene` z niezerowym `group_id` wysyła Recall Scene bezpośrednio do tej grupy;
  `group_id=0` korzysta z bindingu Scenes albo raportuje `action`. Przy
  `binding_priority=OFF` bezpośrednie wywołanie grupy również raportuje `action`.
- Krótkie kliknięcia celowo ignorowane przez wybrany tryb, np. w trybach
  brightness-only, nie generują zastępczego `action`.

Bindingi skonfiguruj w zakładce Bind XIAO, wybierając źródłowy endpoint `ch1` …
`ch8` oraz lampę/grupę. Nie binduj klastrów sterowania z XIAO do koordynatora.
Łączna pojemność tablicy to 32 wpisy, wspólne dla ośmiu kanałów i czterech klastrów.
Lokalny odczyt tabeli odbywa się co sekundę i po Bind/Unbind; po zmianie odczekaj
około sekundy. Nie wymaga działającego Z2M. Po starcie wejścia czekają na pierwszy
pełny odczyt tabeli.

W Z2M dostępne są `binding_status_chN`, `binding_clusters_chN` (bity: 1 On/Off,
2 Level, 4 Color, 8 Scenes) oraz `control_path_chN`: `binding`, `z2m`, `mixed`.
`mixed` oznacza, że tylko część klastrów potrzebnych danej funkcji ma binding.
Diagnostyka opisuje drogę sterowania lampą; dodatkowe `action` przy
`binding_priority=OFF` nie zmienia wartości `control_path`.

Przykład wiadomości na `zigbee2mqtt/NAZWA_URZADZENIA/set`:

```json
{"binding_priority_ch1":"OFF","binding_priority_ch2":"ON"}
```

Po podmianie konwertera i aktualizacji firmware’u uruchom ponownie Z2M oraz
wykonaj Reconfigure, aby odczytać nowe ustawienia. Odczekaj sekundę po zmianie
przed odłączeniem zasilania. Przy `OFF` automatyzacja HA reagująca na `action`
może ponownie przełączyć lampę już obsłużoną przez binding; uwzględnij to
w automatyzacjach.

Akcje mają postać `chN_off/on/toggle`, `chN_single/double/triple/hold/release`,
`chN_brightness_move_up/down`, `chN_brightness_step_up/down`,
`chN_brightness_stop`, analogiczne `color_temperature_*` i `chN_scene`.
Automatyzacje HA konfigurujesz samodzielnie. Bezpośrednie sterowanie lampami
nie wymaga HA ani MQTT. Wymaga zasilonych lamp i działającej drogi Zigbee.

## Przykład: dwa światła i wspólne ściemnianie

- CH1 steruje grupą A przez `genOnOff`, CH2 grupą B przez `genOnOff`.
- Utwórz grupę A+B i dodaj do niej wszystkie indywidualne lampy z A i B.
- CH3: `brightness_level_up_no_onoff`, binding `genLevelCtrl` do A+B.
- CH4: `brightness_level_down_no_onoff`, binding `genLevelCtrl` do A+B.
- CH5–CH8 mogą niezależnie obsługiwać kolejne grupy, sceny lub gesty HA.

Dwuklik daje krok regulacji, a hold przejście do granicy. Gdy świeci tylko A,
zmieniać powinna się tylko A, a B pozostać zgaszona. Sprawdź również obie grupy
zgaszone, obie zapalone i zwolnienie przycisku podczas regulacji.

## Konwerter Z2M i aktualizacja z wersji czterokanałowej

1. Wgraj samą aplikację 2.1.2 pod 0x10000, **bez erase_flash**. Układ partycji
   pozostaje bez zmian. Firmware migruje ustawienia input_mode/invert CH1–CH4,
   zachowuje sieć i bindingi. CH5–CH8 startują jako push/toggle.
2. Zastąp poprzedni konwerter plikiem `zigbee2mqtt/xiao-c6-8ch.mjs`.
   Usuń starą kopię; nie ładuj jednocześnie konwertera 4CH i 8CH.
3. Uruchom ponownie Z2M. Dla istniejącego urządzenia wykonaj ponowny interview,
   aby Z2M odczytał endpointy 5–8 i nowe klastry. Następnie wykonaj konfigurację.
4. Nowy model to `XIAO-C6-8CH`. Nazwę MQTT istniejącego urządzenia możesz zachować.
   Konwerter rozpoznaje też poprzedni identyfikator, aby ułatwić aktualizację.
5. W nowych instalacjach włącz permit join w Z2M; fabrycznie nowy XIAO próbuje
   dołączyć automatycznie. Przykład konfiguracji Z2M jest w `zigbee2mqtt/`.

Ustawienia zmieniasz przez Exposes lub MQTT, np. na `zigbee2mqtt/xiao_4ch/set`
(jeśli zachowałeś wcześniejszą nazwę):

```json
{"input_mode_ch8":"push","switch_type_ch8":"brightness_level_up_no_onoff"}
```

Nie publikuj komend `/set` z retain. Korzystaj z `retain: false`, `debounce: 0`
i `throttle: 0`. W Z2M wymagającym włączenia zewnętrznych konwerterów ustaw
`advanced.enable_external_js: true`.

## Wgranie obrazów

Rozpakuj `dist/xiao-8ch-v2.1.2.zip`. Wybierz wariant odpowiadający antenie:

| Antena | Aktualizacja (0x10000) | Czysta instalacja (0x0) |
|---|---|---|
| Zewnętrzna U.FL | `application-external.bin` | `firmware-factory-external.bin` |
| Wewnętrzna | `application-internal.bin` | `firmware-factory-internal.bin` |

Przykłady poniżej używają anteny zewnętrznej. Dla wewnętrznej zamień
`external` na `internal` w nazwie pliku. W katalogu rozpakowanej paczki:

```sh
python3 -m venv .venv
source .venv/bin/activate
python -m pip install esptool==4.14.0
python -m serial.tools.list_ports -v
```

Zastąp `PORT` portem XIAO, np. `/dev/cu.usbmodem...` lub `COM5`. Tylko do
pierwszej, czystej instalacji:

```sh
python -m esptool --chip esp32c6 --port PORT erase_flash
python -m esptool --chip esp32c6 --port PORT write_flash 0x0 firmware-factory-external.bin
```

`erase_flash` usuwa sieć, bindingi i wszystkie ustawienia. Do aktualizacji z 1.x
lub kolejnej wersji z tym samym układem partycji użyj wyłącznie:

```sh
python -m esptool --chip esp32c6 --port PORT write_flash 0x10000 application-external.bin
```

Model/endpointy po takiej aktualizacji odśwież w Z2M przez interview, nie reset
fabryczny. Gdy port nie jest widoczny, przytrzymaj BOOT podczas podłączania USB,
a potem puść przycisk. Aktywacja venv na Windows: `.venv\Scripts\activate`.

## Test odbiorczy

Sprawdź wszystkie osiem wejść, tryby gestów, niezależne bindingi do lamp i grup,
regulację przy zgaszonych lampach, Stop po puszczeniu, sceny i działanie przy
wyłączonym Z2M/koordynatorze. Sprawdź konfigurację po restarcie oraz to, że
pozostałe kanały nie reagują na przycisk innego kanału. Po aktualizacji deskryptora
Z2M musi widzieć osiem endpointów. Testy komputerowe nie zastępują tego sprawdzenia.

OTA przez Zigbee nie jest zaimplementowane; aktualizacja odbywa się przez USB.

## Reset urządzenia

Przytrzymanie BOOT przez 8 sekund **podczas pracy** kasuje ustawienia wejść,
sieć i bindingi. Krótkie naciśnięcie RESET ich nie kasuje.

## Budowa firmware’u od zera

Poniższe kroki są dla macOS. Projekt rozpakuj do katalogu bez spacji w ścieżce,
np. `~/esp/xiao-4ch`. Do kompilacji nie trzeba podłączać XIAO. Pierwsza kompilacja
wymaga internetu do pobrania narzędzi oraz komponentów Zigbee.

Jeżeli masz Git, kod możesz pobrać zamiast rozpakowywania ZIP-a:

```sh
mkdir -p "$HOME/esp"
git clone https://github.com/direk/xiao-esp32-c6-8ch-zigbee-switch.git "$HOME/esp/xiao-8ch"
```

### A. Instalacja narzędzi — jednorazowo

Potrzebne są Git, Python 3, CMake i Ninja. Jeśli masz Homebrew:

```sh
brew install git python cmake ninja dfu-util
```

Jeżeli brakuje narzędzi programistycznych Apple, uruchom `xcode-select --install`
i zakończ instalację przed kontynuowaniem. Jeśli nie używasz Homebrew,
zainstaluj te zależności według [instrukcji Espressifa dla macOS/Linux](https://docs.espressif.com/projects/esp-idf/en/v5.4.2/esp32c6/get-started/linux-macos-setup.html).

Pobierz dokładnie **ESP-IDF 5.4.2**, wraz z podmodułami, i zainstaluj narzędzia
ESP32-C6. Ten katalog jest osobną instalacją narzędzi, poza projektem:

```sh
mkdir -p "$HOME/esp"
git clone --branch v5.4.2 --recursive https://github.com/espressif/esp-idf.git "$HOME/esp/esp-idf-v5.4.2"
cd "$HOME/esp/esp-idf-v5.4.2"
./install.sh esp32c6
```

Nie klonuj ponownie, jeśli ta instalacja już istnieje. Możesz wskazać własny
katalog ESP-IDF, ale musi to być wersja 5.4.2 z zainstalowanym toolchainem ESP32-C6.

Na Windows użyj [instalatora i terminala ESP-IDF 5.4.2](https://docs.espressif.com/projects/esp-idf/en/v5.4.2/esp32c6/get-started/windows-setup.html).
Dalsze polecenia `idf.py` działają w terminalu ESP-IDF; składnia aktywacji poniżej
dotyczy macOS/Linux. Pełną kompilację tego projektu sprawdzono na macOS.

### B. Aktywacja środowiska — w każdym nowym terminalu

```sh
. "$HOME/esp/esp-idf-v5.4.2/export.sh"
idf.py --version
```

Wynik ma wskazywać `ESP-IDF v5.4.2`. Nie buduj projektu w środowisku `.venv`
utworzonym wcześniej wyłącznie do wgrywania gotowego obrazu — użyj terminala
aktywowanego przez `export.sh`.

### C. Pierwsza kompilacja po pobraniu lub czyszczeniu projektu

Przejdź do głównego katalogu własnej kopii projektu. Dla przykładowej
lokalizacji `~/esp/xiao-8ch`:

```sh
cd "$HOME/esp/xiao-8ch"
python scripts/build_firmware.py
```

Jeżeli projekt jest w innym miejscu, zmień pierwszą ścieżkę. Nie trzeba ręcznie
instalować bibliotek Zigbee ani odtwarzać usuniętych plików. Component Manager
pobiera `esp-zigbee-lib` i `esp-zboss-lib` **1.6.9** na podstawie
`firmware/main/idf_component.yml` oraz `firmware/dependencies.lock`. Nie podmieniaj ich na SDK 2.x.

Skrypt zawsze kompiluje **oba warianty**. Powstaną `firmware/managed_components/`
oraz osobne katalogi `firmware/build/external/` i `firmware/build/internal/`,
każdy z własnym `sdkconfig`. Ustawienia początkowe pochodzą z
`sdkconfig.defaults`: router Zigbee, ESP32-C6, 4 MB flash, UART/USB i wyłączone
logi. CMake ustawia GPIO14 dla właściwej anteny; GPIO3 pozostaje LOW.

| Plik względem `firmware/` | Zastosowanie |
|---|---|
| `build/external/xiao_zigbee_8ch.bin` | Aplikacja dla anteny zewnętrznej; 0x10000 |
| `build/internal/xiao_zigbee_8ch.bin` | Aplikacja dla anteny wewnętrznej; 0x10000 |
| `build/<wariant>/bootloader/bootloader.bin` | Bootloader; 0x0 |
| `build/<wariant>/partition_table/partition-table.bin` | Tablica partycji; 0x8000 |

Przy kolejnych zmianach uruchom ponownie `python scripts/build_firmware.py`.
Nie zmieniaj ręcznie kodu ani `sdkconfig`, aby przełączać antenę. Bezpośrednie
`idf.py build` jest kompilacją pojedynczego wariantu i wymaga jawnego
`-D XIAO_ANTENNA=external` albo `internal`; bez wyboru konfiguracja zostanie
odrzucona. Standardowy skrypt projektu zawsze buduje oba.

### D. Wgranie i sprawdzenie USB — osobny, opcjonalny krok

Sprawdź porty, będąc w aktywnym środowisku ESP-IDF:

```sh
python -m serial.tools.list_ports -v
```

Z głównego katalogu projektu wgraj wybrany wariant, podając prawdziwy port
(przykład: antena zewnętrzna):

```sh
cd firmware
idf.py -B build/external -p PORT flash monitor
```

Na macOS XIAO zwykle ma port `/dev/cu.usbmodem...`; na Windows np. `COM5`.
Zamknij inny program używający tego portu. Monitor kończy się przez `Ctrl+]`.
Logi są domyślnie wyłączone. Aby sprawdzić urządzenie, otwórz port USB
w terminalu wysyłającym LF/CRLF i wyślij `INFO` — otrzymasz JSON z wersją
firmware'u. `STATUS` podaje stan Zigbee. Do diagnostyki włącz `LOG 1` na
wybranym porcie; wyłącz go przez `LOG 0`. Monitor ESP-IDF służy głównie do
odczytu i może wymagać ręcznego wysłania LF po komendzie. Komunikaty ROM lub
starego bootloadera podczas restartu nie podlegają LOG.

`idf.py flash` zapisuje osobne obrazy pod ich adresami; przy niezmienionym układzie
partycji nie kasuje ustawień ani bindingów. Jeśli poprzedni firmware używał innego
układu partycji i potrzebna jest czysta instalacja, przed wgrywaniem wykonaj
`idf.py -B build/external -p PORT erase-flash` — **to usuwa wszystkie ustawienia i sieć**.

## Testy konwertera i logiki wejść — opcjonalnie

Nie są wymagane do zbudowania firmware’u. Uruchamiaj je z **głównego katalogu
projektu**, nie z `firmware`. Potrzebujesz Node.js **24.x** z npm; sprawdzono
Node.js 24.10.0. Test C wymaga kompilatora `cc` i sanitizerów (na macOS zapewniają
je narzędzia programistyczne Apple).

```sh
cd "$HOME/esp/xiao-8ch"
npm ci --ignore-scripts
npm test
npm run test:control
npm run test:serial
npm run test:transport
npm run test:session
```

Testy konwertera używają przypiętych zależności z `package-lock.json` i rzeczywistego
kodowania/dekodowania ramek ZCL. Testy C sprawdzają wejścia i obsługę bindingów.

## Pakowanie wydania

W aktywnym środowisku ESP-IDF, z głównego katalogu projektu uruchom:

```sh
python scripts/package_release.py --idf-path "$IDF_PATH"
```

Skrypt najpierw kompiluje oba warianty (również przyrostowo), a potem używa
programu esptool zainstalowanego razem z ESP-IDF; nie wymaga
ręcznego kopiowania obrazów. Utworzy `dist/xiao-8ch-v2.1.2.zip` w głównym
katalogu projektu i usunie swoje pliki tymczasowe. ZIP zawiera obrazy do wgrania,
źródła, konwerter, dokumentację, licencje i sumy kontrolne. Obok ZIP powstaną
cztery obrazy z nazwami anten, konwerter, `manifest.json` i `SHA256SUMS` do
publikacji w release. Manifest opisuje antenę, adres i SHA256 każdego obrazu.
Brak kompilacji któregokolwiek wariantu przerywa pakowanie. GitHub Actions
wykonuje ten sam proces dla main, pull requestów i tagów; artefakt
`xiao-both-antennas` zawiera oba warianty.

Po zakończeniu pracy można ponownie usunąć wygenerowane `firmware/build/`,
`firmware/managed_components/`, `firmware/sdkconfig`, `firmware/sdkconfig.old`
i `node_modules/`. Zachowaj pliki `sdkconfig.defaults`, `dependencies.lock`
i `package-lock.json` — są częścią odtwarzalnego projektu.

## Dokumentacja źródłowa

- [Seeed XIAO ESP32-C6: piny i przełącznik anteny](https://wiki.seeedstudio.com/xiao_esp32c6_getting_started/)
- [Espressif SDK 1.x, przykład On/Off Switch](https://github.com/espressif/esp-zigbee-sdk/tree/release/v1.0/examples/esp_zigbee_HA_sample/HA_on_off_switch)
- [Z2M: binding endpointów i grup](https://www.zigbee2mqtt.io/guide/usage/binding.html)
- [Z2M: external converters](https://www.zigbee2mqtt.io/advanced/more/external_converters.html)
- [Z2M: opcje urządzeń i filtered_cache](https://www.zigbee2mqtt.io/guide/configuration/devices-groups.html)
