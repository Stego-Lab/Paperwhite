# 🛰️ MeshCom Firmware — Dauerlauf-/Stabilitaets-Audit

> **Board:** `BOARD_WIRELESS_PAPER` (Heltec Wireless Paper, ESP32-S3 + SX1262)
> **Mission:** Code finden, der den **24/7-Dauerbetrieb gefaehrdet** — damit das Modul laeuft und **nicht taeglich neu gestartet** werden muss.

| Badge | Wert |
|-------|------|
| ![Board](https://img.shields.io/badge/Board-Wireless__Paper-blue) | Heltec Wireless Paper (ESP32-S3) |
| ![FW](https://img.shields.io/badge/Firmware-4.35p-informational) | MeshCom v4.35p |
| ![Datum](https://img.shields.io/badge/Datum-2026--06--01-lightgrey) | Audit-Stichtag |
| ![Modus](https://img.shields.io/badge/Modus-READ--ONLY-red) | Kein Code veraendert |
| ![Scope](https://img.shields.io/badge/Scope-WP__build__src__filter-green) | ohne nrf52/t-deck/t-deck-pro/t5-epaper/safeboot/lvgl |

**Scope-Quelle:** `variants/wireless-paper/platformio.ini` → `board = heltec_wifi_lora_32_V3`, `build_src_filter` schliesst `nrf52/*`, `t-deck/*`, `t-deck-pro/*`, `t5-epaper/*`, `safeboot/*`, `lib/lvgl/*` aus. `esp32/*`, `loop_functions`, `lora_functions`, `udp_functions`, `aprs_functions`, `batt_functions`, `clock`, `net_console`, `Platforms/WirelessPaper/*` sind **IN Scope**. Code hinter `#if defined(BOARD_T_DECK…)` / `#if defined BOARD_RAK4630` / `BOARD_E290`-only wurde als nicht-kompiliert ignoriert.

---

## 1. Executive Summary

🟠 **Gesamturteil: BEDINGT dauerlauf-tauglich — ein systemischer Befund verhindert echten unbeaufsichtigten 24/7-Betrieb ueber > ~49 Tage.**

Der LoRa-RX/TX-Hot-Path und die WP-Plattform-Schicht sind sauber (ueberlaufsichere `millis()-start`-Vergleiche, geschuetzte Doppelpuffer, korrekte Heap-Freigabe). **Aber:** die gesamte zyklische Aufgaben-Steuerung im Haupt-Loop (`esp32loop`) benutzt das **ueberlauf-anfaellige Muster `(timer + intervall) < millis()`**. Nach ca. 49,7 Tagen Uptime (`millis()`-Wraparound) brechen NTP-Sync, Positions-/Telemetrie-/Heartbeat-Versand, GPS-/MCP-Refresh und Heap-Monitor gleichzeitig — entweder Dauerfeuer oder Stillstand bis zum naechsten Wrap. Das ist genau die Klasse von Fehler, die „taeglich neu starten" notwendig macht, sobald jemand das Geraet wirklich lange laufen laesst.

Daneben ein **echter Stack-Puffer-Schreibfehler** (`snprintf` mit zu grossem Groessen-Argument in der MCP-Webseite) und mehrere **Boot-Zeit-Todesschleifen** (`while(true);`) bei LoRa-Konfigurationsfehlern.

| Severity | Anzahl | Bedeutung |
|----------|:------:|-----------|
| 🔴 Critical | **1** | Fuehrt mit hoher Wahrscheinlichkeit zu Hang/Fehlverhalten im Normalbetrieb |
| 🟠 High | **3** | Hang/Fehlverhalten unter realistischen Sonderbedingungen |
| 🟡 Medium | **4** | Destabilisiert langfristig / Robustheitsmangel |
| 🟢 Low | **4** | Code-Smell ohne direkte Stabilitaetsfolge |
| **Summe** | **12** | |

> 💡 **Kann das 24/7 laufen?** — Im Wochenbereich: **ja**. Im Monatsbereich ueber den `millis()`-Wraparound (~49,7 Tage) hinweg: **nicht zuverlaessig**, solange `STAB-01` nicht behoben ist.

---

## 2. Befunde

### 🔴 Critical

#### [STAB-01] Systemisches `millis()`-Overflow-Muster in der gesamten Loop-Aufgabensteuerung

**Datei/Zeile (Auswahl):**
[esp32_main.cpp:2458](src/esp32/esp32_main.cpp#L2458) ·
[esp32_main.cpp:2943](src/esp32/esp32_main.cpp#L2943) ·
[esp32_main.cpp:3020](src/esp32/esp32_main.cpp#L3020) ·
[esp32_main.cpp:3073](src/esp32/esp32_main.cpp#L3073) ·
[esp32_main.cpp:3213](src/esp32/esp32_main.cpp#L3213) ·
[esp32_main.cpp:3442](src/esp32/esp32_main.cpp#L3442) ·
[loop_functions.cpp:3382](src/loop_functions.cpp#L3382) ·
[lora_functions.cpp:1583](src/lora_functions.cpp#L1583) ·
[clock.cpp:72](src/clock.cpp#L72)

**Beweis:**
```cpp
// esp32_main.cpp:2458  (NTP-Sync alle 15 min)
if((updateTimeClient + 1000 * 60 * 15) < millis() || updateTimeClient == 0)
// esp32_main.cpp:2943  (Positions-Versand)
if (((posinfo_timer + (posinfo_interval * 1000)) < millis()) || ...)
// esp32_main.cpp:3442  (Heartbeat)
if ((hb_timer + (HEARTBEAT_INTERVAL * 1000)) < millis())
// esp32_main.cpp:3213  (Heap-Monitor)
if ((heapMonTimer + 60000) < millis())
// lora_functions.cpp:1583 (Track->MeshCom alle 5 min)
if(millis() > track_to_meshcom_timer + 1000 * 60 * 5)
// clock.cpp:72  (Minuten-Tick der Uhr)
if (millis() > u32Next_m)
```
Alle Timer-Variablen sind `unsigned long` (z. B. `loop_functions.cpp:435/437/458/461`, `esp32_main.cpp:468`).

**Warum kritisch fuer Dauerbetrieb:** `millis()` ist ein `uint32_t` und laeuft nach **~49,71 Tagen** ueber (zurueck auf 0). Beim Muster `(timer + intervall) < millis()` passieren zwei Fehlerfaelle:
1. **`timer + intervall` ueberlaeuft** (kurz vor dem Wrap): die Summe wird klein, der Vergleich ist sofort wahr → die Aufgabe **feuert in jeder Loop-Iteration** (Positions-/Heartbeat-/Telemetrie-Flut ins Mesh, NTP-Hammering, Heap-Log-Spam).
2. **`millis()` wrappt, `timer` aber nicht** (kurz nach dem Wrap): der Vergleich bleibt fuer fast die volle Periode falsch → die Aufgabe **steht bis zu ~49 Tage still** (keine NTP-Zeit mehr, kein Heartbeat → Knoten verschwindet aus der MeshMap, Uhr in `clock.cpp` bleibt stehen).

Da das **alle** periodischen Loop-Aufgaben gleichzeitig betrifft, ist der Knoten rund um den Wraparound funktional unbrauchbar, bis ein Neustart `millis()` und alle Timer zuruecksetzt — exakt das vom Anwender unerwuenschte „taegliche Neustarten" bei langer Uptime.

**Quelle:** Reasoning (cppcheck erkennt dieses semantische Muster nicht; bestaetigt durch fruehere Code-Audit-Notiz STAB-05 in `code-audit-20260508.md`).

**Fix-Vorschlag (NICHT angewandt):** Durchgaengig auf das ueberlaufsichere Differenz-Muster umstellen:
```cpp
if ((uint32_t)(millis() - timer) >= intervall) { ... timer = millis(); }
```
Diese Form ist gegen den Wrap immun, weil die vorzeichenlose Subtraktion modulo 2^32 rechnet. Die WP-Plattform (`Platforms/WirelessPaper/power_controls.cpp:17/33`) macht es bereits korrekt (`while (millis() - start < 50)`) und kann als Muster dienen. Empfehlung: zuerst die im Sekunden-/Minutentakt feuernden Pfade (NTP 2458, Heartbeat 3442, Position 2943, Telemetrie 3073, `clock.cpp:72`) umstellen.

---

### 🟠 High

#### [STAB-02] 8× `while(true);` als Fehlerbehandlung in der LoRa-Konfiguration (Boot-Hang)

**Datei/Zeile:**
[esp32_main.cpp:1350](src/esp32/esp32_main.cpp#L1350),
[1357](src/esp32/esp32_main.cpp#L1357),
[1365](src/esp32/esp32_main.cpp#L1365),
[1375](src/esp32/esp32_main.cpp#L1375),
[1412](src/esp32/esp32_main.cpp#L1412),
[1452](src/esp32/esp32_main.cpp#L1452),
[1485](src/esp32/esp32_main.cpp#L1485),
[1511](src/esp32/esp32_main.cpp#L1511)

**Beweis:**
```cpp
if (radio.setOutputPower(tx_power) == RADIOLIB_ERR_INVALID_OUTPUT_POWER) {
    Serial.println(F("Selected output power is invalid for this module!"));
    while (true);        // <-- Endlosschleife ohne Ausstieg
}
if (radio.setCurrentLimit(CURRENT_LIMIT) == RADIOLIB_ERR_INVALID_CURRENT_LIMIT) {
    Serial.println(F("Selected current limit is invalid for this module!"));
    while (true);
}
```
Die Bloecke liegen alle in `esp32setup()` (Funktionsbeginn `esp32_main.cpp:587`).

**Warum kritisch fuer Dauerbetrieb:** Schlaegt eine RadioLib-Konfiguration fehl (z. B. ungueltige Frequenz/Power durch korrupte NVS-Settings, SPI-Glitch beim Boot, marginaler SX1262 nach Brownout), bleibt die Firmware in `while(true);` haengen. Da kein Task-Watchdog konfiguriert ist (siehe STAB-04), gibt es **keinen automatischen Reset** — das Geraet ist tot und reagiert auf nichts mehr ausser manuellem Power-Cycle. Der Pfad ist nur beim Boot/Setup erreichbar (kein Laufzeit-Re-Config-Caller gefunden), daher **High** statt Critical: kein spontaner Absturz im laufenden Betrieb, aber ein verkappter Brownout-/Reset-Loop-Faengt.

**Quelle:** Reasoning (grep `while(true)` + Kontextpruefung der umschliessenden Funktion).

**Fix-Vorschlag (NICHT angewandt):** `while(true);` durch ein definiertes Recovery ersetzen — z. B. nach kurzer Wartezeit `ESP.restart()` (sauberer Reboot statt stillem Hang), idealerweise mit persistentem Fehlerzaehler/Safe-Mode, damit kein Endlos-Reboot-Loop entsteht. Alternativ den Default-Parameter setzen und mit Warnung weiterlaufen, statt zu blockieren.

---

#### [BND-01] `snprintf` mit zu grossem Groessen-Argument schreibt potenziell ueber `value[40]`

**Datei/Zeile:** [web_functions.cpp:1657](src/web_functions/web_functions.cpp#L1657)

**Beweis:**
```cpp
char value[40];                                    // web_functions.cpp:1598
...
snprintf(value, 100, "%s", meshcom_settings.node_mcp17t[io]);   // Zeile 1657 — Groesse 100 > Puffer 40
```
`node_mcp17t` ist `char[16][16]` (`esp32/esp32_flash.h:82`), die Quelle also max. 15 Zeichen — **heute** kein realer Ueberlauf. Das `size`-Argument `100` ist aber 2,5× groesser als der Zielpuffer `value[40]`.

**Warum kritisch fuer Dauerbetrieb:** `snprintf` darf laut der angegebenen Groesse bis zu 99 Bytes + NUL nach `value` schreiben. Solange `node_mcp17t[io]` ≤ 39 Zeichen bleibt, passiert nichts — aber die Schutzfunktion von `snprintf` (Begrenzung auf den Puffer) ist **ausgehebelt**. Wird `node_mcp17t` jemals vergroessert oder der Wert ueber einen anderen Pfad ungueltig terminiert, ueberschreibt dieser Aufruf benachbarten Stack (`onclick[100]`, `caption[40]`, Ruecksprungadresse) → Stack-Corruption → Crash/Reboot bei jedem Aufruf der MCP-IO-Webseite. cppcheck meldet hier hart `bufferAccessOutOfBounds (error)`.

**Quelle:** cppcheck 2.20 (`web_functions.cpp:1657 Buffer is accessed out of bounds`) — am Code verifiziert.

**Fix-Vorschlag (NICHT angewandt):** Groessen-Argument an den Puffer binden: `snprintf(value, sizeof(value), "%s", …)`. Konsistent mit den korrekten Aufrufen weiter oben (`snprintf(id, 40, …)`).

---

#### [NET-01] Blockierendes `WiFi.hostByName()` (DNS) im NTP-/Reconnect-Pfad

**Datei/Zeile:** [udp_functions.cpp:868](src/udp_functions.cpp#L868),
[885](src/udp_functions.cpp#L885),
[915](src/udp_functions.cpp#L915),
[923](src/udp_functions.cpp#L923),
[930](src/udp_functions.cpp#L930) sowie `udpUpdateTimeClient()` → `timeClient.forceUpdate()` [udp_functions.cpp:731](src/udp_functions.cpp#L731)

**Beweis:**
```cpp
WiFi.hostByName("meshcom.hamnet.cloud", node_hostip);       // blockierende DNS-Aufloesung
...
WiFi.hostByName(meshcom_settings.node_ntp, ntpServer);
// und im 15-min-NTP-Zyklus:
if(!timeClient.forceUpdate()) { ... return "none"; }        // blockierendes UDP-Warten
```

**Warum kritisch fuer Dauerbetrieb:** `WiFi.hostByName()` blockiert bis zum DNS-Timeout (Default mehrere Sekunden), `timeClient.forceUpdate()` wartet synchron auf die NTP-Antwort. Beide laufen im Haupt-Loop (kein eigener Task). Bei DNS-/NTP-Stoerung (Hamnet-Ausfall, Server weg) blockiert der Loop sekundenlang pro Zyklus; LoRa-RX/TX, Display und Button reagieren in dieser Zeit nicht. Ohne konfigurierten Task-Watchdog (STAB-04) fuehrt das nicht direkt zum Reset, aber zu spuerbaren Haengern und im Extremfall (kaskadierende Timeouts) zu verpasstem RX. Realistisches Sonderbedingungs-Szenario → **High**.

**Quelle:** Reasoning (grep blockierende Netz-Calls + Aufrufkontext im Loop).

**Fix-Vorschlag (NICHT angewandt):** DNS-Ergebnisse cachen und nur selten/bei Reconnect aufloesen; NTP-Update in einen separaten, zeitlich gedeckelten Task auslagern oder einen nicht-blockierenden NTP-Client mit kurzem Timeout verwenden. `forceUpdate()` nicht in jedem Fehlerfall erzwingen.

> Hinweis: Das ebenfalls verdaechtig aussehende `while(WiFi.status() != WL_CONNECTED)` in `udp_functions.cpp:651` ist **kein** Hang — die Schleife `return`t bereits in der ersten Iteration (siehe „Als sauber bewertet").

---

### 🟡 Medium

#### [BND-02] LoRa-RX-`size` auf dem WP-Pfad nicht explizit auf `UDP_TX_BUF_SIZE` geklemmt

**Datei/Zeile:** [lora_functions.cpp:386](src/lora_functions.cpp#L386) (Klemmung nur unter `#if defined BOARD_RAK4630`, Zeilen 312/322 — auf WP **nicht** kompiliert)

**Beweis:**
```cpp
// nur fuer RAK4630 wird size geklemmt:
#if defined BOARD_RAK4630
    uint16_t rxSize = (size <= UDP_TX_BUF_SIZE) ? size : UDP_TX_BUF_SIZE;   // Zeile 312
    ...
    size = rxSize;                                                          // Zeile 323
#endif
...
memcpy(RcvBuffer, payload, size);   // Zeile 386 — auf WP mit ungeklemmtem size
```

**Warum kritisch fuer Dauerbetrieb:** `RcvBuffer` ist `uint8_t[UDP_TX_BUF_SIZE * 2]` = 510 Bytes (`loop_functions.cpp:343`), `UDP_TX_BUF_SIZE` = 255 (`configuration_global.h:53`). In der Praxis **kein** Ueberlauf, weil `checkRX()` mit `radio.readData(payload, ibytes=255)` einliest (`esp32_main.cpp:3667/3669/3732`) und RadioLib damit auf 255 deckelt — und 255 < 510. Der Schutz haengt aber allein am Radio-Treiber; eine fehlende explizite Obergrenze direkt vor dem `memcpy` auf Fremddaten (LoRa-Payload) ist ein latentes Robustheitsrisiko. Defense-in-depth → **Medium**.

**Quelle:** Reasoning (Cross-Check Payload-Pfad WP vs. RAK).

**Fix-Vorschlag (NICHT angewandt):** Die Klemmung `size = min(size, UDP_TX_BUF_SIZE)` vor Zeile 386 boardunabhaengig machen (aus dem `#if BOARD_RAK4630` herausziehen).

---

#### [BND-03] `handleACK()` liest 12 Bytes aus `payload` ohne `size`-Pruefung (Over-Read)

**Datei/Zeile:** [lora_functions.cpp:212](src/lora_functions.cpp#L212)

**Beweis:**
```cpp
static bool handleACK(uint8_t *payload, uint16_t size, ...) {
    if(payload[0] != MSG_TYPE_ACK) return false;
    uint8_t print_buff[30];
    ...
    memcpy(print_buff, payload, 12);   // liest payload[0..11] ohne Pruefung size >= 12
```

**Warum kritisch fuer Dauerbetrieb:** Trifft ein ACK-Frame mit `size < 12` ein, liest `memcpy` ueber das Ende der gueltigen Payload hinaus. Der Quellpuffer in `checkRX()` ist `uint8_t payload[UDP_TX_BUF_SIZE+10]` (Stack, 265 Bytes), das Over-Read bleibt also **innerhalb** des allokierten Arrays — kein Crash, aber Auswertung von Garbage (`msg_id`, Server-Flag). Korrektheits-/Robustheitsmangel, kein direkter Reboot → **Medium**.

**Quelle:** cppcheck-Umfeld + Reasoning (bekannt aus `code-audit-20260508.md` BND-03).

**Fix-Vorschlag (NICHT angewandt):** Frueh `if(size < 12) return false;` einfuegen, bevor 12 Bytes kopiert werden.

---

#### [STAB-04] Kein Task-Watchdog und keine Reset-Reason-Behandlung

**Datei/Zeile:** projektweit (kein `esp_task_wdt_add` / `esp_task_wdt_reset` / `esp_reset_reason` in den In-Scope-Dateien gefunden)

**Beweis:** `grep -rn "esp_task_wdt\|esp_reset_reason"` liefert im WP-Scope keine Treffer.

**Warum kritisch fuer Dauerbetrieb:** Ohne Task-Watchdog fuehrt **kein** der blockierenden Pfade (STAB-02 `while(true)`, NET-01 DNS/NTP) zu einer automatischen Erholung — ein Hang bleibt ein Hang bis zum manuellen Power-Cycle. Genau das erzwingt manuelle Neustarts. Zusaetzlich fehlt jede Diagnose, **warum** ein eventueller Reboot passiert ist (Brownout? Panic? WDT?), was Felddiagnose erschwert. Selten sofort crash-ausloesend, aber strukturell destabilisierend → **Medium**.

**Quelle:** Reasoning / Abwesenheits-Befund (deckt sich mit STAB-01/STAB-04 in `code-audit-20260508.md`).

**Fix-Vorschlag (NICHT angewandt):** Task-WDT aktivieren (`esp_task_wdt_init` + `esp_task_wdt_add(NULL)` im Loop-Task, periodischer `esp_task_wdt_reset()`), `while(true);` durch `esp_restart()` ersetzen, beim Boot `esp_reset_reason()` loggen und einen persistenten Crash-Zaehler fuehren.

---

#### [MEM-01] `Clock::boAlarmValid_m` im Konstruktor nicht initialisiert

**Datei/Zeile:** [clock.cpp:53](src/clock.cpp#L53)

**Beweis:**
```cpp
Clock::Clock() {
    szDateStr_m[0] = '\0';
    u32Next_m = u32Start_m = 0ul;
    SetAlarmDefaults();
    SetClockDefaults();
}   // boAlarmValid_m wird hier nie gesetzt
```

**Warum kritisch fuer Dauerbetrieb:** `boAlarmValid_m` startet mit unbestimmtem Wert. Wird es im Alarm-/Zeitpfad ausgewertet, bevor es das erste Mal gesetzt wird, ist das Verhalten nicht-deterministisch (Alarm faelschlich gueltig/ungueltig). Da `Clock` ein langlebiges Singleton ist, ist das ein einmaliger Init-Effekt, kein wiederkehrender Crash → **Medium**.

**Quelle:** cppcheck 2.20 (`clock.cpp:53 uninitMemberVar`) — am Code verifiziert (Konstruktor setzt das Member nachweislich nicht).

**Fix-Vorschlag (NICHT angewandt):** `boAlarmValid_m = false;` (bzw. den definierten Default) explizit im Konstruktor oder in `SetAlarmDefaults()` setzen.

---

### 🟢 Low

#### [LOG-01] Redundante, immer-wahre innere `if(bLORADEBUG)`-Verschachtelung

**Datei/Zeile:** [lora_functions.cpp:1260](src/lora_functions.cpp#L1260),
[1273](src/lora_functions.cpp#L1273),
[1425](src/lora_functions.cpp#L1425),
[1550](src/lora_functions.cpp#L1550),
[1797](src/lora_functions.cpp#L1797),
[1808](src/lora_functions.cpp#L1808)

**Beweis:**
```cpp
if(bLORADEBUG) {
    uint32_t dup_id = ...;
    if(bLORADEBUG)               // identische, immer wahre innere Bedingung
        Serial.printf("[MC-DBG] RX_DEDUP_DUP ...");
}
```

**Warum (k)ein Stabilitaetsrisiko:** Reiner Code-Smell. Die innere Bedingung ist im Debug-Zweig redundant, hat aber keine Laufzeit-/Speicher-Folge. cppcheck meldet `identicalInnerCondition (warning)` — **kein** Bug. Nur Aufraeumkandidat.

**Quelle:** cppcheck 2.20 — verifiziert als harmlos.

**Fix-Vorschlag (NICHT angewandt):** Innere `if(bLORADEBUG)` entfernen.

---

#### [MEM-02] `aprs_functions.cpp:1241` `clat`/`clon` — cppcheck-Fehlalarm, aber knappe Init-Logik

**Datei/Zeile:** [aprs_functions.cpp:1241](src/aprs_functions.cpp#L1241)

**Beweis:** Die `for(int ig=1; ig<3; ig++)`-Schleife (Zeile 1181) fuellt `clat[0..3]` (ig==1) **und** `clon[0..3]` (ig==2), bevor `snprintf` beide nutzt:
```cpp
for(int ig=1;ig<3;ig++) {
    ...
    if(ig == 1) { clat[0]=...; clat[1]=...; clat[2]=...; clat[3]=...; }
    else        { clon[0]=...; clon[1]=...; clon[2]=...; clon[3]=...; }
}
snprintf(msg_start, ..., clat[0],...,clon[0],...);   // beide initialisiert
```

**Warum kein Stabilitaetsrisiko:** cppcheck verfolgt nur einen Schleifendurchlauf („Assuming condition is false/true") und meldet daher faelschlich `uninitvar`. In Wahrheit deckt die Schleife beide Faelle ab. **Fehlalarm** — als Low gefuehrt, weil die zweifache Befuellung ueber eine `if/else`-in-Schleife leicht uebersehbar ist. Kein Fix noetig.

**Quelle:** cppcheck 2.20 (`uninitvar`) — als Fehlalarm verifiziert.

---

#### [LOG-02] `localtime()` statt `localtime_r()`

**Datei/Zeile:** [time_functions.cpp:96](src/time_functions.cpp#L96)

**Beweis:**
```cpp
strftime(buffer, sizeof(buffer), "%Y.%m.%d %H:%M:%S", localtime(&unix));
```

**Warum kein akutes Risiko:** `localtime()` nutzt einen statischen, nicht-reentranten Puffer. Im WP-Build wird es aus dem Single-Loop-Kontext aufgerufen (keine konkurrierende Task gefunden, die `localtime` parallel nutzt), daher praktisch unkritisch → **Low**.

**Fix-Vorschlag (NICHT angewandt):** Auf `localtime_r(&unix, &tm_buf)` umstellen (so wie `clock.cpp` es bereits tut).

---

#### [STYLE-01] `net_console.cpp` Old-Style-C-Casts + `memcmp` ueber Passwortlaenge

**Datei/Zeile:** [net_console.cpp:326](src/net_console.cpp#L326),
[358](src/net_console.cpp#L358),
[174](src/net_console.cpp#L174)

**Beweis:**
```cpp
::bind(s_listen_fd, (struct sockaddr*)&addr, sizeof(addr))   // old-style cast
if(memcmp(respBuf, s_password, strlen(s_password)) != 0)     // liest min(respBuf, pw-len)
```

**Warum kein Stabilitaetsrisiko:** Die C-Casts sind Stil (cppcheck `dangerousTypeCast`), kein Bug. `respBuf[72]` ist NUL-initialisiert (`= {0}`); `memcmp` ueber `strlen(s_password)` bleibt innerhalb des 72-Byte-Puffers, solange das Passwort < 72 Zeichen ist — kein Over-Read. `new AuthArgs` wird in `authTask` auf **allen** Pfaden via `delete a;` (Zeile 127) freigegeben → **kein Leak**. Nur Code-Smell → **Low**.

**Quelle:** cppcheck 2.20 + Reasoning (Leak-Pfad und Puffergrenzen verifiziert).

---

## 3. Anhang: cppcheck-Rohbefunde (getrennt von verifizierten Befunden)

Quelle: `dist/audit/cppcheck-raw.txt` (cppcheck 2.20, WP-Defines). Triage-Ergebnis:

### Als echte/relevante Befunde uebernommen
| cppcheck-Zeile | Befund-ID | Bewertung |
|----------------|-----------|-----------|
| `web_functions.cpp:1657 bufferAccessOutOfBounds` | **BND-01** 🟠 | Echt: `snprintf(value,100,…)` in `value[40]`. |
| `clock.cpp:53 uninitMemberVar (boAlarmValid_m)` | **MEM-01** 🟡 | Echt: Member im Ctor nicht gesetzt. |
| `aprs_functions.cpp:1241 uninitvar clat/clon` | **MEM-02** 🟢 | **Fehlalarm**: `for`-Schleife fuellt beide Arrays. |
| `lora_functions.cpp:1260/1273/1425/1550/1797/1808 identicalInnerCondition` | **LOG-01** 🟢 | Echt, aber harmlos (redundantes `if(bLORADEBUG)`). |
| `net_console.cpp:326/358 dangerousTypeCast` | **STYLE-01** 🟢 | Stil, kein Bug. |

### Als Tool-Rauschen eingestuft (KEIN Bug)
| cppcheck-Zeile | Begruendung |
|----------------|-------------|
| `batt_functions.cpp:166 preprocessorErrorDirective "#error … configured for ESP32"` | Beabsichtigter Praeprozessor-Guard fuer falsche Target-Konfiguration. Kein Laufzeitcode. |
| `Fonts/FreeMonoBold9pt7b.h:1 unknownMacro (PROGMEM)` | cppcheck kennt das `PROGMEM`-Makro nicht — reines Parsing-Artefakt. |
| `Displays/BaseDisplay/{setup,window,full}.* uninitMemberVar` (40+ Treffer) | Vendor-GFX-Lib (`src/Displays/*`); Member werden ueber Setter/`begin()` initialisiert. Out-of-MeshCom-Kern. |
| `Displays/BaseDisplay/SD.cpp:320 identicalInnerCondition`, `:368 sizeofwithnumericparameter`, `:368 strcpy` | Vendor-SD-Pfad, auf WP funktional irrelevant (kein SD). |
| `GFX_Root/GFX.cpp:1005…1332 dangerousTypeCast` (6×) | Vendor-Adafruit-GFX `pgm_read_byte`-Casts. |
| `Regexp.h:79/80/82 uninitMemberVar` (15×) | Vendor-Regexp-Lib; Member werden vor Nutzung gesetzt. |
| `esp32_main.cpp:1601 uselessCallsSubstr (advName)` | Performance-Hinweis (`substr(0,26)` auf sich selbst), kein Stabilitaetsrisiko. |

---

## 4. Als sauber bewertet — geprüfte Hot-Paths ✅

| Pfad / Datei | Befund |
|--------------|--------|
| **`Platforms/WirelessPaper/power_controls.cpp`** | Wartet ueberlaufsicher (`while (millis() - start < N) yield()`), kein Busy-Block ohne `yield`. **Sauber.** |
| **`Platforms/WirelessPaper/spi.cpp`** | `new SPIClass(HSPI)` ist Einmal-Init beim Boot, kein wiederkehrendes `new` → **kein Leak.** |
| **WP-Batteriemessung `batt_functions.cpp:375-392`** | `ADC_CTRL` active-LOW, `delay(10)` + 8× ADC-Mittelung, gedeckelt; Teiler wird wieder getrennt. **Sauber.** |
| **OnRxDone-Dispatch (ESP32/WP)** | RX laeuft NICHT im ISR: `setFlagReceive` (`esp32_main.cpp:473`) setzt nur ein Flag; `OnRxDone` wird aus `checkRX()` im **Task-Kontext** aufgerufen (`esp32_main.cpp:3732`). Serial.printf/`iWrite`-Zugriffe damit nicht ISR-kritisch. |
| **`checkRX()` Laengen-Deckelung** | `radio.readData(payload, ibytes=UDP_TX_BUF_SIZE)` begrenzt `size` ≤ 255; `RcvBuffer[510]` ist gross genug → **kein realer Overflow** trotz BND-02. |
| **`net_console.cpp` `authTask`** | `new AuthArgs` wird auf allen Exit-Pfaden via `delete a;` freigegeben (Zeile 127); 30-s-Socket-Timeout gesetzt → **kein Leak, kein unbegrenztes Warten.** |
| **`udp_functions.cpp:651` `while(WiFi.status()…)`** | **Kein Hang** — `return false;` in der ersten Iteration; effektiv nicht-blockierend. |
| **`time_functions.cpp:65/76` `while(days >= …)`** | Bounded Date-Math (dekrementiert `days`), terminiert garantiert. **Sauber.** |
| **`command_functions.cpp:510/537/579` `ESP.restart()`** | Bewusste, benutzer-ausgeloeste Reboots (`--reboot`), keine ungewollten Reset-Pfade. |
| **`csma_*`-Timeout / RX-Dedup-Ring** | Feste Ringgroessen (`MAX_DEDUP_RING`, `MAX_RING`), kein unbegrenztes Wachstum gefunden. |

---

> **Hinweis:** Im Rahmen dieses Audits wurde **kein Code veraendert** — es wurde ausschliesslich diese Report-Datei geschrieben. Alle Fix-Vorschlaege sind rein beschreibend und nicht angewandt.
