# Historia zmian

## 2.3.1 — 27 listopada 2026

- Konwerter 0.1.2: dynamiczna widoczność min/max level, temperatury barwowej,
  transition_time, scene_id, group_id i scene_list według switch_type, osobno
  dla 24 endpointów. Zachowanie ukrytych wartości i stała dostępność ustawień
  źródła/gestu/typu/binding priority. Ukrywanie realizuje sam konwerter.

- Naprawa rekurencji custom write hook przy zapisie input_gesture i innych
  ustawień. ZBOSS wywołuje hook również przy set_attribute_val(check=false),
  więc synchronizacja aliasów ponownie uruchamiała zapis konfiguracji.
- Guard całej operacji blokuje zagnieżdżone skutki uboczne i chroni candidate
  przed zastąpieniem przez wewnętrzne sprawdzanie wartości. SDK nadal aktualizuje
  własne dane atrybutów; następny zewnętrzny zapis jest obsługiwany normalnie.
- Test odtwarza 80 aktualizacji aliasów z ponownym wywołaniem hooka, sprawdza
  pojedyncze zastosowanie ustawienia oraz zwolnienie guardu dla następnego zapisu.
- Bez zmiany NVS, protokołu ani endpointów.
- Konwerter 0.1.1: dynamiczne `switch_actions` — pole widoczne wyłącznie dla
  push + toggle/momentary, niezależnie dla 24 endpointów. Zmiana widoczności po
  odczycie konfiguracji, bez kasowania ustawienia. Odczyt aliasów input_mode
  po zmianie trybu fizycznego pinu oraz efektywnego trybu po zmianie input_source.

- Uzupełniona dokumentacja UART/USB: @ID, ponawianie, cache odpowiedzi i błędów,
  restarty XIAO/kontrolera oraz niezależne sesje portów.
- Obrazy aplikacji, obrazy fabryczne USB i aktualizacje Zigbee OTA dla obu anten,
  konwerter 0.1.2 oraz komplet źródeł i dokumentacji w paczce wydania.

## 2.3.0 — 27 listopada 2026

- 24 endpointy źródłowe, niezależne konfiguracje/binding priority/sceny,
  32-bitowe maski bindingów i tabela 96 wpisów.
- input_source: none lub D0–D5/D8/D9; input_gesture: legacy/single/double/hold.
  Osiem fizycznych klasyfikatorów rozdziela gesty przed aktywacją endpointów.
- Wspólne ustawienia elektryczne pinów i czasy hold/multi-click w Z2M.
  Odrzucanie mieszanych legacy/decoded i niezgodnych konfiguracji rockera.
- Protokół UART/USB 3: adresy EP1..EP24, GET EPn, STATUS EPn, wszystkie
  dotychczasowe komendy, wspólny RELEASE ALL i rozróżnianie celu w deduplikacji.
- Stare numery 1..8 symulują wejścia fizyczne przez mapę. Prefiks EP omija ją.
- NVS config3 przechowuje pełne mapy/parametry. Migracja zachowuje EP1..8
  w trybie legacy, EP9..24 nie mają przypisanego pinu. OTA/diagnostyka nadal EP1.
- Konwerter 0.1.0 udostępnia ch1..ch24 i 456 akcji; wymaga Interview.
- Obie anteny: obrazy USB i Zigbee OTA 2.3.0 jako załączniki wydania.
- Zawiera także funkcje niepublikowanych wersji lokalnych 2.2.1 i 2.2.2.

## 2.2.2 — lokalna wersja testowa, nieopublikowana

- Identify 0x0003: nieblokujące miganie wspólnej LED GPIO15 (active LOW),
  250 ms na stan, czas SDK IdentifyTime, automatyczne zatrzymanie.
- Diagnostics 0x0B05 na EP1: trwały NumberOfResets, zapis raz na rozruch
  w osobnej przestrzeni NVS. Pierwszy start 0; sieciowy RESET zachowuje licznik.
- Dodatkowe pola diagnostyczne 0xFC00: uptime, reset_reason i liczba lokalnie
  odrzuconych żądań bezpośredniego sterowania APS od rozruchu.
- Konwerter 0.0.9: globalne Identify oraz pola diagnostyczne bez sufiksów kanałów.
  Raportowanie po dołączeniu/co 60 s; odczyt na żądanie. Wymaga Interview.
- Zachowuje scene_cycle i on_off. Nowy numer pozwala testować OTA z 2.2.1.

## 2.2.1 — lokalna wersja testowa, nieopublikowana

- Nowy `switch_type=scene_cycle` i tekstowe `scene_list` osobno dla CH1–CH8.
  Do 16 różnych ID (0–255), kolejność użytkownika, grupowe genScenes Recall Scene.
- Trwały zapis list i trybu z zachowaniem layoutu config2 i ustawień 2.2.0.
  Własny indeks RAM kanału, reset po zapisie listy/grupy/trybu lub restarcie.
- Z2M: pola tekstowe, walidacja i normalizacja; UART/USB: GET z listą scen.
- Obie anteny mają lokalne binarki i obrazy OTA 2.2.1, konwerter 0.0.8.


- `switch_actions=on_off` dla każdego kanału: jawne ON/OFF na przemian dla
  push/toggle; dla push/momentary sekwencja zmienia się na naciśnięciu i puszczeniu.
- Konwerter 0.0.8 udostępnia czwartą opcję; GET zwraca jej nazwę.
- Opcja zapisywana w NVS z zachowaniem zgodności config2 przy rollbacku;
  kolejność w RAM, niezależna dla kanałów, startuje od ON po restarcie lub
  zapisie switch_actions. Pozostałe ustawienia nie resetują kolejności.

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
