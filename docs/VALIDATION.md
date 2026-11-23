# Walidacja wersji 2.0.0 — osiem kanałów

## Kompilacja i testy na komputerze

- ESP-IDF 5.4.2, Zigbee/ZBOSS 1.6.9, ESP32-C6. Aplikacja ma
  638 768 bajtów (0x9bf30), mieści się w partycji 1,5 MB.
- Trzynaście testów Node z zigbee-herdsman-converters 26.135.0: osiem
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
`npm run test:transport`.

## Wgranie i uruchomienie na XIAO — 23 listopada 2026

- Najnowszą aplikację 2.0.0 wgrano przez USB na XIAO ESP32-C6FH4 rev v0.2.
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
UART/GET po przewodach z urządzeniem zewnętrznym oraz zmiana binding priority
w Z2M i potwierdzenie jej zachowania po fizycznym restarcie.

Konwerter 8CH wymaga podmiany w Z2M. Po przejściu z wersji 4CH wykonaj
ponowne interview/reconfigure, aby Z2M poznał endpointy 5–8. Zigbee OTA
nie jest częścią tej wersji; aktualizacje wykonuje się przez USB.
