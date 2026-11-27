# Komendy przez UART i USB-C — protokół v2, firmware 2.1.1

Firmware obsługuje osiem wirtualnych przycisków. Korzystają z tych samych
ustawień kanałów, logiki gestów, bindingów i `binding_priority` co fizyczne wejścia.
Konwerter Z2M nie wymaga dodatkowych zmian z powodu portów szeregowych.
Ten sam protokół działa równocześnie na UART D6/D7 i natywnym USB-C.

## Połączenie

UART1: **115200 baud, 8N1, bez kontroli przepływu**, poziomy logiczne **3,3 V**.

| Urządzenie zewnętrzne | XIAO ESP32-C6 |
|---|---|
| TX | D7 / RX / GPIO17 |
| RX | D6 / TX / GPIO16 |
| GND | GND |

Prawdziwy RS-232 wymaga transceivera poziomów, np. MAX3232 zasilanego 3,3 V.
Nie podłączaj sygnałów RS-232 ani UART 5 V bezpośrednio do GPIO.
USB-C obsługuje te same komendy przez natywny USB Serial/JTAG. D6/D7 służą do komend,
CH7/CH8 są na D8/D9. Przed aktualizacją usuń stare przyciski z D6/D7.

## USB-C i równoczesna praca portów

Podłącz XIAO kablem USB-C z transmisją danych i otwórz jego port szeregowy,
np. `/dev/cu.usbmodem...` na macOS lub `COM...` w Windows. Użyj LF/CRLF.
Możesz ustawić 115200/8N1 w terminalu, ale dla natywnego USB prędkość baud
nie określa szybkości transmisji. UART D6/D7 nadal wymaga 115200/8N1 i 3,3 V.

Oba porty obsługują wszystkie komendy z tego dokumentu. Odpowiedź wraca
wyłącznie na port, z którego przyszło żądanie. Każdy port ma własny parser,
8-elementowe kolejki komend i odpowiedzi, liczniki, pamięć 16 żądań z ID
oraz osobne zadanie FreeRTOS. Brak odbiorcy lub pełna kolejka USB nie blokuje
UART ani Zigbee. Zigbee obsługuje łącznie najwyżej jedną komendę na cykl,
wybierając porty naprzemiennie, gdy oba mają oczekujące komendy.

Kanały, konfiguracja i wirtualne przyciski są **wspólne**. PRESS na UART może
zostać zwolnione przez RELEASE na USB. RESET dotyczy całego urządzenia.
Numeracja ID jest niezależna: `@42` na USB i `@42` na UART to dwa różne
żądania. Zmiana portu nie przenosi ochrony przed powtórzeniem między portami.

Podczas wgrywania firmware'u zamknij terminal korzystający z USB. Po restarcie
port USB może wymagać ponownego otwarcia. Gdy USB zostanie odłączone podczas
odpowiedzi, po połączeniu sprawdź INFO/boot_id i powtórz żądanie z tym samym ID
na tym samym porcie, jeśli urządzenie nie restartowało się. Niepełną linię
JSON należy odrzucić; nadajnik kończy ją LF przed kolejną odpowiedzią.

## LOG 0 / LOG 1 — niezależne logi portów

**Domyślnie logi są wyłączone na obu portach**, także po restarcie. Stan nie
jest zapisywany do NVS. Komenda dotyczy wyłącznie portu, na którym ją wysłano:

```text
> LOG 1
< OK LOG 1
#LOG I (1200) xiao8: CH2 action=2 to Z2M
> LOG 0
< OK LOG 0
```

LOG 1 w terminalu USB włącza logi tylko na USB. UART nadal może mieć LOG 0.
Aby włączyć logi na obu portach, wyślij LOG 1 osobno na każdym. LOG 0 nie
wyłącza odpowiedzi, komend ani komunikatów ERR. Odczyty INFO i STATUS podają
`port` (`uart`/`usb`) i `logs_enabled` dla bieżącego portu.

Logi aplikacji/SDK (poziomy INFO, WARN i ERROR) są pojedynczymi liniami
z prefiksem **`#LOG `**. Wielowierszowe komunikaty są spłaszczane do jednej
linii, a długie skracane do 255 bajtów z LF. Program obsługujący protokół może
ignorować linie zaczynające się od #LOG. Odpowiedzi i logi nie mieszają się
wewnątrz jednej linii, bo fizyczny zapis portu wykonuje tylko jego zadanie.

Logowanie nigdy nie czeka na port w kontekście Zigbee: callback ESP_LOG
kolejkuje komunikaty bez oczekiwania. Każdy port ma osobną kolejkę 8 logów;
przy przeciążeniu log jest pomijany i zwiększa `counters.log_dropped`.
Odpowiedzi mają pierwszeństwo. Po LOG 0 stare oczekujące logi są odrzucane;
komunikat już wysłany przed potwierdzeniem wyłączenia może być jeszcze widoczny.

LOG można oznaczać ID. Ponowienie tego samego `@ID LOG 1` odtwarza ACK,
ale nie zmienia ponownie stanu po późniejszym LOG 0. Nowa zmiana wymaga
nowego wyższego ID, tak jak pozostałe operacje. LOG działa także przed
parowaniem i podczas reset_pending.

LOG steruje zwykłymi logami aplikacji/SDK po uruchomieniu firmware'u.
Nowa konfiguracja kompilacji wyłącza standardową konsolę i logi bootloadera,
ale komunikaty ROM, awarii/panic oraz starego bootloadera pozostawionego przy
aktualizacji samej aplikacji mogą pojawić się podczas startu poza tym mechanizmem.

## Format i komendy

Wielkie litery ASCII, jedna komenda w linii zakończonej LF (`\n`). CRLF również
jest akceptowane. Puste linie są ignorowane. Limit to 63 znaki przed LF
(z pominięciem CR). Za długa lub zawierająca niedozwolone znaki linia jest
odrzucana w całości; następna linia jest przetwarzana normalnie.
Kanał to liczba 1–8.

| Komenda | Efekt |
|---|---|
| `PING` | Odpowiedź `OK PING` |
| `INFO` | JSON z wersjami, numerem uruchomienia i możliwościami protokołu |
| `STATUS` | JSON ze stanem sieci, kanałów, kolejek i licznikami błędów |
| `RESET` | Usuń sieć Zigbee i bindingi, uruchom ponownie i rozpocznij parowanie |
| `LOG 0` / `LOG 1` | Wyłącz/włącz logi na bieżącym porcie; domyślnie 0 |
| `GET 2` | Konfiguracja i możliwości CH2 jako jedna linia JSON |
| `PRESS 2` | Naciśnij wirtualny przycisk CH2 |
| `RELEASE 2` | Zwolnij wirtualny przycisk CH2, anuluj jego sekwencję |
| `CLICK 2` | Naciśnięcie przez 100 ms, następnie zwolnienie |
| `DOUBLE 2` | Dwa naciśnięcia po 100 ms, przerwa 100 ms |
| `TRIPLE 2` | Trzy naciśnięcia po 100 ms, przerwy 100 ms |
| `HOLD 2 1500` | Naciśnięcie przez 1500 ms, następnie zwolnienie |
| `SET 2 ON` | Zamknij wirtualny styk rocker CH2 |
| `SET 2 OFF` | Otwórz wirtualny styk rocker CH2 |
| `RELEASE ALL` | Anuluj sekwencje i zwolnij wszystkie wirtualne przyciski |

`HOLD` przyjmuje czas 600–10000 ms. `CLICK`, `DOUBLE`, `TRIPLE`, `HOLD`
wymagają `input_mode=push`; `SET` wymaga `rocker`. `PRESS`/`RELEASE` działają
w obu trybach. `ON/OFF` w `SET` określa stan styku, nie komendę dla lampy.
Od 2.1.1 w `rocker + toggle` każda zmiana tego styku wysyła Toggle:
SET ON i późniejsze SET OFF przełączają lampę na przeciwny stan, a ponowne
SET ON bez zmiany styku niczego nie przełącza. W `rocker + momentary`
zamknięcie/otwarcie nadal daje ON/OFF. Dotyczy to fizycznego GPIO, UART i USB.

Dla ręcznego `PRESS`/`RELEASE` pozostaw minimum 100 ms pomiędzy zmianami.
Wysłanie obu w jednym pakiecie bez odstępu może zostać pominięte przez debounce.
Do pojedynczego kliknięcia używaj `CLICK`.

Sekwencje działają bez oczekiwania na ich zakończenie w odbiorniku komend;
inny kanał może być obsługiwany równolegle. W czasie sekwencji nowe
`PRESS`, `SET ON` lub skróty gestów na tym samym kanale zwracają `ERR BUSY`.
`RELEASE` i `RELEASE ALL` mogą anulować operację. Po przyjęciu RESET
kolejne operacje sterujące zwracają ERR RESET_PENDING aż do restartu.

`PRESS` i `SET ON` wygasają po 10 sekundach. Powtórzenie tej samej komendy
bez identyfikatora albo z nowym wyższym ID odnawia ten czas bez ponownego
naciśnięcia. Ponowienie z tym samym ID zwraca zapamiętaną odpowiedź i **nie
odnawia** limitu. Dla długotrwałego zamknięcia rocker
odświeżaj `SET N ON`, np. co 5 sekund. Wygaszenie może wygenerować Toggle/OFF/Stop/release,
zależnie od trybu kanału. Wirtualne stany nie są zapisywane do NVS, po restarcie
wszystkie są zwolnione.

## Identyfikatory i powtarzanie żądań

Dowolną komendę można poprzedzić `@ID` i spacją. ID to liczba 1–4294967295.
Nowe żądania na każdym porcie wysyłaj z **rosnącymi ID**, np. 1, 2, 3. Prefiks jest częścią
limitu 63 znaków linii. Stare komendy bez prefiksu są nadal obsługiwane.

```text
> @42 CLICK 2
< @42 OK CLICK 2
> @42 CLICK 2
< @42 OK CLICK 2
> @42 CLICK 3
< @42 ERR ID_CONFLICT
> @43 GET 2
< @43 {"channel":2,...}
```

W przykładzie JSON skrócono dla czytelności. Powtórzone CLICK 2 jest wykonywane
**tylko raz**. Format odpowiedzi to `@ID ` i dotychczasowy tekst lub JSON z LF.
Zdejmij prefiks przed parsowaniem JSON. Identyfikator jest normalizowany do
liczby dziesiętnej: `@00042` i `@42` oznaczają ten sam ID.

Pamięć RAM każdego portu zawiera 16 ostatnich zaakceptowanych żądań z ID i pełne odpowiedzi,
także błędy BUSY/NOT_READY/STORAGE. Identyczne żądanie zwraca tę samą odpowiedź,
bez ponownego wykonania i bez przedłużenia przytrzymania. To samo ID z innymi
argumentami zwraca ERR ID_CONFLICT, dopóki pierwotne żądanie jest w pamięci.
Starsze ID poza pamięcią oraz nowe żądanie z numerem mniejszym od ostatniego
zwracają ERR ID_EXPIRED: **nie wykonują się ponownie**. Pamięć odpowiedzi
obejmuje również INFO/STATUS/GET/PING z ID. Nowy odczyt wymaga nowego ID;
powtórzenie starego odczytu zwraca stary wynik.

Błędy składni i ERR QUEUE nie rezerwują ID — komenda nie została przyjęta do
wykonania. Można ponowić ją z tym samym numerem, o ile żadne późniejsze żądanie
z wyższym ID nie zostało już wykonane. Najprościej wysyłać pojedyncze żądania
i czekać na odpowiedź przed wysłaniem kolejnego. Po BUSY/NOT_READY zapamiętanym
w sesji użyj **nowego wyższego ID**, aby podjąć nową próbę.

Restart, utrata zasilania i RESET zerują całą pamięć identyfikatorów. Nie jest
ona zapisywana do NVS. INFO zwraca losowy `boot_id` danego uruchomienia.
Po jego zmianie nie ponawiaj automatycznie dawnych operacji — mogły już
zostać wykonane. Odczytaj stan i rozpocznij nową sesję. Po wyczerpaniu zakresu
ID potrzebne jest nowe uruchomienie; licznik nie zawija się do 1 w tej samej sesji.

## INFO i STATUS

`INFO` zwraca jedną linię JSON, np.:

```json
{"firmware":"2.1.1","protocol":2,"model":"XIAO-C6-8CH","boot_id":123456,"channels":8,"baud":115200,"format":"8N1","port":"uart","transports":["uart","usb"],"logs_enabled":false,"request_ids":true,"id_order":"increasing","dedup_depth":16,"hold_timeout_ms":10000,"commands":["PING","INFO","STATUS","GET","PRESS","RELEASE","CLICK","DOUBLE","TRIPLE","HOLD","SET","RELEASE ALL","RESET","LOG"]}
```

`STATUS` zwraca JSON z następującymi polami:

| Pole | Znaczenie |
|---|---|
| `port`, `logs_enabled` | Port odpowiedzi i jego bieżący stan logowania |
| `boot_id`, `uptime_ms` | Numer uruchomienia i czas od startu w milisekundach |
| `zigbee.joined` | Urządzenie jest dołączone do sieci; nie jest to test działania Z2M |
| `zigbee.bindings_known` | Zakończono pełny odczyt lokalnej tabeli bindingów |
| `zigbee.factory_new` | Stos Zigbee nie ma skonfigurowanej sieci |
| `zigbee.reset_pending` | Przyjęto RESET i oczekiwany jest restart |
| `zigbee.short_address` | Adres sieciowy lub null, gdy brak dołączenia |
| `queues.commands`, `queues.replies` | Liczba oczekujących komend/odpowiedzi; limit 8 każdej kolejki |
| `channels` | Osiem obiektów z polami opisanymi poniżej |
| `counters` | Liczniki żądań i diagnostyki UART |

Każdy element `channels` zawiera `channel`, `binding_status`
(unknown/unbound/bound), `binding_clusters` (bity 1/2/4/8 jak w Z2M),
`control_path` (binding/z2m/mixed), `physical`, `virtual`, `sequence_active`.
`physical` uwzględnia invert, `virtual` to wirtualne przytrzymanie, a
`sequence_active` oznacza sekwencję CLICK/DOUBLE/TRIPLE/HOLD, także przerwę
między kliknięciami. Suma logiczna physical/virtual jest stanem wejścia przed
debounce. GET nadal służy do odczytu ustawień i dostępnych gestów kanału.

Liczniki `counters`:

- `requests`: poprawne składniowo żądania pobrane przez Zigbee, także powtórki.
- `executed`: nowe żądania rozpatrzone przez Zigbee, w tym odczyty i odmowy
  BUSY/NOT_READY; nie jest to licznik potwierdzonych przełączeń lampy.
- `duplicates`, `id_conflicts`, `id_expired`: powtórki i błędy identyfikatorów
  w sesji bieżącego portu.
- `log_dropped`: logi pominięte z powodu pełnej kolejki bieżącego portu.
- `rx_bytes`, `rx_lines`, `parse_errors`, `queue_rejected`: ruch UART i odmowy.
- `fifo_overflows`, `buffer_full`, `frame_errors`, `parity_errors`, `breaks`,
  `event_queue_full`, `rx_resyncs`, `tx_errors`: diagnostyka transmisji.

Liczniki dotyczą portu, z którego wysłano STATUS (sprzętowe błędy UART na USB
mają wartość zero). `baud` w INFO opisuje fizyczny UART. Liczniki zerują się przy restarcie, są 32-bitowe i zawijają się po osiągnięciu
maksimum. Bezpośrednie PING bez ID nie zwiększa requests/executed. STATUS
obejmuje samo bieżące żądanie. Stan Zigbee/kanałów jest odczytywany w jednym
kontekście; liczniki UART i zajętość kolejek mogą zmieniać się w trakcie odczytu.

## Błędy odbioru UART i odzyskiwanie komunikacji

Sterownik monitoruje przepełnienie FIFO/bufora RX, błędy ramki/parzystości,
sygnał BREAK i przepełnienie kolejki zdarzeń. Po wykryciu błędu czyści RX,
odrzuca niedokończoną linię oraz ignoruje kolejne bajty aż do LF. Wysyła
**nieoznaczone ID** `ERR UART RX`, ponieważ uszkodzony identyfikator nie jest
wiarygodny. Szczegół błędu jest widoczny w licznikach STATUS.

Po ERR UART RX wyślij najpierw pustą linię LF, potem PING lub INFO. Jeżeli
brakuje odpowiedzi na wcześniejsze żądanie z ID, ponów je z tym samym ID w tej
samej sesji. Już przyjęte poprawne komendy pozostają w kolejce; błąd odbioru
nie cofa wykonanych operacji. Odzyskiwanie nie zatrzymuje fizycznych wejść ani
zegara wirtualnych przycisków. Po wykrytym błędzie w połowie `RE...SET` sam
zachowany fragment nie jest wykonywany jako RESET.

## RESET — zapomnienie sieci Zigbee

```text
> @100 RESET
< @100 OK RESET
```

RESET działa również bez ID, ale nie ma wtedy ochrony przed powtórzeniem.
Polecenie:

1. Zapisuje oczekujące zmiany ustawień kanałów; przy błędzie zwraca ERR STORAGE
   i nie rozpoczyna kasowania.
2. Anuluje sekwencje UART i zatrzymuje aktywne regulacje przycisków.
3. Zwraca OK RESET. Zadanie UART czeka na opróżnienie TX poza obsługą Zigbee.
4. Po co najmniej 200 ms wywołuje `esp_zb_factory_reset()`: SDK usuwa lokalne
   dane sieci Zigbee, klucze i bindingi z partycji zb_storage oraz restartuje
   urządzenie. Gdy potwierdzenie TX się nie powiedzie, reset następuje po 2 s,
   aby nie pozostać na stałe w stanie oczekiwania.
5. Po starcie urządzenie automatycznie próbuje dołączyć do sieci. W Z2M włącz
   **Permit join**. Przy zamkniętym parowaniu kolejne próby są ponawiane.

Konfiguracja CH1–CH8 i binding priority w NVS pozostaje zachowana. RESET nie
kasuje firmware’u i nie zmienia anteny. Różni się od przytrzymania BOOT przez
8 s, które usuwa także ustawienia kanałów. RESET jest lokalnym kasowaniem:
nie usuwa wpisu urządzenia z bazy koordynatora/HA. W razie potrzeby usuń stary
wpis w Z2M przed ponownym parowaniem.

OK RESET oznacza przyjęcie żądania, a nie zakończenie kasowania. Do restartu
nowe komendy sterujące są odrzucane jako ERR RESET_PENDING, a zapisy ustawień
z Z2M są odrzucane do czasu restartu; odczyty, LOG i PING
pozostają dostępne. Powtórka tego samego ID przed restartem dostaje ten sam
ACK bez drugiego resetu. Po restarcie pamięć ID jest pusta: jeśli ACK zaginęło,
sprawdź nowy boot_id zamiast automatycznie wysyłać RESET ponownie.

## Odczyt konfiguracji: GET

Wyślij `GET 2` z LF, aby odczytać konfigurację CH2. Dopuszczalne są kanały
1–8. Odpowiedzią jest **jeden obiekt JSON zakończony LF**, bez prefiksu `OK`.
Poniżej przykład dla trybu brightness-only (dla czytelności rozpisany na linie;
na przewodzie obiekt zajmuje jedną linię):

```json
{
  "channel": 2,
  "input_mode": "push",
  "switch_type": "brightness_level_up_no_onoff",
  "binding_priority": "ON",
  "invert": false,
  "switch_actions": "toggle",
  "min_level": 1,
  "max_level": 254,
  "transition_time": 25,
  "color_temp_min": 153,
  "color_temp_max": 500,
  "scene_id": 0,
  "group_id": 0,
  "commands": ["GET", "PRESS", "RELEASE", "CLICK", "DOUBLE", "TRIPLE", "HOLD"],
  "gestures": ["double", "hold", "release"]
}
```

Pola konfiguracji mają nazwy i jednostki jak w Z2M: `transition_time` w 0,1 s,
`color_temp_min/max` w miredach, `invert` jako JSON boolean, `binding_priority`
jako `ON`/`OFF`. Wszystkie ustawienia kanału są zwracane, także te nieużywane
przez aktualną funkcję. Odczyt nie zmienia stanu ani ustawień, niczego nie
zapisuje do NVS i nie generuje akcji Zigbee.

- `commands` opisuje dozwolone dla aktualnego push/rocker komendy kanału.
  Nie obejmuje globalnych `PING`, `INFO`, `STATUS`, `RESET`, `LOG` i `RELEASE ALL`. `GET` jest dostępne w obu
  trybach. Nie gwarantuje chwilowej gotowości: wykonanie nadal może zwrócić
  `ERR BUSY`, `ERR NOT_READY` lub `ERR QUEUE`.
- `gestures` opisuje gesty/zmiany styku dające efekt w wybranej funkcji,
  niezależnie od tego, czy polecenie pójdzie przez binding, czy jako `action`.
  `single` odpowiada pojedynczemu kliknięciu; `close/open` to zamknięcie/otwarcie
  rocker. `release` oznacza zwolnienie po naciśnięciu/przytrzymaniu, np. Stop.

Przykładowo `CLICK` jest poprawne składniowo w brightness_level_up_no_onoff,
ale pojedynczy klik jest celowo ignorowany — dlatego `single` nie ma na liście
`gestures`. `DOUBLE` powoduje tu krok jasności. W trybach toggle, momentary
oraz scene wielokrotne naciśnięcia mogą wykonać kilka zwykłych operacji;
nie są rozpoznawane jako osobny gest double/triple.

| input_mode / switch_type | gestures |
|---|---|
| rocker / toggle lub momentary | close, open |
| push / toggle | single |
| push / momentary | single, release |
| push / multifunction | single, double, triple, hold, release |
| push / brightness_level | single, hold, release |
| push / brightness_level_up, brightness_level_down | single, double, hold, release |
| push / move_to_color_temperature | hold, release |
| push / move_to_color_temperature_up, move_to_color_temperature_down | single, hold, release |
| push / scene | single |
| push / brightness_level_no_onoff | hold, release |
| push / brightness_level_up_no_onoff, brightness_level_down_no_onoff | double, hold, release |

Dla rocker `commands` to `["GET","PRESS","RELEASE","SET"]`, a `gestures`
to `["close","open"]`.

`GET` działa również przed dołączeniem do Zigbee i podczas trwającej sekwencji.
Przechodzi przez tę samą kolejkę co komendy sterujące; odczyt wykonuje zadanie
Zigbee, ze spójną konfiguracją i binding priority **z chwili wykonania**, a nie
z chwili odebrania linii. Dłuższy JSON wysyła zadanie UART; obsługa Zigbee nie
czeka na transmisję. Bufor transmisji mieści odpowiedź do 2063 bajtów łącznie z LF i opcjonalnym
prefiksem ID. Niekompletne JSON nie jest wysyłane.

## Odpowiedzi

Każda niepusta zakończona linia otrzymuje odpowiedź z LF. `GET`, `INFO` i
`STATUS` zwracają JSON, pozostałe komendy potwierdzenia `OK` lub błędy `ERR`. Przykłady:

```text
> PRESS 2
< OK PRESS 2
> RELEASE 2
< OK RELEASE 2
> HOLD 3 1500
< OK HOLD 3
```

Prefiksy `>` i `<` pokazują kierunek — nie są częścią transmisji.
`OK` oznacza przyjęcie komendy, nie zakończenie sekwencji ani potwierdzenie
przełączenia lampy. Nie ma osobnej odpowiedzi po zakończeniu sekwencji.

| Odpowiedź | Znaczenie |
|---|---|
| `ERR SYNTAX` | Nieznana komenda, nieprawidłowe argumenty/liczba argumentów |
| `ERR CHANNEL` | Kanał poza 1–8 lub niepoprawna liczba |
| `ERR DURATION` | Czas HOLD poza 600–10000 ms |
| `ERR MODE` | Komenda nie pasuje do push/rocker |
| `ERR BUSY` | Kanał wykonuje sekwencję albo jest wirtualnie wciśnięty |
| `ERR NOT_READY` | Brak dołączenia do Zigbee lub pierwszego odczytu bindingów |
| `ERR LINE` | Zbyt długa linia lub niedozwolone znaki |
| `ERR QUEUE` | Kolejka komend pełna; komenda nie została przyjęta ani wykonana |
| `ERR ID` | Niepoprawne ID, poza 1–4294967295 |
| `ERR ID_CONFLICT` | Ten sam zapamiętany ID użyty z innym żądaniem |
| `ERR ID_EXPIRED` | Stary ID poza pamięcią odpowiedzi lub ID niższy od ostatniego |
| `ERR RESET_PENDING` | Przyjęto RESET, operacje sterujące czekają na restart |
| `ERR STORAGE` | Nie zapisano ustawień kanałów; RESET nie został rozpoczęty |
| `ERR UART RX` | Utrata/niepewność danych UART, odrzucono bieżącą linię |
| `ERR CONFIG` | Odczyt GET wykrył nieprawidłową konfigurację |
| `ERR RESPONSE_SIZE` | Bufor nie mieści pełnej odpowiedzi; JSON nie jest wysyłany częściowo |

`PING`, `INFO`, `STATUS`, `GET`, `RESET`, `LOG` i zwalnianie przycisków działają
także przed gotowością Zigbee.
Stan PING potwierdza komunikację UART, nie dostępność Z2M; odpowiada na niego
zadanie UART bez udziału Zigbee, jeśli PING jest bez ID. PING z ID przechodzi
przez kolejkę i pamięć odpowiedzi, aby respektować reguły identyfikatorów. Przy normalnym sterowaniu wysyłaj kolejną
komendę po otrzymaniu odpowiedzi. Przy potokowym wysyłaniu wielu linii lokalne
błędy składni, ERR QUEUE i PING mogą wyprzedzić odpowiedzi na wcześniejsze
komendy oczekujące na wykonanie. Z ID odpowiedź można dopasować do żądania.

Odbiór, składnia i wysyłanie odpowiedzi działają w osobnych zadaniach FreeRTOS
portów na głównym rdzeniu, z priorytetem 4 (Zigbee: 5). Rdzeń LP nie jest używany.
Każdy port ma kolejki komend i odpowiedzi po 8 elementów. Obsługa Zigbee pobiera
najwyżej jedną komendę na cykl ok. 10 ms, zawsze bez oczekiwania. Komendę
wykonuje dopiero, gdy ma miejsce na jej odpowiedź. Przy pełnej kolejce
odpowiedzi nadal działają przyciski, gesty i limit przytrzymania; kolejne
komendy czekają. Pełna kolejka komend daje ERR QUEUE bez wykonania żądania.

UART może czekać na dane lub miejsce do wysłania odpowiedzi wyłącznie w swoim
zadaniu. Parser UART nie czyta konfiguracji ani nie zmienia stanu przycisków;
walidacja trybu, gotowości i BUSY odbywa się w momencie wykonania w kontekście
Zigbee. Stan kanałów zapisuje wyłącznie Zigbee; zadania portów przekazują tylko żądania.
Ograniczona liczba bajtów na przebieg zadania UART i oddawanie czasu procesora
zapobiegają nieograniczonej pętli parsowania przy ciągłym napływie danych.
Nie używaj UART do ciągłego strumienia bez oczekiwania na odpowiedzi.

## Współpraca z wejściami i Zigbee

Efektywny stan kanału to fizyczne wejście aktywne **LUB** przycisk UART wciśnięty.
`invert` odwraca wyłącznie wejście fizyczne. Zwolnienie przez UART nie zwolni
styku nadal trzymanego fizycznie. Skrót gestu podczas fizycznego naciśnięcia
może zatem nie utworzyć dodatkowych zboczy. Jednoczesne sterowanie tym samym
kanałem z obu źródeł traktuj jako wspólne trzymanie przycisku.

Próg przytrzymania i okno wielokliku to 500 ms; debounce 35 ms. Znaczenie gestu
określa `switch_type`: np. DOUBLE w multifunction daje `chN_double`, a w
brightness_level_up_no_onoff krok jasności. Gesty ignorowane przez dany tryb
pozostają ignorowane. Krótkie polecenia nie omijają rozpoznawania gestów.

Binding jest sprawdzany dla klastra polecenia. Przy `binding_priority=ON`
polecenie zbindowane idzie bezpośrednio bez `action`; przy OFF dodatkowo
raportuje `action`. Bez bindingu generuje `action`. Brak działającego Z2M nie
przeszkadza bezpośredniemu sterowaniu, gdy XIAO pozostaje w sieci Zigbee.

Zmiana parametrów wejścia lub funkcji w Z2M anuluje wirtualny stan/sekwencję
tego kanału i zatrzymuje trwającą regulację. Zmiana `binding_priority` nie
przerywa operacji. `RELEASE ALL` nie kasuje konfiguracji ani bindingów.

Przy utracie odpowiedzi do komendy z ID ponów **dokładnie to samo żądanie**
z tym samym ID. Nowy ID oznacza nową operację. Bez ID ponowienie `CLICK`
może wykonać drugi gest. Po restarcie najpierw sprawdź INFO/boot_id.

## Sprawdzenie

Po wgraniu firmware’u i dołączeniu do sieci:

1. Otwórz port urządzenia zewnętrznego z ustawieniami 115200/8N1.
2. Wyślij `PING` z LF — oczekuj `OK PING`.
3. Wyślij `GET 2` — sprawdź konfigurację, commands i gestures.
4. Dla push CH2 wyślij `CLICK 2` — zachowanie jak fizyczne kliknięcie.
5. Sprawdź HOLD/Stop w odpowiednim trybie oraz działanie CH7 i CH8.
6. Sprawdź oba ustawienia binding priority, binding bez Z2M i 10-sekundowy limit.

Testy na komputerze: `npm test`, `npm run test:serial`, `npm run test:transport`
i `npm run test:session`. Test używa produkcyjnego parsera,
zegara sekwencji i rozpoznawania przycisków; nie zastępuje testu po przewodach.

Test transportu wykonuje produkcyjny kod z zastępczymi kolejkami i sterownikiem
UART. Sprawdza rozdzielenie kontekstów, przeciążenie i odkładanie wykonania przy
pełnej kolejce odpowiedzi. Nie symuluje rzeczywistych przerwań i harmonogramu RTOS.

## Dodatkowe pola INFO od 2.2.0

INFO zwraca także `antenna` (external/internal) i `ota`: enabled,
manufacturer, image_type, file_version, slot, state, received, total.
Postęp received/total dotyczy bajtów aplikacji, bez nagłówka kontenera Zigbee.
Slot ota_0 albo ota_1 oznacza uruchomioną aplikację. Stany OTA: idle,
downloading, validating, ready, reboot_pending, error. Te pola nie zmieniają
składni komend ani niezależnych ustawień LOG dla obu portów.


## `on_off` od testowej wersji 2.2.1

`GET x` zwraca `"switch_actions":"on_off"`, jeśli wybrano tę opcję w Z2M
(konwerter 0.0.8 i nowy firmware). Listy komend i gestów pozostają zgodne z
`input_mode` i `switch_type`.
Dla `push + toggle` kolejne `CLICK x` lub stabilne `PRESS x` po `RELEASE x`
wysyłają jawne ON, OFF, ON, OFF. Puszczenie nie zmienia kolejności.
Dla `push + momentary` naciśnięcie i puszczenie wysyłają kolejne ON/OFF z tej samej sekwencji.
Wejście fizyczne i wirtualne tworzą jeden stan logiczny OR: nakładające się
naciśnięcia nie liczą się podwójnie. Każdy kanał ma własną kolejność.
Po restarcie lub zapisie opcji `switch_actions` pierwszą komendą jest ON.
Stan żarówek zmieniony zewnętrznie nie zmienia tej kolejności.


## `scene_cycle` od testowej wersji 2.2.1

`GET x` zawiera `"switch_type":"scene_cycle"` i `"scene_list":"0,1,3"`
oraz group_id. Konfiguracja jest zapisywana przez Z2M; GET jej nie zmienia.
Przy niepustej liście i group_id>0 każde stabilne naciśnięcie wybiera następne ID.
`CLICK x` wybiera jedną scenę, `DOUBLE x` dwie, `TRIPLE x` trzy.
`HOLD x 1000` wybiera jedną scenę przy naciśnięciu, bez dodatkowej akcji
po czasie lub puszczeniu. PRESS wymaga RELEASE przed następnym naciśnięciem.
Gest dający efekt to single. Nakładające się fizyczne i wirtualne wejście nie
liczy się podwójnie. Pusta lista lub group_id=0 nie generuje sceny.


## Protokół 3 — 24 endpointy (firmware 2.3.0)

Dwie przestrzenie adresowe obowiązują identycznie na UART D6/D7 i USB-C:

| Przykład | Znaczenie |
|---|---|
| CLICK 2 | Fizyczne wejście D1 przez aktualne mapowania |
| DOUBLE 2 | Dwa naciśnięcia wejścia D1, sklasyfikowane fizycznym dekoderem |
| HOLD 2 1500 | Przytrzymanie wejścia D1 przez 1500 ms |
| CLICK EP10 | Jeden klik wirtualnego endpointu 10, z pominięciem źródła i filtra gestu |
| DOUBLE EP10 / TRIPLE EP10 | Dwa/trzy kliknięcia logicznego EP10, wynik zależy od switch_type |
| PRESS EP18 / RELEASE EP18 | Wirtualne naciśnięcie/puszczenie endpointu 18 |
| HOLD EP18 1500 | Przytrzymanie logicznego endpointu 18 przez 1500 ms |
| SET EP24 ON / SET EP24 OFF | Ustawienie styku endpointu 24; tylko input_mode=rocker |
| GET 2 | Konfiguracja fizycznego D1: mode/invert/czasy, dozwolone komendy, lista endpointów |
| GET EP10 | Pełna konfiguracja EP10: działanie, źródło, gest, sceny, priorytet, komendy i gesty logiczne |
| STATUS EP18 | Binding clusters/control_path oraz stan wirtualny EP18 |
| RELEASE ALL | Zeruje wirtualne stany obu przestrzeni i zatrzymuje trwające regulacje na wszystkich EP |

Zakres bez prefiksu to 1..8, odpowiada D0/D1/D2/D3/D4/D5/D8/D9.
Z prefiksem **EP** zakres to 1..24. Wielkość liter ma znaczenie. CLICK 10 jest
błędem; poprawne jest CLICK EP10. Nie ma dodatkowego pola liczby kanałów w kablu.
ACK zachowuje adres: `OK CLICK EP10`, `OK RELEASE EP18`; błędny zakres zwraca
`ERR CHANNEL`, nieobsługiwany tryb `ERR MODE`. Wszystkie formy obsługują @ID.
Ten sam ID dla CLICK 2 i CLICK EP2 powoduje ID_CONFLICT, bo to różne operacje.

input_source i input_gesture filtrują **tylko fizyczne**/bare-channel wejście.
Dlatego fizyczny double D1 -> EP10 aktywuje EP10 raz, a DOUBLE EP10 wywołuje
dwa logiczne naciśnięcia: np. toggle przełączy dwa razy. `source=none` dopuszcza
sterowanie EP przez serial. PRESS/RELEASE wymagają minimum 100 ms na fazę.
HOLD ma zakres 600..10000 ms. Gdy wskazujesz fizyczny kanał z odbiorcą hold,
czas musi być przynajmniej pin_hold_time+100 ms, inaczej ERR DURATION.
EP HOLD korzysta z logicznego progu 500 ms, niezależnie od ustawień pinu.
SET wymaga rocker, skróty kliknięć/przytrzymania push. GET/STATUS działają offline.

Stany są wspólne dla obu portów i łączone OR z fizycznym źródłem: RELEASE EP18
puszcza stan serialowy, ale nie symuluje puszczenia nadal trzymanego fizycznego
przycisku. RELEASE ALL jest globalnym Stop; nie uruchomi ponownie gestu hold,
dopóki fizyczne wejście nie zostanie puszczone i ponownie naciśnięte.
PRESS/SET nadal mają watchdog 10 s. Deduplikacja, kolejki i logi per port pozostają.

INFO zwraca protocol=3, channels=8, endpoints=24 i endpoint_addressing=true.
STATUS bez argumentu zachowuje dotychczasowy ośmioelementowy widok zgodności;
bindingi w nim dotyczą EP1..8, a stan styku odpowiada wejściom fizycznym.
Przy zmienionych mapowaniach używaj STATUS EPn do diagnostyki konkretnego celu.
Pełny stan 24 EP pobierz osobnymi zapytaniami; pojedyncza odpowiedź nadal ma
limit 2048 bajtów, więc nie powiększamy kolejek/cache na małym urządzeniu.
GET bez prefiksu w działającym firmware pokazuje konfigurację **pinu**, nie
jednego arbitralnego endpointu. Starsze przykłady GET kanału w tym dokumencie
opisują wersję 2; aktualny odpowiednik konfiguracji logicznej to GET EPn.

Przy fizycznych mappingach single/double/hold komenda TRIPLE bez prefiksu
wykonuje trzy rzeczywiste kliknięcia; jeśli istnieje odbiorca double, otrzymasz
double i następnie single. Nie ma osobnej fizycznej trasy triple w wariancie
24 EP. TRIPLE EPn nadal korzysta z logicznego trybu endpointu (np. multifunction
raportuje chN_triple). OK oznacza przyjęcie operacji: pin bez przypisanych
endpointów lub nieaktywny gest danego switch_type może nie wygenerować Zigbee.
