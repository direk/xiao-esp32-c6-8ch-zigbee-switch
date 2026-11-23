# Walidacja wersji 2.1.0 — osiem kanałów

## Kompilacja i testy na komputerze

- ESP-IDF 5.4.2, Zigbee/ZBOSS 1.6.9, ESP32-C6. Aplikacja ma
646 560 bajtów (0x9dda0), mieści się w partycji 1,5 MB.
- Czternaście testów Node z zigbee-herdsman-converters 26.135.0: osiem
  endpointów, 13 funkcji, 152 pary kanał/akcja, kodowanie uint8/uint16/BOOL,
  normalizacja nazw w Z2M, wiele ustawień w żądaniu, brak replay po odczycie,
  deduplikacja i diagnostyka. Binding priority zapisuje się i odczytuje
  niezależnie na wszystkich ośmiu endpointach.
- Testy C z AddressSanitizer i UndefinedBehaviorSanitizer: debounce,
  rocker, momentary, single/double/triple, hold/release, zmiana kierunku,
  anulowanie, brak hold przy starcie, zakresy ustawień i niezależność wejść.
  Sprawdzono cztery klastry, filtrowanie/paginację bindingów oraz macierz
  routingu dla bindingu, grupy, report-only i obu wartości binding priority.
- Produkcyjny control.c generuje ramki z rzeczywistych sekwencji przycisków.
  zigbee-herdsman dekoduje standardowe polecenia Level/Color/Scenes:
  ExecuteIfOff, warianty komend, kroki, min/max, sceny i grupy.
- Parser i zegar UART: osiem kanałów, CLICK/DOUBLE/TRIPLE/HOLD, rocker,
  łączenie z fizycznym wejściem, Stop po limicie 10 s, odnawianie limitu,
  anulowanie, fragmentacja linii, błędne argumenty i za długie linie.
- Produkcyjny serial_transport.c z zastępczymi kolejkami/sterownikiem:
  brak I/O UART w kontekście Zigbee, limit jednej komendy na cykl,
  8-elementowe kolejki, ERR QUEUE i odkładanie wykonania przy pełnej kolejce
  odpowiedzi bez utraty ACK.
- GET: 120 odpowiedzi JSON dla 13 trybów push i 2 trybów rocker na ośmiu
  kanałach. Test parsuje JSON i sprawdza komplet pól przy maksymalnych
  wartościach, nazwy funkcji względem Z2M, binding priority, commands i gestures.
  GET nie zmienia sekwencji, działa bez gotowości sieci i przy BUSY.
  Sprawdzono mały bufor, błędne argumenty, odczyt ustawień zmienionych po
  zakolejkowaniu i przesłanie pełnego JSON przez zadanie UART.

Uruchomienie: `npm test`, `npm run test:control`, `npm run test:serial`,
`npm run test:transport`, `npm run test:session`.

## Poprzednie wydanie 2.0.0 — test sprzętowy 23 listopada 2026

- Aplikację 2.0.0 wgrano przez USB na XIAO ESP32-C6FH4 rev v0.2.
  Przed zapisem wykonano lokalną kopię 4 MB pamięci poza repozytorium.
- Zapisano wyłącznie aplikację pod adresem 0x10000, bez erase_flash.
  esptool potwierdził zapis, a osobne verify_flash potwierdziło zgodność obrazu.
- Po restarcie log potwierdził projekt xiao_zigbee_8ch, wersję 2.0.0,
  wejścia GPIO0/1/2/21/22/23/19/20 oraz antenę zewnętrzną
  (GPIO3=0, GPIO14=1). CH7/CH8 są na D8/D9; D6/D7 obsługują UART.
- Urządzenie przywróciło dotychczasową sieć Zigbee i odczytało binding CH1;
  kanały CH2–CH8 były unbound. Dane sieci i konfiguracji nie były kasowane.
- W ciągu 20 sekund obserwacji startu nie wystąpił panic ani dodatkowy restart.

## Zakres pozostałych prób sprzętowych

Testy hostowe nie emulują lamp, przerwań ani rzeczywistego harmonogramu RTOS.
Pozostają próby przycisków CH5–CH8 na nowym mapowaniu, gestów, regulacji,
Stop, ExecuteIfOff i scen na docelowych lampach. Do sprawdzenia pozostają
UART D6/D7 po przewodach z urządzeniem zewnętrznym oraz zmiana binding priority
w Z2M i potwierdzenie jej zachowania po fizycznym restarcie.

Konwerter 8CH wymaga podmiany w Z2M. Po przejściu z wersji 4CH wykonaj
ponowne interview/reconfigure, aby Z2M poznał endpointy 5–8. Zigbee OTA
nie jest częścią tej wersji; aktualizacje wykonuje się przez USB.

## Nowości 2.1.0 — protokół UART v2

- INFO/STATUS: JSON sprawdzony parserem Node przy maksymalnych licznikach,
  przy dołączeniu i bez sieci, dla wszystkich kanałów, z aktywnymi wejściami
  fizycznymi/wirtualnymi i sekwencjami. Wersja firmware'u pochodzi z metadanych
  aplikacji ESP-IDF, protokół ma numer 2.
- Sesja ID: powtórzenie i konflikt, pamięć 16 pełnych odpowiedzi, odmowa ID
  starszych niż pamięć zamiast ponownego wykonania, stare migawki GET,
  zapamiętane odmowy, granice uint32 i brak zapisu sesji do NVS.
- Produkcyjny transport: zdarzenia FRAME/FIFO/BUFFER/PARITY/BREAK,
  przepełnienie kolejki zdarzeń, błąd przed i podczas odczytu RX, odrzucenie
  uszkodzonego fragmentu RESET, synchronizacja po LF i liczniki STATUS.
- RESET: atrapa callbacka, jedna akceptacja przy powtórce ID, błąd zapisu,
  blokada nowych operacji podczas reset_pending, ACK dopiero z zadania UART,
  minimum 200 ms i fallback 2 s, także ochrona przed ujemną różnicą czasu.
  Test nie kasuje rzeczywistej sieci. Wywołanie SDK w firmware kompiluje się
  przeciwko nagłówkom Zigbee 1.6.9.

2.1.0 uruchomiono i sprawdzono przez USB zgodnie z opisem poniżej. RESET,
ponowne parowanie oraz zdarzenia UART D6/D7 po przewodach wymagają świadomego
testu sprzętowego. RESET nie był wywoływany na urządzeniu.

## Dwa porty i logi

Hostowy test produkcyjnego transportu obejmuje równoległe USB/UART, osobne
parsowanie częściowych linii, powrót odpowiedzi do właściwego portu, niezależne
ID, round-robin i pełną kolejkę USB przy działającym UART. Atrapy sterowników
sprawdzają brak fizycznego I/O w kontekście Zigbee. Sprawdzono wyłączone logi
na obu portach po starcie, włączanie osobno i razem, wyłączanie bez wpływu na
drugi port, format #LOG i spłaszczanie linii, odrzucanie starych komunikatów,
limit kolejki logów, log_dropped i priorytet gotowych odpowiedzi. Powtórzenie LOG
z tym samym ID nie zmienia ponownie przełącznika. RESET przez USB opróżnia TX
w zadaniu USB; przerwana odpowiedź USB jest zakończona LF przed ponowną wysyłką.

Kompilacja regeneruje sdkconfig z defaults: brak konsoli głównej i dodatkowej,
logi domyślnie NONE z dostępnym poziomem INFO, bootloader NONE. Normalne logi
aplikacji są kierowane przez callback do osobnych kolejek portów.

## Wgranie 2.1.0 i test USB — 23 listopada 2026

- Przed aktualizacją wykonano lokalną kopię 4 MB pamięci poza repozytorium.
  Zapisano samą aplikację 2.1.0 pod 0x10000, bez kasowania konfiguracji/sieci.
  Osobne verify_flash potwierdziło zgodność obrazu.
- USB-C: PING i INFO potwierdziły działający protokół, firmware 2.1.0,
  port usb oraz logs_enabled=false po starcie.
- STATUS potwierdziło przywrócenie istniejącej sieci, zakończony odczyt bindingów,
  osiem kanałów i binding Level Control CH1 (maska 2); pozostałe maski wynosiły 0.
- GET 1 i GET 8 zwróciły konfigurację, w tym binding_priority.
- Powtórka @1 INFO zwróciła identyczną odpowiedź, @1 STATUS dało ID_CONFLICT.
- @2 LOG 1 i @3 LOG 0 zmieniały logs_enabled odczytywane przez INFO.
  Ponowienie @2 LOG 1 po wyłączeniu zwróciło wcześniejszy ACK bez ponownego
  włączenia logów. Po teście pozostawiono LOG 0.
- Kolejne STATUS przez 10 sekund potwierdzały ten sam boot_id i połączenie
  Zigbee. Testy nie wysyłały CLICK/PRESS/HOLD ani RESET i nie sterowały lampami.

Rzeczywiste wyjście #LOG przy zdarzeniu aplikacji, UART D6/D7 oraz RESET
pozostają do testu sprzętowego; izolację portów i formatowanie logów sprawdzają
opisane powyżej testy produkcyjnego kodu z atrapami sterowników.
