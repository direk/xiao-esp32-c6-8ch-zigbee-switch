# Walidacja wersji 2.1.1 — osiem kanałów

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

## Poprawka rocker/toggle 2.1.1

Testy C sprawdzają oba stany styku przy starcie (bez wysyłania komendy),
debounce i brak powtórek przy długim utrzymaniu pozycji, Toggle na obu zboczach,
niezależność od switch_actions i ośmiu stanów wejść. Ramka genOnOff ma command
ID 0x02 i pusty payload; zigbee-herdsman dekoduje cztery produkcyjne ramki
z obu kierunków jako toggle. Sprawdzono również SET/PRESS/RELEASE dla CH2
rocker/toggle oraz zachowanie starej semantyki ON/OFF w rocker/momentary.

24 listopada 2026: aplikację 2.1.1 wgrano pod 0x10000 i sprawdzono osobnym
verify_flash. INFO potwierdziło 2.1.1, a GET wszystkich ośmiu kanałów był
identyczny przed/po aktualizacji. STATUS potwierdził przywróconą sieć i te same
maski bindingów: CH1=2 (Level), CH2=1 (On/Off), pozostałe 0. CH2 zachował
rocker/toggle oraz binding_priority=ON. Nie wysyłano komend sterujących lampami
ani RESET; rzeczywiste przestawienie styku i zachowanie lampy wymagają próby
użytkownika. Test regresyjny ramek potwierdza Toggle na obu zboczach.

## Dwa warianty anteny 2.1.2 — 26 listopada 2026

- Pełna kompilacja external i internal od zera przez package_release.py w nowej
  instalacji ESP-IDF 5.4.2 / Zigbee SDK 1.6.9 zakończyła się powodzeniem.
- Osobne katalogi build i sdkconfig; compile_commands.json potwierdza
  XIAO_EXTERNAL_ANTENNA=1 dla external i =0 dla internal. GPIO3 pozostaje LOW,
  a ta stała określa poziom GPIO14.
- Obie aplikacje mają wersję 2.1.2, różne SHA256 i identyczną tablicę partycji.
- Sprawdzono obrazy aplikacji i fabryczne dla obu anten, manifest, sumy
  kontrolne załączników i każdego pliku wewnątrz ZIP oraz zgodność źródeł.
- Pakowanie ponownie uruchamia obie kompilacje; GitHub Actions używa tego
  samego skryptu. Brak poprawnej kompilacji wariantu przerywa proces.
- Nie wgrywano wersji 2.1.2 na XIAO i nie testowano obu anten radiowo.

## Bindingi grupowe i kolejne endpointy 2.1.3 — 26 listopada 2026

Na podłączonym XIAO z anteną wewnętrzną odtworzono błąd: tablica miała dwa
wpisy, ale firmware 2.1.2 rozpoznawał tylko CH1. Log pokazał poprawny pierwszy
rekord grupowy i losowe pola kolejnego rekordu. Analiza kodu maszynowego
przypiętej biblioteki 1.6.9 potwierdziła stały krok 21 bajtów, także dla
14-bajtowych rekordów grupowych.

- Nowy dekoder otrzymuje surową odpowiedź Mgmt_Bind_rsp przez API ZBOSS,
  rozróżnia rekordy 14/21 bajtów i odrzuca uszkodzone lub niepełne strony.
- Testy z ASan/UBSan: grupa/grupa, grupa/IEEE, IEEE/grupa i IEEE/IEEE;
  oba porządki ośmiu endpointów, cztery klastry, strony, obce źródła,
  wykluczenie koordynatora, maksymalna tablica, każde ucięcie odpowiedzi
  oraz 10 000 losowych buforów. Wynik PASS; testy logiki wejść także PASS.
- Skompilowano oba warianty anteny w ESP-IDF 5.4.2 / Zigbee SDK 1.6.9.
- Przed diagnostyką zachowano lokalną kopię całej pamięci i ośmiu konfiguracji.
  Na urządzeniu zapisano wyłącznie aplikację internal pod 0x10000.
- verify_flash potwierdziło zgodność obrazu 2.1.3. Pięć kolejnych STATUS
  pokazało joined=true, bindings_known=true i maski [1,1,0,0,0,0,0,0].
  Wszystkie osiem odpowiedzi GET było identyczne przed/po aktualizacji.
- Użytkownik potwierdził fizycznym testem, że CH1 i CH2 sterują swoimi lampami.
  Nie kasowano ani nie tworzono ponownie bindingów; logi na USB pozostały OFF.
- Test sprzętowy dotyczy wariantu internal i dwóch kanałów. External oraz
  pozostałe kombinacje zweryfikowano kompilacją i testami dekodera.

## Zigbee OTA 2.2.0 — 26 listopada 2026

- Skompilowano external i internal z klientem OTA, dwoma slotami oraz
  CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y w ESP-IDF 5.4.2 / Zigbee SDK 1.6.9.
- Sześć zestawów testów C z ASan/UBSan przeszło: control, bindings, serial,
  transport, session i ota. Odbiornik OTA sprawdzono dla bloków 1–150 bajtów,
  każdego ucięcia strumienia, nieznanego tagu, przepełnienia slotu, błędu
  zapisu i niezgodnej wersji aplikacji.
- Testy Node obejmują rzeczywisty parser herdsman dla obu .zigbee, SHA512,
  zawartość aplikacji, typy anten, metadane indeksu i obsługę OTA w konwerterze.
  Sprawdzono zarówno kontenery testowe, jak i gotowe obrazy z kompilacji.
- Na wyraźnie wskazanym XIAO z anteną zewnętrzną wykonano lokalną kopię 4 MB
  i snapshot konfiguracji. Zapisano oraz zweryfikowano bootloader, tablicę
  partycji, aplikację 2.2.0-rc1 i początkowe otadata; bez erase_flash i bez
  zapisu w obszarach NVS/zb_storage/zb_fct.
- USB potwierdziło 2.2.0-rc1, external, OTA enabled=true, image_type=0xC601,
  file_version=0x0201FFFF oraz slot ota_0. Pięć STATUS potwierdziło istniejącą
  sieć i bindingi. Wszystkie osiem GET pozostało identyczne przed/po migracji.
  Weryfikacja trwała ponad 10 sekund; aplikacja pozostała aktywna, logi OFF.
- Wersja startowa celowo reklamuje niższy numer OTA, aby użytkownik mógł
  przetestować transfer do produkcyjnego obrazu 2.2.0. Dokładne obrazy
  produkcyjne nie mają jeszcze potwierdzonego rozruchu na sprzęcie.
- Pełny transfer .zigbee przez Zigbee2MQTT, start z ota_1 po transferze i
  fizyczny test rollbacku nie zostały jeszcze wykonane. Użytkownik wybrał
  samodzielny test OTA po podmianie konwertera. Nie należy traktować testów
  formatu i migracji USB jako potwierdzenia transferu radiowego.


## Niewydane: switch_actions=on_off

- Test control.c symuluje grupę sześciu lamp z mieszanym stanem i dekoduje
  rzeczywiste ID komend: 1, 0, 1, 0. Każda komenda ustawia całą grupę zgodnie.
- Testy sprawdzają debounce, brak powtórzeń przy trzymaniu, osiem niezależnych
  sekwencji, pierwszy ON po starcie, zachowanie fazy przy rebase i NVS maskę
  zgodną z poprzednią strukturą konfiguracji.
- Parser UART współdzieli sekwencję z fizycznym wejściem; ich nakładanie nie
  generuje podwójnego naciśnięcia. GET zwraca nazwę on_off.
- Konwerter koduje i odczytuje wartość 3 atrybutu 0x06 na wszystkich ośmiu
  endpointach. zigbee-herdsman dekoduje ramki control.c jako on/off, bez Toggle.
- Wszystkie sześć zestawów testów C przeszło z ASan/UBSan; Node: 18 testów
  przeszło, test dodatkowego katalogu dystrybucji pominięty (brak nowego wydania).
- Brak testu nowej funkcji na fizycznych lampach; urządzenie nie było resetowane
  ani programowane podczas trwającego OTA 2.2.0.
- Oba warianty anteny skompilowane poprawnie od zera: ESP-IDF 5.4.2, Zigbee/ZBOSS 1.6.9.


## Lokalna wersja testowa 2.2.1: scene_cycle

- 14 trybów i 8 osobnych list: parser odrzuca duplikaty, uszkodzone ciągi,
  nieprawidłowe ID, ponad 16 scen i nieprawidłowy znacznik długości ZCL.
- control.c: rotacja 0/1/3, wrap, debouncing, rebase, restart, brak powtórzeń
  podczas hold/release, pusta lista i grupa 0, odrębne grupy ośmiu kanałów.
- Rzeczywiste ramki ZCL z control.c są dekodowane przez zigbee-herdsman jako
  Recall Scene do grupy 321 ze scenami dokładnie 3→0→1→3.
- Test UART sprawdza wspólny indeks przy wejściu fizycznym i wirtualnym,
  nakładające się naciśnięcia oraz HOLD bez dodatkowego wywołania sceny.
- Test GET sprawdza 128 kompletnych JSON-ów (14 trybów push i 2 rocker,
  8 kanałów) z maksymalną listą 16 trzycyfrowych ID.
- NVS: testy kodeka zachowują 16-bajtowy kanał config2, osobne listy i maskę
  trybu; przy starym config2 listy są puste. Layout 130-bajtowego blobu jest
  sprawdzany przez kompilator.
- Konwerter: tekstowe exposes i rzeczywiste kodowanie char string 0x42 na
  wszystkich kanałach; normalizacja spacji, zapis/odczyt, czyszczenie i walidacja.
- Wszystkie sześć zestawów C z ASan/UBSan oraz komplet testów Node, w tym
  dekodowanie obu rzeczywistych obrazów OTA, sprawdzone lokalnie.
- Warianty internal i external skompilowane przez ESP-IDF 5.4.2, SDK 1.6.9.
  Wersja aplikacji 2.2.1, wersja OTA 0x02020100, różne imageType anten.
- Brak publikacji i programowania przez USB. Test radiowy OTA oraz reakcja
  rzeczywistych lamp na scene_cycle pozostają do wykonania przez użytkownika.


## Lokalna wersja testowa 2.2.2: Identify i Diagnostics

- Warianty external/internal: ESP-IDF 5.4.2 i Zigbee SDK 1.6.9. Numer OTA
  0x02020200, oddzielne typy obrazów C601/C600, brak programowania przez USB.
- Test C z ASan/UBSan obejmuje GPIO active LOW, dokładne granice 250 ms,
  przerwanie Identify, ponowny start od zapalonej LED, nasycanie liczników,
  odróżnienie pierwszego uruchomienia od restartu, zerowanie RAM na start
  oraz inkrementację wyłącznie lokalnie odrzuconych żądań APS.
- Test konwertera koduje rzeczywistą komendę genIdentify Identify i standardowy
  raport haDiagnostic NumberOfResets przez zigbee-herdsman, sprawdza read/get,
  brak sufiksów kanałów, filtr EP1, reset_reason oraz wartości graniczne uint32.
- Siedem zestawów C z ASan/UBSan i 22 testy Node obejmujące również oba
  rzeczywiste pliki OTA i indeksy są walidowane lokalnie.
- Miganie LED, faktyczne przetrwanie licznika przez fizyczny restart oraz
  odczyt nowego klastra w prawdziwym Z2M pozostają testami sprzętowymi użytkownika.
  Testy na komputerze nie potwierdzają odbioru radiowego ani wykonania przez lampy.
- Nie dodano globalnego przechwytywania potwierdzeń APS, dodatkowego heartbeat,
  zadań blokujących ani zapisu NVS przy każdej transmisji.


## Wersja 2.3.0: 24 endpointy

- 24 niezależne plannery, 8 pinów; test integruje input_router, control i
  serial_input z debounce, opóźnionym single, rozpoznaniem double i hold
  oraz drugim naciśnięciem dokładnie na granicy okna wielokliku.
- Przypadek D1 -> EP2/EP10/EP18: on_off, kolejność scen 3/0/1 i naprzemienne
  ściemnianie ze Stop. Brak pierwszego kliknięcia podczas double/hold.
- Boot/rebase z wciśniętym wejściem nie emituje gestu. Test obejmuje współdzielone
  ustawienia, odrzucenie legacy+decoded/rocker, mapowanie 24 celów jednego pinu,
  oddzielne fazy ON/OFF oraz odczyt mapy GET fizycznego wejścia.
- Dekoder raw Mgmt_Bind: pełne 96 rekordów group/unicast, wszystkie cztery
  klastry, EP1..24, obie kolejności i maski 0xffffff. Zachowana regresja stride
  14/21 bajtów i walidacja paginacji/uszkodzonych danych.
- Parser: CLICK/PRESS/RELEASE/DOUBLE/HOLD/SET na dodatkowych EP, granice EP0/25,
  stały zakres bez prefiksu 1..8, RELEASE ALL i watchdog. Sesje rozróżniają
  CLICK 2 od CLICK EP2 przy tym samym @ID. STATUS EP24 i GET EP24 są odczytywane.
- Test produkcyjnego transportu wysyła PRESS EP24 przez UART i RELEASE EP24
  przez USB; GET/STATUS EP24 wracają właściwym portem. Zachowane granice kolejek
  i niezależność logów/deduplikacji obu portów.
- Z2M: prawdziwe ZCL uint8/bool/uint16 dla map, pinów i priorytetów, source D9,
  endpointy 1..24 i 456 akcji; kontrola braku D6/D7 w wyborze źródła.
- Osiem zestawów C z ASan/UBSan i 23 testy Node (wraz z oboma rzeczywistymi
  obrazami OTA) są sprawdzane lokalnie. Obie anteny kompilowane przez IDF 5.4.2.
- Pełna migracja 24 endpointów na żywym stosie Zigbee, nowy Interview Z2M,
  rzeczywisty zapis NVS przez zanik zasilania i sprzętowe bindingi 24 EP są
  testami użytkownika. Nie wykonano flashowania ani transferu OTA przez agenta.


## Lokalna poprawka 2.3.1

- Ustalona ścieżka SDK: esp_zb_zcl_set_attribute_val -> zb_zcl_set_attr_val ->
  zb_zcl_set_attr_val_manuf_internal -> zb_zcl_write_attr_hook. Wariant check=false
  nie wyłącza hooka. W 2.3.0 callback synchronizował kolejne RW atrybuty tym samym
  setterem, co dawało rekurencję i brak odpowiedzi na Write Attributes.
- Test config_write_guard symuluje 80 aktualizacji aliasów i ponowne wejścia
  SDK; ustawienie zmienia się raz, candidate nie jest zastępowany przez nested
  callback, następne zewnętrzne ustawienie działa. Test z ASan/UBSan dodany do CI.
- Obie anteny kompilowane lokalnie. Testy nowej konfiguracji po Zigbee na
  fizycznym urządzeniu wymagają testu użytkownika; nie podłączono urządzenia
  i nie wykonano flashowania ani publikacji tej poprawki.
- Dodano również test rzeczywistych funkcji config_value/check_value/apply_attribute/write_attr
  wyciętych z produkcyjnego main.c i skompilowanych z backendem imitującym hooki SDK.
  Sprawdza CH1 legacy→single, Stop z raportem w trakcie zapisu, następne ustawienie,
  priorytet bez przerwania hold oraz prawidłowe odrzucanie konfliktów i aliasy pinu.
- Jednorazowo uruchomiono ten sam harness z funkcjami odczytanymi z tagu v2.3.0:
  zatrzymał się na asercji drugiego zagnieżdżonego wejścia SDK. Wersja poprawiona
  przechodzi test i zwraca odpowiedź. Test nie wysyła komend do prawdziwego urządzenia.

### Konwerter 0.1.1 — widoczność switch_actions (lokalnie)

- `npm test`: 26 testów zaliczonych, 1 test opublikowanych artefaktów pominięty.
- Dynamiczna definicja przygotowana przez przypięty zigbee-herdsman-converters:
  wszystkie 14 typów × 24 endpointy, push/rocker, ukrywanie i przywracanie pola.
- Odtworzone aktualizowanie cache przed konwerterem (jak w herdsman), częściowe
  odpowiedzi, zachowany cache po restarcie i brak powiadomień dla powtórzeń.
- Zmiana trybu pinu odczytuje aliasy wszystkich endpointów; zmiana input_source
  odczytuje jego efektywny input_mode. Pozostałe ustawienia i zapis switch_actions
  pozostają dostępne przez protokół, nawet gdy pole jest ukryte w panelu.
- Sprawdzony interfejs `deviceExposesChanged` w kodzie Z2M 2.14.3.
  Zachowanie panelu wymaga jeszcze sprawdzenia w działającym Z2M użytkownika.


### Konwerter 0.1.2 — warunkowe parametry endpointów (lokalnie)

- `npm test`: 28 testów zaliczonych, 1 test opublikowanych artefaktów pominięty.
- Sprawdzona pełna tabela 14 typów × 24 endpointy: switch_actions, min/max level,
  min/max temperatury barwowej, transition_time, scene_id, group_id i scene_list.
- Niezależność endpointów i stała dostępność źródła, gestu, trybu, invert,
  switch_type oraz binding_priority. Brak odświeżania dla niezmienionego
  zestawu pól; odświeżenie także między brightness a scene, gdy switch_actions
  pozostaje ukryte.
- Test zachowania wartości przy ukryciu/przywróceniu oraz nieudanego zapisu typu,
  który nie zmienia widoczności przed potwierdzeniem przez urządzenie.
- Firmware i pliki OTA bez zmian; niczego nie opublikowano ani nie wgrano.
  Sprawdzenie w panelu działającego Z2M pozostaje do wykonania.


### Przygotowanie wydania 2.3.1

- Lokalne testy z rzeczywistymi obrazami OTA: 29/29, bez pominięć;
  9 natywnych zestawów testów logiki firmware’u zaliczonych.
- Kompilacja obu anten i weryfikacja paczki: zgodność źródeł/dokumentacji,
  konwerter 0.1.2, wewnętrzne/zewnętrzne sumy SHA256 i niezależne dekodowanie OTA.
- Pierwsze uruchomienie CI ujawniło różnicę GCC/Clang w ostrzeżeniu sign-compare
  dla indeksu endpointu w callbacku. Porównanie ma teraz jawny typ unsigned;
  nie zmienia to zakresu ani zachowania, bo endpoint jest wcześniej walidowany.
- Pełny test sprzętowy funkcji i panelu dynamicznych pól pozostaje odrębny od
  testów programowych; manifest nie deklaruje go jako wykonanego.
