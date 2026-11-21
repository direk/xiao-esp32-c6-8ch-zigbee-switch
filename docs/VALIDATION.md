# Walidacja wersji 1.2.0

- Firmware skompilowany dla ESP32-C6 z ESP-IDF 5.4.2,
  esp-zigbee-lib 1.6.9 i esp-zboss-lib 1.6.9.
- Aplikacja: 587 520 bajtów, mieści się w partycji 1,5 MB.
- Osiem testów konwertera przeszło z zigbee-herdsman-converters 26.135.0:
  cztery kanały, odczyt/zapis ustawień, normalizacja nazw endpointów,
  kodowanie ramek ZCL, deduplikacja i diagnostyka bindingów.
- Testy C z AddressSanitizer i UndefinedBehaviorSanitizer przeszły:
  wybór drogi, tablica bindingów, debounce, rocker/push i izolacja kanałów.
- Wgranie na XIAO ESP32-C6FH4 rev v0.2 zakończone weryfikacją zapisu.
  Potwierdzono start 1.2.0, przywrócenie sieci i bindingów oraz wybór zewnętrznej
  anteny: GPIO3=LOW, GPIO14=HIGH. Podczas obserwacji startu nie było restartów
  ani błędów inicjalizacji.

Testy programowe i sprawdzenie startu nie zastępują testu odbiorczego z lampami,
wyłączeniem koordynatora i pomiarem zasięgu. Kroki testu są w README.

## Odtworzenie po czyszczeniu projektu

21 listopada 2026 sprawdzono pełną kompilację na macOS w osobnej kopii źródeł,
bez `build/`, `managed_components/` i `sdkconfig`, z pustym, oddzielnym cache
Component Managera. Użyto zainstalowanego toolchaina ESP-IDF 5.4.2;
nie instalowano go ponownie. Polecenie `idf.py set-target esp32c6 build`
odtworzyło komponenty Zigbee/ZBOSS 1.6.9 i zakończyło się poprawnie.
Aplikacja nadal ma 587 520 bajtów; wygenerowano również bootloader i tablicę partycji.
Potwierdzono ustawienia domyślne: ESP32-C6, 4 MB, router Zigbee i konsola USB.

W tej samej czystej kopii wykonano `npm ci --ignore-scripts`, osiem testów
konwertera i testy C. Sprawdzono również pakowanie wydania z katalogu `firmware`
oraz sumy kontrolne obrazów i pozostałych plików ZIP. Projekt źródłowy pozostał
bez plików kompilacji i pobranych bibliotek. Nie wgrywano ponownie firmware’u.
