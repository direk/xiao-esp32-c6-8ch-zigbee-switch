# KC868-A8S → XIAO ESP32-C6 przez UART

Dla XIAO z firmware'em **2.1.0 lub nowszym**. Konfiguracja KinCony używa
ESP32/ESP-IDF, PCF8574 0x22 dla wejść i 0x24 dla przekaźników.

## Pliki

- `xiao-uart.package.yaml`: wspólny pakiet UART, komend i diagnostyki.
- `kc868-a8s-xiao.example.yaml`: pełny przykład na podstawie konfiguracji
  użytkownika, z wejściami 1–8 → CH1–CH8 oraz dotychczasowymi akcjami HA/relay.
- `secrets.example.yaml`: nazwy sekretów używane przez pełny przykład.

Nie wgrywaj konfiguracji KinCony na XIAO — to dwa różne firmware'y.

## Przewody

W **tym szablonie** jawnie wybrano TX=GPIO15, RX=GPIO13 po stronie KinCony:

| KinCony KC868-A8S | XIAO |
|---|---|
| GPIO15, skonfigurowane jako TX | D7 / RX / GPIO17 |
| GPIO13, skonfigurowane jako RX | D6 / TX / GPIO16 |
| GND | GND |

To **UART 3,3 V**, nie elektryczny RS-232 ani RS485. Nie używaj zacisków A/B.
Nie podłączaj pinu VCC4V złącza GSM do 3V3 XIAO; XIAO może być zasilane przez USB.
Moduł GSM nie może jednocześnie używać tych linii — odłącz go, jeśli jest wpięty.

Nazwy GSM_RX/GSM_TX w opisach złączy mogą być podane z perspektywy modułu.
Kierunek w ESPHome wyznaczają **tx_pin i rx_pin**, nie nazwa nadruku. Producent
ma również przykład z ESP32 TX=13/RX=15. Jeżeli tak masz połączone przewody,
zamień tylko wartości `xiao_tx_pin` i `xiao_rx_pin` na GPIO13/GPIO15.
Oba warianty wymagają skrzyżowania TX z RX i wspólnej masy.

Źródła: [pinout ESPHome](https://devices.esphome.io/devices/kincony-kc868-a8s/),
[schemat producenta](https://www.kincony.com/download/KC868-A8S-schematic.pdf),
[przykład UART producenta](https://www.kincony.com/forum/showthread.php?pid=4054).

## Dołączenie do istniejącej konfiguracji

Skopiuj `xiao-uart.package.yaml` do tego samego katalogu co YAML urządzenia.
Zachowaj swoje Wi-Fi, API, OTA, I2C, przekaźniki i zdarzenia. Do istniejącego
`substitutions` dopisz piny, a na poziomie głównym dołącz pakiet:

```yaml
substitutions:
  heat_pump_name: rotenso
  xiao_tx_pin: GPIO15
  xiao_rx_pin: GPIO13

packages:
  xiao_uart: !include xiao-uart.package.yaml
```

Nie dodawaj drugiego klucza substitutions, jeżeli już go masz — scal wpisy.
Pakiet ustawia `logger.baud_rate: 0`, żeby logi ESPHome nie trafiały na UART.
Logi przez API/HA nadal działają. Pakiet dodaje też akcję API, nie zastępuje
Twojego klucza szyfrowania.

Do istniejących `on_press` i `on_release` każdego wejścia dopisz wywołanie
skryptu. Przykład dla input1 → XIAO CH1:

```yaml
on_press:
  - script.execute:
      id: xiao_send
      command: "PRESS 1"
  # Dalej dotychczasowe homeassistant.event / switch.toggle.
on_release:
  - script.execute:
      id: xiao_send
      command: "RELEASE 1"
  # Dalej dotychczasowe homeassistant.event.
```

Dla input2 użyj 2 itd. Nie wysyłaj dodatkowego HOLD z on_multi_click, gdy
przekazujesz już PRESS/RELEASE: XIAO samo rozpoznaje gesty i wysyła Stop po
puszczeniu. Dotychczasowe zdarzenia HA mogą pozostać w on_multi_click.

Pełny `kc868-a8s-xiao.example.yaml` zawiera już te zmiany dla ośmiu wejść.
Zachowuje lokalne przełączanie: input3→relay2, input4→relay1,
input5→relay3, input6→relay4. Pozostawia zdarzenia HA wszystkich ośmiu wejść.
Jeżeli dotychczasowa automatyzacja HA steruje tą samą lampą co nowy binding,
usuń z niej powielone sterowanie, aby nie przełączać lampy dwukrotnie.

Pełny przykład korzysta z `!secret`. Do swojego secrets.yaml dopisz nazwy
z secrets.example.yaml, przypisując im **swoje dotychczasowe wartości**.
Nie zastępuj całego secrets.yaml plikiem przykładowym.

## Encje i wywołania

Pakiet dodaje w HA:

- 8 przycisków `XIAO CH1 click` … `XIAO CH8 click`.
- Wybór kanału 1–8, komendy i czasu HOLD, oraz przycisk wysłania.
- INFO, STATUS, GET wybranego kanału i RELEASE ALL.
- Przełącznik `XIAO UART logs`, sterujący LOG 0/1 na UART XIAO.
  Stan jest potwierdzany przez ACK/INFO/STATUS, nie zgadywany optymistycznie.
- Łączność UART, stan dołączenia Zigbee, wersję, boot_id, ostatnie żądanie,
  odpowiedź, błąd oraz podsumowanie GET.
- Przycisk RESET Zigbee, domyślnie wyłączony w HA; usuwa sieć i bindingi XIAO.
  Nie jest wykonywany podczas startu ani diagnostyki.

Po uruchomieniu pakiet wysyła tylko LOG 0, INFO i STATUS. STATUS jest
odświeżane co 30 s, o ile skrypt nie wysyła innych poleceń. Nie jest to
heartbeat sterujący bindingami — działanie XIAO pozostaje niezależne od HA.
Łączność UART jest uznawana za aktywną po rozpoznanej odpowiedzi z ostatnich
65 s. `XIAO Zigbee joined` pokazuje ostatni wynik STATUS; sprawdzaj go razem
z łącznością UART.

W dowolnej automatyzacji ESPHome:

```yaml
- script.execute:
    id: xiao_send
    command: "DOUBLE 3"
```

Skrypt dodaje LF. Nie dopisuj `\n` do argumentu. Z HA można wywołać akcję
API (nazwę sprawdź w Narzędziach deweloperskich):

```yaml
action: esphome.kc868_a8s_xiao_uart_command
data:
  command: "HOLD 3 1500"
```

## Zachowanie i ograniczenia tego szablonu

- Ustaw na XIAO input_mode=push dla przycisków chwilowych. Funkcję kanału
  wybierasz w Z2M; binding_priority nadal obowiązuje dla komend z KinCony.
- XIAO rozpoznaje przytrzymanie od 500 ms, a Twoje zachowane zdarzenia HA
  ON_HOLD od 1 s. To dwa niezależne mechanizmy.
- HOLD/PRESS mają limit 10 s. Dla trwałego rocker trzeba odświeżać SET ON
  przed upływem tego limitu — pełny przykład nie robi tego automatycznie.
- Komendy są kolejkowane po stronie ESPHome (do 32 wywołań) z odstępem 25 ms,
  poniżej przepustowości schedulera XIAO. To ograniczenie tempa, nie oczekiwanie
  na ACK i nie gwarancja czasu wykonania. Duży ruch może opóźniać zbocza;
  sprawdź docelowe przyciski i równoczesne naciśnięcia na sprzęcie.
- To podstawowy klient **bez @ID i bez automatycznych ponowień**. Przy utracie
  odpowiedzi nie powtarza CLICK/RESET, więc nie powoduje drugiego wykonania.
  Ewentualny błąd urządzenia zobaczysz w `XIAO last error`. Identyfikatory są
  celowo odrzucane w xiao_send — niezależny restart KinCony wymagałby osobnej
  obsługi utrzymania sesji i numeracji ID.
- Odbiornik składa pełne linie także z wielu fragmentów UART. Parsuje cały
  JSON (do 2063 bajtów); encja ostatniej odpowiedzi pokazuje pierwsze 250 znaków,
  aby zmieścić się w stanie HA. Nie jest to ucięcie JSON przed parsowaniem.
- Lista komend w select jest stała. GET pokazuje tryb kanału; wybrana komenda
  musi pasować do trybu, np. SET wymaga rocker, a CLICK może być ignorowane
  w brightness-only. Szczegóły w [protokole XIAO](../docs/UART.md).

## Opcjonalne działanie bez HA/Wi-Fi

Sam UART i bindingi nie wymagają HA. Jeśli KinCony ma pozostawać aktywne przy
długiej niedostępności HA lub Wi-Fi, możesz wyłączyć związane z tym restarty
ESPHome, dopisując do istniejących sekcji (bez dublowania kluczy):

```yaml
api:
  reboot_timeout: 0s
wifi:
  reboot_timeout: 0s
```

Pełny przykład zachowuje dotychczasowe ustawienia tych sekcji; nie zmienia
polityki restartów automatycznie.

## Sprawdzenie przed wgraniem

Sprawdzono walidację, generowanie C++ i **pełną kompilację** w ESPHome
**2025.9.3**, ESP-IDF **5.4.2**, dla esp32dev. Wynik: SUCCESS, RAM 35 884 B
(11,0%), aplikacja 1 017 102 B (55,4% partycji OTA 1 835 008 B).
Budowano kopię w katalogu tymczasowym z testowymi sekretami; gotowy binarny
obraz nie jest dołączony, ponieważ należy zbudować własny z Twoimi ustawieniami.

Lokalne narzędzia PlatformIO na macOS wymagały normalizacji aliasów /tmp i
/private/tmp podczas wyznaczania ścieżek obiektów C. Poprawka dotyczyła tylko
tymczasowej instalacji narzędzi, nie YAML ani kodu ESPHome.
ESPHome sygnalizuje GPIO15 i GPIO5 jako piny strapping — to ostrzeżenie o
właściwościach ESP32, nie błąd składni konfiguracji.

```sh
esphome config kc868-a8s-xiao.example.yaml
esphome compile kc868-a8s-xiao.example.yaml
```

Po wgraniu najpierw użyj INFO/STATUS i sprawdź wersję 2.1.0 oraz stan sieci.
Następnie sprawdź pojedyncze wejście, krótki klik, HOLD i Stop po puszczeniu,
a potem pozostałe kanały. Nie używaj RESET do testowania połączenia.
Nie wykonano zdalnego wgrania na KinCony ani testu obu płytek po przewodach.

Dokumentacja ESPHome: [UART](https://esphome.io/components/uart/),
[API actions](https://esphome.io/components/api/#user-defined-actions),
[logger](https://esphome.io/components/logger/),
[JSON](https://esphome.io/components/json/).
