# XIAO ESP32-C6 — cztery kanały Zigbee: binding lub action

[Kod źródłowy](https://github.com/direk/xiao-esp32-c6-4ch-zigbee-switch) ·
[Firmware 1.2.0 do pobrania](https://github.com/direk/xiao-esp32-c6-4ch-zigbee-switch/releases/tag/v1.2.0)

Firmware dla **Seeed Studio XIAO ESP32-C6**, zasilanego stale, ze **zewnętrzną
anteną**. Każdy z czterech kanałów ma własny endpoint i niezależne bindingi.
Kanał z bindingiem zawsze steruje bezpośrednio lampą lub grupą. Kanał bez bindingu
wysyła `action` do Zigbee2MQTT. Wybór drogi nie zależy od dostępności HA/Z2M.

**Stan sprawdzenia:** firmware skompilowany dla ESP32-C6 z ESP-IDF 5.4.2 i bibliotekami
Espressifa Zigbee/ZBOSS 1.6.9. Konwerter sprawdzany z
`zigbee-herdsman-converters` 26.135.0. Testy opisuje `docs/VALIDATION.md`.
Firmware wgrano na fizyczne XIAO ESP32-C6; sprawdzenie aktualizacji opisuje dokument walidacji.
Pełny test wejść, lamp i zasięgu opisano w instrukcji odbiorczej poniżej.

## Co jest w projekcie

- [Wydanie 1.2.0](https://github.com/direk/xiao-esp32-c6-4ch-zigbee-switch/releases/tag/v1.2.0) — gotowe obrazy i pełna paczka ZIP. Lokalnie skrypt pakujący zapisuje ZIP w `dist/`.
- `firmware/` — pełne źródła ESP-IDF i przypięte wersje zależności.
- `zigbee2mqtt/xiao-c6-4ch.mjs` — konwerter z konfiguracją czterech wejść.
- `docs/PROTOCOL.md` — opis protokołu oraz dokładnych ograniczeń.

## 1. Podłączenie przycisków

| Kanał / endpoint | Pin XIAO | GPIO ESP32-C6 | Drugi styk |
|---|---|---|---|
| 1 | D0 | 0 | GND |
| 2 | D1 | 1 | GND |
| 3 | D2 | 2 | GND |
| 4 | D3 | 21 | GND |

Wejścia mają wewnętrzne podciąganie do 3,3 V i filtr drgań styków 35 ms.
**Do GPIO podłączaj wyłącznie styki bezpotencjałowe. Nie podłączaj 230 V.**
XIAO zasil przez USB-C lub odpowiednie zasilanie płytki. To pilot Zigbee;
firmware nie obsługuje czterech lokalnych przekaźników.

W tej wersji wybrana jest **zewnętrzna antena** podłączona do gniazda U.FL:
GPIO3=LOW aktywuje przełącznik RF, GPIO14=HIGH wybiera antenę zewnętrzną.
Log startowy potwierdza odczyt obu poziomów. Powrót na antenę wbudowaną wymaga
zmiany `EXTERNAL_ANTENNA` na 0 i ponownej kompilacji.

## 2. Wgranie gotowego firmware’u

Pobierz i rozpakuj `xiao-4ch-v1.2.0.zip` z [wydania 1.2.0](https://github.com/direk/xiao-esp32-c6-4ch-zigbee-switch/releases/tag/v1.2.0).
`firmware-factory.bin` to połączony obraz do wgrania pod
adres **0x0**. `application.bin` to sama aplikacja pod **0x10000**.

Pierwsze uruchomienie, w terminalu w katalogu rozpakowanej paczki:

```sh
python3 -m venv .venv
source .venv/bin/activate
python -m pip install esptool==4.14.0
python -m esptool --chip esp32c6 --port PORT erase_flash
python -m esptool --chip esp32c6 --port PORT write_flash 0x0 firmware-factory.bin
```

Zastąp `PORT` portem USB XIAO, np. `/dev/cu.usbmodem...` lub `COM5`. Na Windows
aktywacja środowiska: `.venv\Scripts\activate`.
**`erase_flash` usuwa dotychczasowy firmware, sieć i ustawienia.** Te polecenia są
przeznaczone do pierwszej instalacji. Jeżeli port nie pojawia się, przytrzymaj
BOOT podczas podłączania USB, następnie puść przycisk.

Kolejna aktualizacja tej samej wersji układu partycji, z zachowaniem ustawień:

```sh
python -m esptool --chip esp32c6 --port PORT write_flash 0x10000 application.bin
```

Nie używaj obrazu factory do aktualizacji z zachowaniem konfiguracji: zawiera
również puste obszary pomiędzy bootloaderem i aplikacją.

## 3. Dodanie obsługi w Zigbee2MQTT

1. Skopiuj `zigbee2mqtt/xiao-c6-4ch.mjs` do katalogu `external_converters` obok
   `configuration.yaml` Z2M. Możesz też dodać go w interfejsie Z2M w sekcji
   Settings → Dev console → External converters.
2. W nowszych wersjach Z2M włącz `advanced.enable_external_js: true`.
3. Uruchom ponownie Z2M i włącz dodawanie urządzeń. Włącz XIAO. Fabrycznie nowy
   firmware szuka otwartej sieci i ponawia próbę co 10 sekund.
4. Urządzenie powinno zostać rozpoznane jako `XIAO-C6-4CH-FAIL1`. Nadaj mu nazwę
   **`xiao_4ch`**. Jeżeli było już dodane bez konwertera, uruchom ponowny interview
   oraz konfigurację urządzenia.
5. Po sparowaniu zastosuj ustawienia z `zigbee2mqtt/configuration.example.yaml`,
   wpisując prawdziwy adres IEEE. Połącz je z istniejącą konfiguracją — nie
   zastępuj jej całej. Ustaw `retain: false`, `debounce: 0` i `throttle: 0`.

Firmware uruchamia się jako **router Zigbee**, nigdy jako drugi koordynator.
Dołącza do tej samej sieci co istniejący koordynator i lampy.

## 4. Konfiguracja rocker / push

Używaj firmware’u i konwertera **1.2.0**. Zastąp poprzedni konwerter tym
plikiem, uruchom ponownie Z2M i odśwież panel. Nie dodawaj drugiej kopii
konwertera dla tego samego modelu. Aktualizacja aplikacji pod adres 0x10000
zachowuje sieć i bindingi; nie uruchamiaj `erase_flash` przy aktualizacji.

W zakładce Exposes znajdziesz `input_mode_ch1` … `input_mode_ch4` oraz
`invert_ch1` … `invert_ch4`.

| Ustawienie | Działanie |
|---|---|
| `push` — domyślne | Naciśnięcie daje Toggle; puszczenie nic nie wysyła |
| `rocker` | Zwarcie styku do GND daje ON; rozwarcie daje OFF |
| `invert_chN: true` | Odwraca aktywny poziom wejścia |

Każdy kanał może mieć inny typ wejścia. Przykład MQTT na
`zigbee2mqtt/xiao_4ch/set`, **bez retain**:

```json
{"input_mode_ch1":"rocker","input_mode_ch2":"push","input_mode_ch3":"rocker","input_mode_ch4":"push"}
```

Zmiany zapisują się po około sekundzie. Zmiana ustawienia ani restart nie wysyła
komendy do lampy. Rocker reaguje na kolejną zmianę pozycji; po starcie nie wymusza
automatycznie zgodności światła z pozycją klawisza.

## 5. Bindingi — osobno dla wszystkich kanałów

Skonfiguruj bindingi dla kanałów, które mają bezpośrednio sterować światłem. W zakładce Bind urządzenia XIAO:

1. Wybierz źródłowy endpoint `ch1` / 1.
2. Wybierz lampę albo grupę lamp kanału 1 i zaznacz **wyłącznie `genOnOff`**.
3. Kliknij Bind. Dla pojedynczej lampy wybierz jej endpoint obsługujący On/Off;
   nie zakładaj, że każda lampa używa endpointu 1.
4. Powtórz dla `ch2` / 2, `ch3` / 3 i `ch4` / 4.

Przykładowe żądania MQTT na `zigbee2mqtt/bridge/request/device/bind`:

```json
{"from":"xiao_4ch","from_endpoint":"ch1","to":"lampa_1","to_endpoint":1,"clusters":["genOnOff"]}
{"from":"xiao_4ch","from_endpoint":"ch2","to":"lampa_2","to_endpoint":1,"clusters":["genOnOff"]}
{"from":"xiao_4ch","from_endpoint":"ch3","to":"lampa_3","to_endpoint":1,"clusters":["genOnOff"]}
{"from":"xiao_4ch","from_endpoint":"ch4","to":"lampa_4","to_endpoint":1,"clusters":["genOnOff"]}
```

Każdy wiersz wyślij jako osobną wiadomość. Nazwy lamp i ich endpointy są
przykładowe. Dla grupy podaj jej nazwę i pomiń `to_endpoint`. Sprawdź odpowiedź
Z2M: `status: ok`, `failed: []`. Tablica mieści łącznie 32 bindingi.

**Nie binduj `genOnOff` z XIAO do koordynatora.** Normalne zdarzenia do HA mają
oddzielną drogę przez klaster konfiguracyjny; bindingi wskazują odbiorniki
bezpośredniego sterowania. Lampy muszą przyjmować standardowe komendy Zigbee On/Off;
sam fakt obecności lampy w Z2M tego nie gwarantuje.

## 6. Home Assistant

Dla kanałów bez bindingu przypisz zdarzenia `action` do własnych automatyzacji HA.
Dostępne są `chN_toggle`, `chN_on` i `chN_off` dla kanałów 1–4. Kanały z bindingiem
sterują bezpośrednio i nie wysyłają `action`. Projekt nie wymaga dodatkowego
pakietu YAML w HA. Unikaj kilku automatyzacji obsługujących to samo naciśnięcie.

## Wybór drogi dla każdego kanału

| Binding On/Off do lampy/grupy | Działanie przycisku |
|---|---|
| Jest | Zawsze bezpośrednia komenda przez binding; bez `action` |
| Brak | Zawsze `action` do Z2M; bez bezpośredniej komendy do lampy |

Dostępność Z2M, MQTT i HA nie zmienia wyboru drogi. Zatrzymany Z2M nie odbierze
wysyłanego `action`, ale kanały z bindingiem działają tak samo jak wcześniej.
Nie ma oczekiwania na wykrycie awarii ani powrotu do innego trybu.

ESP odczytuje **własną tablicę bindingów**, również bez koordynatora. Liczą się
wpisy `genOnOff` dla danego endpointu do urządzenia lub grupy. Wpisy do siebie
oraz rozpoznanego koordynatora są pomijane. Tablica jest odświeżana lokalnie co
sekundę oraz po odebraniu Bind/Unbind. Po zmianie bindingu poczekaj około sekundy.
Nowy wynik zastępuje poprzedni dopiero po odczytaniu wszystkich stron tabeli.
Przy błędzie odczytu zachowywany jest poprzedni pełny wynik. Podczas startu
obsługa wejść czeka na pierwszy poprawny odczyt tabeli, zwykle kilkadziesiąt ms.

W Exposes są osobne pola dla kanałów:

- `binding_status_ch1` … `ch4`: `unknown`, `unbound`, `bound`;
- `control_path_ch1` … `ch4`: `z2m` albo `binding`.

Aktualizacja samej aplikacji zachowuje ustawienia wejść, sieć i bindingi.
Model urządzenia pozostaje ten sam; ponowne parowanie nie jest potrzebne.

Nie ma okresowych wyzwań ani odpowiedzi. Zmiany diagnostyki są raportowane tylko
po zmianie wartości; konwerter odczytuje je również podczas konfiguracji.
Binding wymaga zasilonych lamp i działającej drogi radiowej. Firmware nie kasuje
sieci ani bindingów podczas awarii i nie tworzy nowej sieci.

## Test odbiorczy na urządzeniu

1. Zbinduj np. CH2 do lampy/grupy. Po około sekundzie sprawdź `bound` i drogę
   `binding`. Zwarcie D1 do GND powinno sterować lampą bez `action` w Z2M.
2. Zatrzymaj Z2M: ten sam kanał ma od razu działać przez binding.
3. Pozostaw inny kanał bez bindingu. Przy działającym Z2M ma wysyłać swoje
   `action`; przy `push` jest to `chN_toggle`, przy `rocker` — `chN_on`/`chN_off`.
4. Uruchom Z2M ponownie: sposób działania obu kanałów nie powinien się zmienić.
5. Dodaj/usuń binding, odczekaj około sekundy i sprawdź nową drogę sterowania.
6. Powtórz dla wszystkich kanałów i grup, a także po restarcie XIAO przy
   odłączonym koordynatorze, lecz zasilonych lampach/routerach.
7. Zmień rocker/push, odczekaj 2 sekundy i zrestartuj XIAO. Odczytaj ustawienie,
   aby potwierdzić zapis. Sprawdź też stan lampy w HA przy sterowaniu bindingiem.

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
git clone https://github.com/direk/xiao-esp32-c6-4ch-zigbee-switch.git "$HOME/esp/xiao-4ch"
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

Przejdź do katalogu `firmware` we własnej kopii projektu. Dla przykładowej
lokalizacji `~/esp/xiao-4ch`:

```sh
cd "$HOME/esp/xiao-4ch/firmware"
idf.py set-target esp32c6
idf.py build
```

Jeżeli projekt jest w innym miejscu, zmień pierwszą ścieżkę. Nie trzeba ręcznie
instalować bibliotek Zigbee ani odtwarzać usuniętych plików. Component Manager
pobiera `esp-zigbee-lib` i `esp-zboss-lib` **1.6.9** na podstawie
`main/idf_component.yml` oraz `dependencies.lock`. Nie podmieniaj ich na SDK 2.x.

Automatycznie powstaną `managed_components/`, `sdkconfig` i `build/`.
Ustawienia początkowe pochodzą z `sdkconfig.defaults`: router Zigbee, ESP32-C6,
4 MB flash i konsola USB. Zewnętrzna antena jest wybierana w `main/main.c`
przez `EXTERNAL_ANTENNA=1`.

Gotowe pliki:

| Plik względem `firmware/` | Zastosowanie |
|---|---|
| `build/xiao_zigbee_4ch.bin` | Sama aplikacja; adres 0x10000 |
| `build/bootloader/bootloader.bin` | Bootloader; adres 0x0 |
| `build/partition_table/partition-table.bin` | Tablica partycji; adres 0x8000 |

Przy kolejnych zmianach kodu wystarczy `idf.py build`. Ponowne `set-target`
nie jest potrzebne i może zresetować wygenerowane ustawienia projektu.

### D. Wgranie i odczyt logu — osobny, opcjonalny krok

Sprawdź porty, będąc w aktywnym środowisku ESP-IDF:

```sh
python -m serial.tools.list_ports -v
```

Z katalogu `firmware` wgraj zbudowany projekt, podając prawdziwy port:

```sh
idf.py -p PORT flash monitor
```

Na macOS XIAO zwykle ma port `/dev/cu.usbmodem...`; na Windows np. `COM5`.
Zamknij inny program używający tego portu. Monitor kończy się przez `Ctrl+]`.
Pierwszy start powinien pokazać wersję aplikacji i
`Antenna: external (RF enable=0, select=1)`.

`idf.py flash` zapisuje osobne obrazy pod ich adresami; przy niezmienionym układzie
partycji nie kasuje ustawień ani bindingów. Jeśli poprzedni firmware używał innego
układu partycji i potrzebna jest czysta instalacja, przed wgrywaniem wykonaj
`idf.py -p PORT erase-flash` — **to usuwa wszystkie ustawienia i sieć**.

## Testy konwertera i logiki wejść — opcjonalnie

Nie są wymagane do zbudowania firmware’u. Uruchamiaj je z **głównego katalogu
projektu**, nie z `firmware`. Potrzebujesz Node.js **24.x** z npm; sprawdzono
Node.js 24.10.0. Test C wymaga kompilatora `cc` i sanitizerów (na macOS zapewniają
je narzędzia programistyczne Apple).

```sh
cd "$HOME/esp/xiao-4ch"
npm ci --ignore-scripts
npm test
npm run test:control
```

Testy konwertera używają przypiętych zależności z `package-lock.json` i rzeczywistego
kodowania/dekodowania ramek ZCL. Testy C sprawdzają wejścia i obsługę bindingów.

## Pakowanie wydania

Po udanym `idf.py build`, w aktywnym środowisku ESP-IDF i nadal w katalogu
`firmware`, uruchom:

```sh
python ../scripts/package_release.py --idf-path "$IDF_PATH"
```

Skrypt używa programu esptool zainstalowanego razem z ESP-IDF; nie wymaga
ręcznego kopiowania obrazów. Utworzy `dist/xiao-4ch-v1.2.0.zip` w głównym
katalogu projektu i usunie swoje pliki tymczasowe. ZIP zawiera obrazy do wgrania,
źródła, konwerter, dokumentację, licencje i sumy kontrolne.

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
