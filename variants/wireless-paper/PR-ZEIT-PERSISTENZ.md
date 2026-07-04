## Was

Auf der **Heltec Wireless Paper** wird die in NVS gespeicherte Uhrzeit beim Boot wiederhergestellt, indem `loadTimePersistence()` einmalig in `esp32setup()` aufgerufen wird (WP-guarded).

## Warum

Die Wireless Paper hat **weder RTC noch GPS**. Die Firmware *speichert* die Uhrzeit zwar alle 15 Min in NVS (`saveTimePersistence()` in `esp32loop`), aber der passende **Lade-Aufruf** `loadTimePersistence()` existiert im Original **nur im T-Deck-Pfad** (`src/t-deck/tdeck_main.cpp`) — auf der WP/ESP32 wird er **nie** aufgerufen.

**Folge:** Nach jedem Reboot/Stromausfall startet die Uhr bei `2000-01-01`, bis (irgendwann) NTP greift → *„Time Lost"*. Da die WP weder RTC noch GPS hat, ist NVS die **einzige Brücke** über Netz-/NTP-Lücken.

## Wie

- **Datei:** `src/esp32/esp32_main.cpp` — in `esp32setup()` direkt nach `save_settings()`.
- **Additiv** (kapern): ein einzelner `loadTimePersistence();`-Aufruf, vollständig in `#if defined(BOARD_WIRELESS_PAPER)` gekapselt → **andere Boards unberührt**.
- Den **UTC-Offset** wendet `loadTimePersistence()` seit dev-Commit `9e049d3` **selbst** an (`SetClock(unix + node_utcoff, /*boUseUTC=*/false)` → Lokalzeit, vgl. Issue #972). `SetClock` greift nur bei gültigem Wert; **NTP/{CET}/Phone überschreiben** das später wie gehabt.
- `initTimePersistence()` (mit `clear()`) läuft weiterhin nur bei Flash-Clear/Versionswechsel — unberührt.

## Test

- Build grün: `wireless-paper` **und** `vision-master-e290` (Gegenprobe, da geteilter Code).
- Verhalten: Reboot ohne Netz → Uhr läuft mit der zuletzt gespeicherten (lokalen) Zeit weiter statt bei `2000-01-01`.

🤖 Generated with [Claude Code](https://claude.com/claude-code)
