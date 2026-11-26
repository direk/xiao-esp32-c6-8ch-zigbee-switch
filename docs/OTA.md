# Aktualizacje Zigbee OTA — od 2.2.0

XIAO udostępnia standardowy klaster OTA Upgrade 0x0019 jako klient na endpoint 1.
Zigbee2MQTT jest serwerem aktualizacji. Zasilanie pozostaje stałe; GPIO, kanały,
binding priority i naprawa bindingów z 2.1.3 działają tak jak wcześniej.

## Pierwsza instalacja klienta OTA

Firmware 2.1.x nie ma klienta OTA. Pierwsze przejście na 2.2.0 wymaga USB oraz
nowego bootloadera, tablicy partycji i danych wyboru slotu, a nie samej aplikacji.
Rozpakuj paczkę wydania, zainstaluj esptool według README i wykonaj kopię pamięci:

```sh
python -m esptool --chip esp32c6 --port PORT read_flash 0x0 0x400000 xiao-backup.bin
```

Kopia zawiera także prywatne dane sieci; przechowuj ją lokalnie. Następnie,
przy antenie wewnętrznej:

```sh
python -m esptool --chip esp32c6 --port PORT write_flash 0x0 bootloader.bin 0x8000 partition-table.bin 0x10000 application-internal.bin 0x196000 ota-data-initial.bin
```

Dla anteny zewnętrznej zamień aplikację na `application-external.bin`.
**Nie wykonuj erase_flash.** Zapisywane segmenty nie obejmują NVS i danych
Zigbee. Sieć, bindingi i ustawienia zachowują dotychczasowe adresy. Obrazy
`firmware-factory-*.bin` są do czystej instalacji, nie do zachowującej dane migracji.
Gdy płytka zostanie w trybie programowania, naciśnij sam RESET, bez BOOT.

Przy przyszłej aktualizacji przez USB również użyj całego powyższego zestawu:
wybiera on slot ota_0. Sam zapis aplikacji pod 0x10000 nie wystarczy, jeśli
urządzenie uruchamia aktualnie slot ota_1.

| Partycja | Adres | Rozmiar |
|---|---|---|
| NVS — bez zmian | 0x9000 | 24 KB |
| ota_0 — dawny adres aplikacji | 0x10000 | 1536 KB |
| zb_storage — bez zmian | 0x190000 | 16 KB |
| zb_fct — bez zmian | 0x194000 | 4 KB |
| otadata | 0x196000 | 8 KB |
| ota_1 | 0x1a0000 | 1536 KB |

OTA zapisuje nieaktywny slot; nie zmienia bootloadera ani tablicy partycji.
Przyszła zmiana układu pamięci nadal będzie wymagała migracji USB.

## Konwerter i interview w Z2M

1. Podmień `xiao-c6-8ch.mjs` na rewizję 0.0.7 z tego wydania i zrestartuj Z2M.
2. Dla istniejącego urządzenia wykonaj **ponowny interview**, aby Z2M odkrył
   klaster genOta na endpoint 1. Samo Reconfigure nie odświeża listy klastrów.
3. Wykonaj konfigurację urządzenia i otwórz zakładkę OTA.

Konwerter zweryfikowano z zigbee-herdsman-converters 26.135.0 i zawartą w nim
biblioteką zigbee-herdsman. Ustawienie `ota` w definicji włącza serwer aktualizacji
Z2M; indeks nie jest automatycznie dodawany do centralnego repo Koenkk/zigbee-OTA.

## Indeks aktualizacji

W configuration.yaml Z2M dodaj (łącząc z istniejącą sekcją ota):

```yaml
ota:
  zigbee_ota_override_index_location: https://raw.githubusercontent.com/direk/xiao-esp32-c6-8ch-zigbee-switch/main/ota/index.json
```

Jeśli używasz już własnego indeksu, połącz jego wpisy z naszym indeksem.
Po zmianie zrestartuj Z2M. Indeks ma dwie pozycje, osobną dla każdej anteny.
Pliki `.zigbee` są w katalogu `ota/` repozytorium i jako załączniki release;
indeks wskazuje wersjonowane załączniki release. Zawiera SHA512, identyfikatory,
wersję i ograniczenie sprzętowe. Możesz też pobrać `ota-index.json` i używać go
jako własnego lokalnego indeksu.

W panelu OTA sprawdź dostępność, a potem uruchom aktualizację. Możliwe są też
żądania MQTT, np.:

```text
zigbee2mqtt/bridge/request/device/ota_update/check
{"id":"nazwa_xiao"}

zigbee2mqtt/bridge/request/device/ota_update/update
{"id":"nazwa_xiao"}
```

Do jednorazowego testu możesz wskazać własny plik `.zigbee` w interfejsie Z2M
albo przekazać `url` pliku w żądaniu update. Wybierz plik odpowiadający antenie.
Nie wgrywaj pliku `.bin` jako aktualizacji Zigbee.

## Identyfikacja i wersje

| Parametr | Wewnętrzna | Zewnętrzna |
|---|---|---|
| Manufacturer code | 0x131B | 0x131B |
| Image type | 0xC600 | 0xC601 |
| Hardware version | 1 | 1 |
| Wersja 2.2.0 | 0x02020000 | 0x02020000 |

Wersję pliku kodujemy jako major.minor.patch.0: 2.2.1 to 0x02020100.
Firmware akceptuje wyłącznie nowszy obraz zgodnego producenta, typu i sprzętu.
Nie obsługuje downgrade ani wymiany obrazu na przeciwny wariant anteny przez
OTA. Aby zmienić antenę, użyj odpowiedniego zestawu USB.

Testowy firmware USB 2.2.0-rc1 reklamuje 0x0201FFFF, dzięki czemu można
przetestować aktualizację do produkcyjnego pliku 2.2.0. Zwykłe urządzenie z
zainstalowanym 2.2.0 poprawnie pokaże brak aktualizacji przy pliku 2.2.0.

## Walidacja i zachowanie podczas aktualizacji

- Strumień obsługuje nagłówek elementu rozdzielony między bloki; nie wymaga
  konkretnej wielkości pierwszego bloku. Niepełne obrazy, dodatkowe bajty,
  nieobsługiwane elementy i przekroczenie rozmiaru slotu są odrzucane.
- Przed zmianą slotu ESP-IDF sprawdza obraz aplikacji i jego sumę kontrolną.
  Dodatkowo weryfikowane są project_name i zgodność wersji aplikacji z OTA.
- Przerwanie transferu nie wybiera niedokończonego obrazu. Bezczynność przez
  trzy minuty przerywa lokalny zapis. Można ponowić aktualizację.
- Po poprawnym transferze zapisywane są ustawienia wejść i wysyłane Stop dla
  aktywnej regulacji. Restart następuje po krótkiej zwłoce.
- Bootloader ma rollback. Nowa aplikacja potwierdza rozruch po 10 sekundach
  działania lokalnej pętli Zigbee; brak dostępności koordynatora nie blokuje
  potwierdzenia. Nie jest to test całej sieci ani gwarancja poprawności wszystkich
  funkcji firmware’u. Awaria przed potwierdzeniem pozwala wrócić do poprzedniego slotu.
- Aktualizacja może trwać kilkadziesiąt minut. Przyciski i bindingi nadal działają
  w trakcie pobierania; podczas restartu występuje krótka przerwa.

INFO przez USB/UART pokazuje `antenna` i obiekt `ota`: enabled, identyfikatory,
file_version, bieżący slot, stan i liczbę bajtów received/total. Domyślnie logi
pozostają wyłączone. Postęp możesz śledzić w Z2M albo włączyć LOG 1 na jednym porcie.
Pełny transfer radiowy jest osobnym testem sprzętowym; jego aktualny wynik podaje
[VALIDATION.md](VALIDATION.md).

## Budowanie i publikacja

`python scripts/package_release.py --idf-path "$IDF_PATH"` buduje obie anteny,
obrazy USB, pliki `.zigbee` i indeks. `--sync-repository-ota` aktualizuje także
śledzone pliki w `ota/`; przed tagiem i release należy je dodać do commita.
Zmiana sdkconfig.defaults automatycznie regeneruje sdkconfig w katalogach build,
żeby stara konfiguracja 2.1.x nie wyłączyła rollbacku. Nie przechowuj własnych
zmian konfiguracji kompilacji wyłącznie w wygenerowanym sdkconfig.

Źródła: [Z2M OTA](https://www.zigbee2mqtt.io/guide/usage/ota_updates.html),
[klient OTA SDK 1.x](https://github.com/espressif/esp-zigbee-sdk/tree/release/v1.0/examples/esp_zigbee_ota/ota_client),
[ESP-IDF OTA](https://docs.espressif.com/projects/esp-idf/en/v5.4.2/esp32c6/api-reference/system/ota.html).
