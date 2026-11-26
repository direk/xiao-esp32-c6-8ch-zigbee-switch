# Historia zmian

## 2.2.0 — 26 listopada 2026

- Standardowy klient Zigbee OTA na endpoint 1 i obsługa OTA w konwerterze 0.0.7.
- Dwa sloty aplikacji i rollback, z zachowaniem adresów NVS i danych Zigbee.
  Pierwsza migracja z 2.1.x wymaga zestawu USB, nie samej aplikacji.
- Osobne pliki .zigbee i identyfikatory obrazu dla obu anten, indeks z SHA512
  w repozytorium i wszystkie obrazy jako załączniki wydania.
- Walidacja strumienia, rozmiaru, wariantu, wersji i obrazu ESP-IDF; wybór
  nowego slotu dopiero po zakończeniu weryfikacji.
- INFO pokazuje antenę, slot, identyfikatory OTA i postęp odbioru.
- Testy kontenerów niezależnym parserem herdsman, podziałów bloków i błędów
  transferu; obie anteny nadal budowane przez standardowy skrypt i CI.

## 2.1.3 — 26 listopada 2026

- Naprawa odczytu tablicy bindingów zawierającej wpisy grupowe. Grupa zajmuje
  14 bajtów, a wpis IEEE 21; dekoder SDK 1.6.9 błędnie używał stałego kroku 21.
  W rezultacie kolejne endpointy mogły być oznaczane jako niezwiązane.
- Odczyt surowej odpowiedzi przez przypięte API ZBOSS i własny dekoder z
  kontrolą długości, trybu adresowania, źródła i spójności stron.
- Następna strona jest odczytywana po powrocie callbacku; niepełna lub błędna
  tablica nie zastępuje ostatniego poprawnego obrazu bindingów.
- Testy obu kolejności grup/urządzeń, ośmiu kanałów, paginacji i uciętych ramek.
- Poprawka nie wymaga kasowania sieci, ustawień ani istniejących bindingów.

## 2.1.2 — 26 listopada 2026

- Standardowy skrypt budowania i pakowanie zawsze kompilują warianty external
  i internal w osobnych katalogach z niezależną konfiguracją.
- Osobne application-external.bin / application-internal.bin oraz obrazy
  firmware-factory-external.bin / firmware-factory-internal.bin.
- Manifest podaje antenę, adres flash i sumę kontrolną każdego obrazu.
- Wybór anteny przez CMake, bez ręcznych zmian źródeł; konfiguracja kanałów,
  bindingi i układ partycji pozostają zgodne.

## 2.1.1 — 24 listopada 2026

- Naprawa rocker + toggle: każda stabilna zmiana styku wysyła genOnOff Toggle
  (0x02), zamiast ON/OFF wynikających z pozycji przełącznika. Dzięki temu
  zmiana światła przez inną automatykę nie rozjeżdża działania przełącznika.
- Rocker + momentary zachowuje closed=ON/open=OFF; switch_actions dotyczy push.
- Ta sama poprawka obowiązuje dla fizycznych wejść, UART i USB.
- Testy obu pozycji startowych, debounce, obu zboczy, ośmiu niezależnych wejść
  i dekodowania produkcyjnych ramek Toggle przez zigbee-herdsman.

## 2.1.0 — 23 listopada 2026

- Ten sam protokół komend przez UART D6/D7 i natywny USB-C Serial/JTAG.
  Osobne zadania, parsery, kolejki, odpowiedzi i sesje ID; wspólne kanały.
- `INFO` i `STATUS`: wersje, numer uruchomienia, stan sieci, bindingi,
  fizyczne/wirtualne wejścia, kolejki i liczniki diagnostyczne.
- Opcjonalne rosnące identyfikatory `@ID`, odtwarzanie 16 ostatnich odpowiedzi,
  odmowa ponownego wykonania starszych ID i wykrywanie konfliktów.
- Obsługa przepełnienia/błędów UART z odrzucaniem uszkodzonej linii do LF.
- `RESET`: usunięcie sieci Zigbee i bindingów, restart i automatyczne parowanie.
  Ustawienia kanałów pozostają zachowane; potwierdzenie poprzedza reset.
- `LOG 0` / `LOG 1` niezależnie na każdym porcie. Domyślnie wyłączone także
  po restarcie; kolejki logów nie blokują Zigbee. Linie logów mają prefiks #LOG.
- Wyciszone standardowe konsole/bootloader; normalne logi przez własny router.
- Wgranie i weryfikacja aplikacji na XIAO bez kasowania sieci/ustawień.
  Test USB: PING, INFO, STATUS, GET, LOG, powtórki ID i konflikt.
  RESET nie był wywoływany na urządzeniu; testy po przewodach D6/D7 pozostają
  do wykonania. Szczegóły w [walidacji](docs/VALIDATION.md).

Konwerter Z2M z 2.0.0 pozostaje zgodny. Do aktualizacji użyj application.bin
pod 0x10000 bez erase_flash. [Protokół UART/USB](docs/UART.md).

## 2.0.0 — 23 listopada 2026

- Osiem kanałów i 13 funkcji: on/off, momentary, gesty, regulacja jasności
  i temperatury, sceny oraz jasność bez On/Off.
- Binding według kanału i klastra oraz niezależne, zapisywane na urządzeniu
  binding priority. Brak bindingu daje action do Z2M.
- UART z PRESS/RELEASE, CLICK/DOUBLE/TRIPLE, HOLD, SET, PING i GET.
- CH7/CH8 na D8/GPIO19 i D9/GPIO20; D6/D7 przeznaczone dla UART.
- Stałe zasilanie, router Zigbee i antena zewnętrzna; brak heartbeat.

## 1.2.0 — 21 listopada 2026

- Pierwsze publiczne wydanie: cztery kanały, push/rocker, invert,
  sterowanie przez binding albo action i zewnętrzna antena.
