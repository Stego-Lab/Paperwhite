# Neue Variante: Heltec Wireless Paper (ESP32-S3 + SX1262 + 2.13″ E-Ink)

Diese PR fügt die **Heltec Wireless Paper** als neue PlatformIO-Variante `wireless-paper`
hinzu (ESP32-S3FN8 + SX1262, 2.13″ E-Ink 250×122). Die Variante ist additiv aufgebaut:
sie nutzt den bestehenden `HAS_EPAPER`-Darstellungspfad und den `SX1262_E290`-Funkpfad,
sodass keine bestehenden Boards verändert werden. Alle board-spezifischen Eingriffe sind
mit `#if defined(BOARD_WIRELESS_PAPER)` gekapselt.

Die zur Laufzeit erkannte Display-Hardware-Version wird ausschließlich als **DEBUG-Zeile
am Terminal** ausgegeben, der **Start-Screen bleibt im unveränderten E290-Layout**
(nur der Board-Name lautet board-spezifisch „Heltec PaperW").

---

## 1. Neue Variante & Build-Einbindung
- **`variants/wireless-paper/configuration.h`** *(neu)* — Variantenkonfiguration:
  `MODUL_HARDWARE HELTEC_WIRELESS_PAPER`, `RF_FREQUENCY 433.175`, Radio-Schalter
  `SX1262_WIRELESS_PAPER` + `SX1262_E290` (gleicher Chip/Pinout wie E290), `HAS_EPAPER`,
  LoRa-Parameter (SF11/BW250/CR6), sämtliche GPIOs (siehe unten).
- **`variants/wireless-paper/platformio.ini`** *(neu)* — Env `wireless-paper`
  (board `heltec_wifi_lora_32_V3`, safeboot-Partition, esptool-Upload, `lib_ignore`,
  `EspSoftwareSerial`). **Kein** `ARDUINO_USB_CDC_ON_BOOT` — die Wireless Paper hängt
  über einen **CP2102-USB-UART** (kein natives USB) am Rechner, daher muss `Serial` auf
  UART0 laufen, sonst bleibt die serielle Konsole stumm.
- **`platformio.ini`** — `wireless-paper` zu `default_envs` ergänzt.
- **`.github/workflows/meshcom-ci.yml`** — Build/Release-Asset `wireless-paper.bin`
  ergänzt, damit die CI die Variante baut und im Release veröffentlicht.
- **`src/configuration_global.h`** — neue HW-ID `HELTEC_WIRELESS_PAPER 57`
  (45 war bereits durch `TBEAM_1262` belegt → Kollision vermieden).
- **`src/mheard_functions.cpp`** — `max_hardware` erhöht, `WLPAPER`/`WIRELESS_PAPER`
  in die HardWare-Tabellen aufgenommen, `getHardwareLong()` mappt ID 57.

## 2. Display-Treiber + Laufzeit-Erkennung der HW-Version
Die Wireless Paper verbaut je nach HW-Revision unterschiedliche Panel-Controller. Beide
Treiber wurden ergänzt und werden **zur Laufzeit per Chip-ID** ausgewählt:
- **`src/Displays/E0213A367/`** *(neu)* — Controller der V1.0 / V1.1.1 / V1.2
  (inkl. Partial-Refresh-LUT gegen Ghosting).
- **`src/Displays/LCMEN2R13EFC1/`** *(neu)* — Controller der V1.1 (inkl. Partial-LUTs).
- **`src/Displays/class-aliases.h`**, **`src/heltec-eink-modules.h`**,
  **`src/Displays/BaseDisplay/layout.cpp`** — Einbindung der neuen Panels.
- **`src/esp32/esp32_functions.cpp`** — `detectEinkChipId()` liest per Software-SPI
  (Bit-Bang, Methode wie im Heltec-Factory-Test) die Controller-ID aus und instanziiert
  in `initDisplay()` den passenden Treiber (Polymorphie über `BaseDisplay*`-Zeiger
  + `epaper_display`-Makro). Das erkannte Panel wird als DEBUG-Zeile am Terminal
  ausgegeben. Der Start-Screen in `startDisplay()` bleibt im E290-Layout.

## 3. Display-Anpassung an das kleinere 2.13″-Panel (Hardware-bedingt)
Alle Anpassungen unter `#if defined(BOARD_WIRELESS_PAPER)` in **`src/loop_functions.cpp`**:
- Seiten-abhängiges Layout (`wpApplyLayout`/`dzeile`) und FreeSans- statt FreeMono-Schrift
  für die Lesbarkeit auf dem kleinen Panel.
- Laufende Uhr `wpRefreshClock()` (10-s-Teilbereich-Refresh der Statuszeile, schont E-Ink).
- Voll-Refresh (`fastmodeOff`) für Nachrichten-, Positions-, Track- und Info-Seite ->
  kein Ghosting/kein Partial-Zwischenframe.
- Track-Modus-Wechsel: physischer `clear()` + Neuaufbau der passenden Seite (sonst bleibt
  beim Abschalten die alte Seite stehen bzw. die neue überschreibt die alte).
- Track-/GPS-Statusseite in FreeSans 9pt (sonst bricht die `RATE … NEXT …`-Zeile um).
- `src/onebutton_functions.cpp`, `src/loop_functions.h`, `src/lora_functions.cpp` —
  kleine additive Guards für den E-Paper-Pfad ohne GPS.

## 4. Batteriemessung (korrekte Heltec-Wireless-Paper-Pinbelegung)
- **`variants/wireless-paper/configuration.h`** — `BATTERY_PIN 20`, `ADC_CTRL_WP 19`
  (Mess-Freigabe, active LOW), `ADC_MULTIPLIER 2.0` (1:1-Spannungsteiler).
- **`src/batt_functions.cpp`** — eigener `BOARD_WIRELESS_PAPER`-Zweig in `vbat_pin`,
  `init_batt()` und `read_batt()`: ADC_CTRL LOW -> 8× `analogReadMilliVolts` -> ×2 ->
  ADC_CTRL HIGH. Ohne diesen Zweig fiel die WP in den generischen ADC-Fallback und maß nie
  korrekt.

## 5. Onboard-LED & Funk
- **`variants/wireless-paper/configuration.h`** — `BOARD_LED 18` (einfache LED, active HIGH;
  Heartbeat über bestehenden `BOARD_LED`-Pfad, standardmäßig AUS, opt-in via `--board led on`).
- **`src/esp32/esp32_main.cpp`** — WP-spezifisch `radio.setTCXO(1.8)` (Heltec/Meshtastic-Spec;
  RadioLib-Default von `begin()` wäre 1.6V). Verbessert die Frequenzstabilität über
  Temperatur; der gemeinsam genutzte E290-Funkpfad bleibt unverändert bei 1.6V.
  (DIO2-als-RF-Switch und RX-Boosted-Gain sind bereits über RadioLib bzw.
  `setRxBoostedGainMode()` abgedeckt.)
- **`src/lora_setchip.cpp`**, **`src/command_functions.cpp`** — additive
  `SX1262_WIRELESS_PAPER`/`SX1262_E290`-Einbindung.

---

## Test
Auf realer Hardware (Heltec Wireless Paper V1.2, Panel E0213A367) gebaut, geflasht und
verifiziert: Panel-Erkennung, E-Ink-Darstellung (Nachrichten/Position/Track/Info ohne
Ghosting), Batteriemessung, LoRa-RX/TX (433.175 MHz, listen success), Terminal über CP2102.
