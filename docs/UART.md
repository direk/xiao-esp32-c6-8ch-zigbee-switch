# Sterowanie przyciskami przez UART

Firmware obsługuje osiem wirtualnych przycisków. Korzystają z tych samych
ustawień kanałów, logiki gestów, bindingów i `binding_priority` co fizyczne wejścia.
Konwerter Z2M nie wymaga dodatkowych zmian z powodu UART.

## Połączenie

UART1: **115200 baud, 8N1, bez kontroli przepływu**, poziomy logiczne **3,3 V**.

| Urządzenie zewnętrzne | XIAO ESP32-C6 |
|---|---|
| TX | D7 / RX / GPIO17 |
| RX | D6 / TX / GPIO16 |
| GND | GND |

Prawdziwy RS-232 wymaga transceivera poziomów, np. MAX3232 zasilanego 3,3 V.
Nie podłączaj sygnałów RS-232 ani UART 5 V bezpośrednio do GPIO.
Konsola i logi pozostają na USB Serial/JTAG. D6/D7 służą do komend,
CH7/CH8 są na D8/D9. Przed aktualizacją usuń stare przyciski z D6/D7.

## Format i komendy

Wielkie litery ASCII, jedna komenda w linii zakończonej LF (`\n`). CRLF również
jest akceptowane. Puste linie są ignorowane. Limit to 63 znaki przed LF
(z pominięciem CR). Za długa lub zawierająca niedozwolone znaki linia jest
odrzucana w całości; następna linia jest przetwarzana normalnie.
Kanał to liczba 1–8.

| Komenda | Efekt |
|---|---|
| `PING` | Odpowiedź `OK PING` |
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

Dla ręcznego `PRESS`/`RELEASE` pozostaw minimum 100 ms pomiędzy zmianami.
Wysłanie obu w jednym pakiecie bez odstępu może zostać pominięte przez debounce.
Do pojedynczego kliknięcia używaj `CLICK`.

Sekwencje działają bez oczekiwania na ich zakończenie w odbiorniku komend;
inny kanał może być obsługiwany równolegle. W czasie sekwencji nowe
`PRESS`, `SET ON` lub skróty gestów na tym samym kanale zwracają `ERR BUSY`.
`RELEASE` i `RELEASE ALL` zawsze mogą anulować operację.

`PRESS` i `SET ON` wygasają po 10 sekundach. Powtórzenie tej samej komendy
odnawia ten czas bez ponownego naciśnięcia. Dla długotrwałego zamknięcia rocker
odświeżaj `SET N ON`, np. co 5 sekund. Wygaszenie może wygenerować OFF/Stop/release,
zależnie od trybu kanału. Wirtualne stany nie są zapisywane do NVS, po restarcie
wszystkie są zwolnione.

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
  Nie obejmuje globalnych `PING` i `RELEASE ALL`. `GET` jest dostępne w obu
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
czeka na transmisję. Odpowiedź ma maksymalnie 767 bajtów, łącznie z LF.

## Odpowiedzi

Każda niepusta zakończona linia otrzymuje odpowiedź z LF. `GET` zwraca JSON,
pozostałe komendy potwierdzenia `OK` lub błędy `ERR`. Przykłady:

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
| `ERR CONFIG` | Odczyt GET wykrył nieprawidłową konfigurację |
| `ERR RESPONSE_SIZE` | Bufor nie mieści pełnej odpowiedzi GET; JSON nie jest wysyłany częściowo |

`PING`, `GET` i zwalnianie przycisków działają także przed gotowością Zigbee.
Stan PING potwierdza komunikację UART, nie dostępność Z2M; odpowiada na niego
zadanie UART bez udziału Zigbee. Przy normalnym sterowaniu wysyłaj kolejną
komendę po otrzymaniu odpowiedzi. Przy potokowym wysyłaniu wielu linii lokalne
błędy składni, ERR QUEUE i PING mogą wyprzedzić odpowiedzi na wcześniejsze
komendy oczekujące na wykonanie. Nie ma identyfikatorów żądań.

Odbiór, składnia i wysyłanie odpowiedzi działają w osobnym zadaniu FreeRTOS
na głównym rdzeniu, z priorytetem 4 (Zigbee: 5). Rdzeń LP nie jest używany.
Kolejki komend i odpowiedzi mieszczą po 8 elementów. Obsługa Zigbee pobiera
najwyżej jedną komendę na cykl ok. 10 ms, zawsze bez oczekiwania. Komendę
wykonuje dopiero, gdy ma miejsce na jej odpowiedź. Przy pełnej kolejce
odpowiedzi nadal działają przyciski, gesty i limit przytrzymania; kolejne
komendy czekają. Pełna kolejka komend daje ERR QUEUE bez wykonania żądania.

UART może czekać na dane lub miejsce do wysłania odpowiedzi wyłącznie w swoim
zadaniu. Parser UART nie czyta konfiguracji ani nie zmienia stanu przycisków;
walidacja trybu, gotowości i BUSY odbywa się w momencie wykonania w kontekście
Zigbee. Stan kanałów nie jest współdzielony do zapisu między zadaniami.
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

Przy przerwaniu transmisji w połowie linii wyślij najpierw LF, odczytaj odpowiedź,
a potem `RELEASE ALL` i nową komendę. Protokół nie zapewnia identyfikatorów ani
deduplikacji żądań: ponowienie `CLICK` po utracie odpowiedzi może ponowić gest.

## Sprawdzenie

Po wgraniu firmware’u i dołączeniu do sieci:

1. Otwórz port urządzenia zewnętrznego z ustawieniami 115200/8N1.
2. Wyślij `PING` z LF — oczekuj `OK PING`.
3. Wyślij `GET 2` — sprawdź konfigurację, commands i gestures.
4. Dla push CH2 wyślij `CLICK 2` — zachowanie jak fizyczne kliknięcie.
5. Sprawdź HOLD/Stop w odpowiednim trybie oraz działanie CH7 i CH8.
6. Sprawdź oba ustawienia binding priority, binding bez Z2M i 10-sekundowy limit.

Testy na komputerze: `npm test`, `npm run test:serial` i `npm run test:transport`. Test używa produkcyjnego parsera,
zegara sekwencji i rozpoznawania przycisków; nie zastępuje testu po przewodach.

Test transportu wykonuje produkcyjny kod z zastępczymi kolejkami i sterownikiem
UART. Sprawdza rozdzielenie kontekstów, przeciążenie i odkładanie wykonania przy
pełnej kolejce odpowiedzi. Nie symuluje rzeczywistych przerwań i harmonogramu RTOS.
