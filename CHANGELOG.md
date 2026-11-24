# Historia zmian

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
