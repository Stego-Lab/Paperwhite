# Sicherheitsaudit & Fehleranalyse: Display-Freeze nach Akku-leer / „AKKU LOW" nie sichtbar

**Datum:** 2026-06-12
**Board:** Heltec Wireless Paper (ESP32-S3FN8, SX1262, 2.13" E-Ink V1.1/V1.2)
**Untersuchter Stand:** Working Tree, Branch `wireless-paper-v11-v12-fixes` @ dev `871da1a` + lokale (uncommitted) WP-Änderungen; geflasht ist `wireless-paper-preview` (mit `WP_PREVIEW`)
**Methode:** Multi-Agent-Audit (9 parallele Analyse-Perspektiven: 5 Symptom-Hypothesen + 4 Sicherheits-Lenses), ~2,4 Mio. Analyse-Tokens, 75 Roh-Findings, anschließend manuell konsolidiert. Die Kern-Mechanismen (K1–K3, AKKU-LOW-Kette) wurden zusätzlich von Hand im Code nachvollzogen; Findings zu Library-Interna (Busy-Polarität, Panel-Sequenzen) tragen Restunsicherheit (siehe „Limitierungen").

> ⚠️ **Ursprünglich reine Analyse.** Seit 2026-06-17 sind Teile umgesetzt (nur im `WP_PREVIEW`-Build) — siehe **Umsetzungsstand** direkt unten.

---

## 0. Umsetzungsstand (Stand 2026-06-17, alles `WP_PREVIEW`-gated)

| Finding | Maßnahme | Status |
|---|---|---|
| **K1** Deepsleep ohne Wakeup | **TK1** — `esp_sleep_enable_ext1_wakeup(GPIO0/PRG)` + Hinweis „Wake: RST/PRG" | ✅ umgesetzt · **am Gerät getestet** |
| **K7** Sleep-Strom (mA statt µA) | **TK5** — `Platform::prepareToSleep()` (SX1262→SLEEP, VEXT aus) vor Deepsleep | ✅ umgesetzt · **am Gerät getestet** (LoRa nach Wakeup OK) |
| **K2** `--display off` persistiert Flash | **TK2** — im Low-Voltage-Pfad weggelassen (kein `save_settings()`) | ✅ umgesetzt |
| **K12** 2 Refreshes + Flash-Write bei Tiefstspannung | **TK2** — nur noch 1 Refresh, kein Flash-Write | ✅ umgesetzt |
| **K3** Deepsleep-Boot-Schleife | **TK4** — `wpBattIsCharging()` (Lade-Trend über Ringpuffer) unterdrückt Deepsleep | ✅ umgesetzt |
| **K25/K38** AKKU-LOW grau (Doppel-Refresh) | `wpShowDeepSleep`: `clear()` → **`clearMemory()`** (nur 1 Refresh) | ✅ umgesetzt · **bei voller Spannung getestet** (12 Werte sichtbar) |
| **K14** Serial-Parser (Bounds/strlen/Timeout) | **FSM-Parser** (OE3WAS) für Serial **+ Telnet**: binärsicher, 90-s-Timeout, Befehls-Vereinzelung, `cBuf`-Overflow gefixt | ✅ umgesetzt · **in Erprobung** |
| **K13** Button-Pull-up | — | ❌ **FALSE POSITIVE** (Wolfgang): GPIO0-Strapping hat externen Pull-up, `INPUT` ist korrekt |
| **K4** Chip-ID-Plausibilität (Endlos-Hänger) | — | ⬜ offen (Display-Library) |
| **K5/K11/K16** `wait()` ohne Timeout | — | ⬜ offen (Display-Library) |
| **K6** Panel-`wait()` vor Sleep (V1.1 0x02) | teilweise via `delay(100)` vor `prepareToSleep` | 🟡 teil-gemildert |
| **K8** `is_receiving`/`tx_is_active`-Hänger | — | ⬜ offen (geteilter LoRa-Pfad, „TK3") |
| **K9** millis()-Wrap (49,7 Tage) | — | ⬜ offen (allgemein, 31 Stellen) |
| **K10** `--maxv` Division-durch-0 | — | ⬜ offen (allgemein) |
| **K11/K15** Flash-Hygiene | upstream geht selbst Richtung weniger Persistenz (dev: `node_persist`-Defaults `false`) | 🟡 teils upstream |

**Symptom-Status:** Die heutigen Maßnahmen (TK1/TK5 getestet, AKKU-LOW-Fix getestet) adressieren die **Haupt-Ursachenketten** des Display-Freeze (K1, K7, K2/K12, K3) sowie die graue AKKU-LOW-Anzeige (K25). **K8** (LoRa-Gate, erklärt „AKKU LOW nie erreicht") bleibt offen — der nächste symptomrelevante Kandidat.

---

## 1. Kurzfassung

**Symptom:** Akku wird leer → Firmware geht in Low-Voltage-Deepsleep. Wird danach wieder Spannung angelegt, „startet" das Wireless Paper neu — aber das Display bleibt stehen. Außerdem war die „AKKU LOW"-Anzeige (Panel zeigt „AKKU LOW" + letzte Spannungswerte) **noch nie** zu sehen.

**Befund:** Es gibt nicht *eine* Ursache, sondern **drei zusammenwirkende Ursachenketten** — plus eine vierte, die erklärt, warum „AKKU LOW" nie erscheint:

| # | Ursache | Schwere | Betrifft |
|---|---------|---------|----------|
| K1 | `esp_deep_sleep_start()` **ohne jede Wakeup-Quelle** → USB-Anstecken weckt den Chip NICHT; es gibt gar keinen Neustart, das E-Ink hält nur das alte Bild | 🔴 critical | Original + Preview |
| K2 | Low-Voltage-Pfad **persistiert `--display off` in den Flash** → nach echtem Neustart bootet das Original mit Display aus (im lokalen Stand durch Boot-Fix entschärft, Flash-Bit bleibt aber stehen) | 🔴 critical (Original) / 🟡 medium (lokal) | Original ungefixt |
| K3 | **Deepsleep-Boot-Schleife**: keine USB-/Ladeerkennung; nach Reset mit noch leerem Akku fällt das Gerät nach ~25 s (Original) bzw. ~47 s (Preview/Glitch-Filter) erneut in den Deepsleep | 🟠 high | Original + Preview |
| K8 | **`is_receiving`/`tx_is_active` können dauerhaft hängenbleiben** → `read_batt()` läuft nie mehr → der AKKU-LOW-Pfad wird **nie erreicht**, der Akku entlädt bis zum harten Brownout-Tod — Display friert mit dem letzten *normalen* Bild ein, ohne je „AKKU LOW" zu zeigen | 🟠 high | Original + Preview |

**Die wahrscheinlichste Erklärung des beobachteten Ablaufs:**
Entweder (a) der AKKU-LOW-Pfad wurde nie erreicht (K8 oder Brownout vor Filter-Konvergenz, siehe Kap. 3) und das Gerät starb hart mit eingefrorenem Normalbild — oder (b) er wurde erreicht, das Gerät schlief, und beim USB-Anstecken gab es schlicht **keinen Neustart** (K1): der Chip schläft weiter, das bistabile E-Ink hält das Bild, es sieht nur aus wie „neu gestartet und eingefroren". Ein echter Neustart passiert erst durch RESET-Taste oder komplette Tiefentladung (Schutz-IC trennt) + Wiederanlegen.

---

## 2. Ursachenketten im Detail

### K1 (critical): Deepsleep ohne Wakeup-Quelle — „der Neustart, der keiner ist"

**Code:** [command_functions.cpp:804-811](../src/command_functions.cpp#L804-L811)

```cpp
#if defined(BOARD_WIRELESS_PAPER)
wpShowDeepSleep();
#endif
#if not defined(BOARD_RAK4630)
esp_deep_sleep_start();          // <-- KEIN esp_sleep_enable_* davor!
#endif
```

- Im gesamten `src/`-Baum existiert für das WP **kein einziger** `esp_sleep_enable_*`-Aufruf (nur auskommentiertes Beispiel in `Platforms/WirelessPaper/power_controls.cpp:93`).
- Ein ESP32-S3 im Deepsleep ohne Wakeup-Quelle wacht **nur** durch EN/RST-Toggle oder kompletten Power-On-Reset auf.
- USB anstecken erzeugt **keinen** Reset: der CP2102 toggelt EN nur beim Öffnen eines seriellen Ports (DTR/RTS), ein Ladegerät nie. Solange der Akku den Chip noch versorgt (Low-Voltage-Schwelle ist 3,3 V — weit über der Schutzabschaltung ~2,5 V), bleibt der Chip im Schlaf, der Akku lädt nur.
- Folge: Das E-Ink zeigt unverändert das letzte Bild → wirkt exakt wie „neu gestartet, aber Display eingefroren", obwohl nie gebootet wurde. Der Code-Kommentar ([loop_functions.cpp:1788](../src/loop_functions.cpp#L1788) „Aufwecken per RESET") dokumentiert das sogar — der Anwender erfährt es aber nirgends.

**Änderungsvorschlag (V1):**
1. Vor `esp_deep_sleep_start()` den PRG-Button als Wakeup armieren: `esp_sleep_enable_ext1_wakeup(1ULL << GPIO_NUM_0, ESP_EXT1_WAKEUP_ANY_LOW)` (GPIO0 ist RTC-fähig) → Tastendruck weckt das Gerät.
2. Zusätzlich optional Timer-Wakeup (z. B. alle 5–15 min): nach dem Aufwachen Spannung prüfen — erholt (lädt/USB) → normal booten; weiter leer → sofort wieder schlafen (Verbrauch minimal).
3. Auf dem AKKU-LOW-/Sleep-Schirm den Hinweis ergänzen: **„Wake: RST/PRG drücken"**.

### K2 (critical im Original): `--display off` wird in den Flash persistiert

**Code-Kette:** [batt_functions.cpp:313](../src/batt_functions.cpp#L313) → [command_functions.cpp:747-763](../src/command_functions.cpp#L747-L763) → [esp32_main.cpp:741](../src/esp32/esp32_main.cpp#L741)

- Der Low-Voltage-Pfad ruft `commandAction("--display off")`. Das setzt nicht nur die RAM-Flags, sondern **persistiert `node_sset |= 0x0002` per `save_settings()`** — ein NVS-Flash-Write bei kritisch niedriger Spannung (Korruptionsrisiko, siehe K12).
- Beim nächsten echten Boot lädt `esp32_main.cpp:741` das Bit → `bDisplayOff = true` → `mainStartTimeLoop()` setzt `bDisplayIsOff = bDisplayOff` ([loop_functions.cpp:1856](../src/loop_functions.cpp#L1856)) → **alle** `sendDisplay*`-Pfade und `wpRefreshClock` brechen ab. Auf dem bistabilen E-Ink bleibt das letzte Bild für immer stehen = exakt das Symptom — **im Upstream-Original ungefixt**.
- Der lokale (uncommittete) WP-Boot-Fix ([esp32_main.cpp:742-749](../src/esp32/esp32_main.cpp#L742-L749), `bDisplayOff = false`) neutralisiert das auf unserem Build. **Aber:** das 0x0002-Bit bleibt im Flash stehen (inkonsistent: Phone-App/Web-Setup zeigen weiterhin „Display off"; ein Wechsel auf andere Firmware bootet wieder mit Display aus).

**Änderungsvorschläge (V2):**
1. **Boot-Fix committen/als PR einreichen** — löst das Symptom für alle WP-Nutzer des Originals.
2. Ursächlich sauberer: Im Low-Voltage-Pfad **kein** `commandAction("--display off")` verwenden, sondern nur die RAM-Flags setzen (`bDisplayOff/bDisplayIsOff = true`) — der Zustand muss den Reboot nicht überleben; spart zugleich den riskanten Flash-Write und einen E-Ink-Voll-Refresh (siehe K12).
3. Ergänzend im WP-Boot-Fix das Flash-Bit mitbereinigen: `meshcom_settings.node_sset &= ~0x0002;` (vor dem ohnehin laufenden `save_settings()` bei Z.738) → Flash-Zustand == RAM-Zustand.

### K3 (high): Deepsleep-Boot-Schleife — keine USB-/Ladeerkennung

**Code:** [batt_functions.cpp:219-241, 286-324](../src/batt_functions.cpp#L219-L324), Gate in [esp32_main.cpp:3132-3136](../src/esp32/esp32_main.cpp#L3132-L3136)

- Das WP hat **keinerlei** VBUS-/Ladeerkennung. Boot mit leerem, gerade ladendem Akku (z. B. 3,2 V Klemmenspannung):
  - `firstReading` seedet den Filter auf `fBattMax` (4,125 V) — gewollt („kein Deepsleep nach Reboot").
  - Mit alpha = 0,05 bei 2 Messungen/s fällt der gefilterte Wert nach ~44 Messungen (**~22 s**) unter 3,3 V.
  - Im **Preview** (geflasht!) halbiert der Glitch-Filter die Konvergenz (firstGlitch-Toggle verwirft jede zweite Messung außerhalb ±2 %): **~44–47 s**.
  - Danach CountDown = 6 (3 s) → erneuter Deepsleep inkl. erneutem Flash-Write — obwohl USB lädt.
- Ergebnis: Gerät bootet, zeigt kurz den Startscreen, fällt nach <1 min wieder in „AKKU LOW"-Deepsleep → wirkt wie eingefroren auf dem AKKU-LOW-Bild; je nach Ladestrom wiederholt sich das mehrfach.

**Änderungsvorschläge (V3):**
1. **Spannungs-Trend als Lade-Heuristik:** steigt `rawVoltage` über z. B. 30 s monoton → Akku lädt → Deepsleep unterdrücken, CountDown neu armieren.
2. CountDown erst aktivieren, wenn der Filter konvergiert ist (z. B. `|raw − filtered| < 2 %`) — die Abschalt-Entscheidung beruht dann auf echten Werten statt auf dem Seed-Artefakt.
3. Kombiniert mit V1/Timer-Wakeup: kurzer Mess-Boot alle 5–15 min statt Dauerschleife.

### K8 (high): `is_receiving`/`tx_is_active` bleiben hängen → Batteriemessung tot → AKKU-LOW-Pfad unerreichbar

**Code:** [esp32_main.cpp:2316-2330, 3649-3650, 3136](../src/esp32/esp32_main.cpp#L3136), [lora_functions.cpp:1616-1623](../src/lora_functions.cpp#L1616-L1623), TX-Watchdog [esp32_main.cpp:1997-2030](../src/esp32/esp32_main.cpp#L1997-L2030)

- `read_batt()` läuft nur bei `tx_is_active == false && is_receiving == false`.
- `is_receiving` wird im TX-Gate-IRQ-Poll gesetzt (Z.2322), aber auf ESP32 existiert **kein Pfad, der es garantiert zurücksetzt**: `checkRX` bricht bei gesetztem Flag sofort ab (Z.3649), `OnRxTimeout/OnRxError` sind RAK-only.
- `tx_is_active` bleibt im `lora_setchip_aprs()`-Fehler-Rollback und nach dem TX-Watchdog gesetzt.
- Folge: Batterie-/Prozentanzeige friert ein, der Low-Voltage-Schutz feuert **nie**, das Gerät läuft mit leerem Akku weiter bis zum Brownout/Schutz-IC-Cut — **ohne je „AKKU LOW" zu zeichnen.**

**Änderungsvorschläge (V4):**
1. Im `receiveFlag`-Handler vor `checkRX()` das Flag zurücksetzen (`is_receiving = false`; `checkRX` setzt es selbst wieder korrekt).
2. Im CSMA-Timeout-/Restart-Zweig nach `startReceive()` ebenfalls `is_receiving = false`.
3. Im `lora_setchip_aprs()`-Rollback und im TX-Watchdog `tx_is_active = false` ergänzen (symmetrisch zu den startTransmit-Fehlerpfaden).
4. Defensiv: das Batt-Gate mit einem Zeitlimit versehen — wenn `read_batt()` länger als z. B. 60 s nicht lief, trotzdem messen.

---

## 3. Warum die „AKKU LOW"-Anzeige noch nie zu sehen war

**Der Render-Code selbst ist fehlerfrei** — das wurde gezielt geprüft (Puffer, Modulo-Ringpuffer, Schriftposition, Reihenfolge der Flags; Findings F-58/F-66): Wenn `wpShowDeepSleep()` mit `bWpAkkuLow == true` aufgerufen wird, zeichnet es korrekt „AKKU LOW" + bis zu 10 Spannungswerte und macht einen Voll-Refresh, **bevor** `esp_deep_sleep_start()` folgt. `bDisplayIsOff` stört dabei nicht (wpShowDeepSleep prüft es nicht).

Dass die Anzeige real nie erschien, hat demnach **vorgelagerte** Gründe — in absteigender Wahrscheinlichkeit:

1. **Pfad nie erreicht (K8):** hängt `is_receiving`/`tx_is_active`, wird `read_batt()` nie mehr aufgerufen → kein CountDown, kein Deepsleep, kein AKKU-LOW-Bild. Das Gerät stirbt später hart am Brownout — mit dem letzten *normalen* Displaybild. Passt zugleich perfekt zum „eingefrorenen" Display.
2. **Brownout mitten in der Abschalt-Sequenz (K12):** Der Pfad macht bei Tiefstspannung erst `delay(1000)`, dann `--display off` (= NVS-Flash-Write **und** ein E-Ink-Voll-Refresh über `sendDisplayHead`), dann erst `wpShowDeepSleep` (zweiter Voll-Refresh). E-Ink-Refreshes ziehen Boost-Converter-Stromspitzen — bricht die Spannung dabei unter ~2,44 V (Brownout-Level), resettet der Chip, **bevor** „AKKU LOW" je auf dem Panel landet.
3. **Filter-Trägheit am LiPo-Knick:** Am Entladeschluss fällt die Spannung schnell; der EMA (alpha 0,05) hinkt nach, im Preview zusätzlich durch den Glitch-Filter verlangsamt. Unter Last (LoRa-TX, WiFi) kann die echte Spannung den Brownout erreichen, bevor der *gefilterte* Wert die 3,3-V-Schwelle 6× in Folge unterschreitet.
4. **Frühere Firmware-Stände:** Das Feature kam erst mit dem Build vom 11.06. auf das Gerät; frühere Akku-leer-Ereignisse liefen mit FW ohne AKKU-LOW-Anzeige.

**Test-Vorschlag, um die Anzeige gezielt zu provozieren (ohne auf leeren Akku zu warten):**
Temporärer Testbuild mit `BAT_MIN_VOLTAGE 3.9` (nur Preview) bei vollem Akku — dann durchläuft das Gerät den kompletten AKKU-LOW-Pfad bei gesunder Spannung, und man sieht sofort, ob „AKKU LOW" + Werte gezeichnet werden. Zusätzlich `esp_reset_reason()` beim Boot loggen (war der letzte Reset ein Brownout?).

---

## 4. Weitere Findings (Sicherheits-/Stabilitätsaudit)

### 🟠 High

| ID | Fund | Ort | Vorschlag |
|----|------|-----|-----------|
| K4 | **Panel-Chip-ID-Erkennung ohne Plausibilitätsprüfung**; jeder Lesefehler (0x00/0xFF/Müll bei einbrechender Versorgung) fällt auf den V1.1-Treiber zurück. Da die **Busy-Polarität der beiden Treiber invertiert** ist (BaseDisplay: busy==HIGH, LCMEN: busy==LOW), hängt ein fehl­erkanntes V1.2-Panel im allerersten `wait()` **endlos — vor dem ersten sichtbaren Refresh** (Boot wirkt eingefroren) | esp32_functions.cpp:56-119 | chipId gegen bekannte Werte beider Controller prüfen; bei unplausiblem Wert Retry mit längerem Power-Settle + zweitem Reset; erkannten Typ in Settings persistieren und als Fallback nutzen |
| K5 | **`wait()`-Busy-Schleifen ohne Timeout** (BaseDisplay + LCMEN) — jeder Panel-Hänger (SPI-Glitch, ESD, Spannungseinbruch beim Refresh) blockiert die gesamte Firmware dauerhaft; besonders kritisch beim finalen AKKU-LOW-Refresh: `esp_deep_sleep_start()` wird dann nie erreicht und der Akku tiefentlädt | Displays/BaseDisplay/hardware.cpp:63-67; LCMEN2R13EFC1/hardware.cpp:103-106 | millis()-Timeout (~5 s) in beide `wait()`; bei Überschreitung return + optional `toggleResetPin()` |
| K6 | **V1.1-`activate()` endet mit Power-Off-Befehl 0x02 OHNE `wait()`**, und `prepareToSleep()` (fertig in der Library vorhanden!) wird nie gerufen → Deepsleep startet, während der Panel-Controller noch seine Power-Down-Sequenz fährt; VEXT/SPI-Pins floaten | LCMEN2R13EFC1/hardware.cpp:15-27; command_functions.cpp:804-811 | Nach `wpShowDeepSleep()` ein abschließendes Panel-wait; dann `Platform::prepareToSleep()` (SX1262-Sleep, VExt definiert aus, NSS-Hold), erst dann `esp_deep_sleep_start()` |
| K7 | **„Deepsleep" zieht mA statt µA:** SX1262 bleibt in Dauer-RX (`startReceive(RX_TIMEOUT_INF)`), kein `radio.sleep()`, kein `gpio_hold_en` für ADC_CTRL/NSS (Pins gehen im Sleep auf High-Z → Spannungsteiler zieht wieder), kein VextOff → der „geschützte" Akku entlädt im Schlaf weiter bis zur Tiefentladung — genau die Vorbedingung des beobachteten Repower-Szenarios | command_functions.cpp:803-812; power_controls.cpp:39-95 (ungenutzt) | `radio.sleep()` bzw. `prepareToSleep()` + `gpio_hold_en(ADC_CTRL=HIGH)` vor dem Sleep |
| K9 | **millis()-Wrap (49,7 Tage):** `Clock::CheckEvent` nutzt Absolut-Deadlines (`millis() > u32Next_m`) — beim Wrap kann die Sekunden-Uhr stehenbleiben oder rasen; die **gesamte WP-Displayaktualisierung hängt an dieser Uhr** (node_date_second-Tick) → Display-Freeze nach 49,7 Tagen Dauerlauf. Zusätzlich 31 Stellen mit wrap-unsicherem Muster `(X + Konst) < millis()`, u. a. das Batt-Gate | clock.cpp:72-119; projektweit | Wrap-sicheres Differenzmuster `(uint32_t)(millis() - start) >= intervall` überall; bei Clock zusätzlich Drift-Korrektur gratis |
| K10 | **`mv_to_percent()`: Division durch 0/NaN** bei `node_maxv == 3.3`; `--maxv` ist komplett unvalidiert (`sscanf %f`), `node_maxv = 0` erzeugt Boot-Deepsleep mit **vollem** Akku (read_batt übernimmt ungeprüft, überschreibt den >0-Guard aus esp32_main alle 0,5 s) | batt_functions.cpp:391-394, 208; command_functions.cpp:327 | `--maxv` validieren (3,5–5,0 V), in read_batt nur übernehmen wenn `> BAT_MIN+0,1`, Nenner absichern |
| K11 | **FLASH_VERSION-Mismatch → `clear_flash()` löscht ALLE Settings** (inkl. Rufzeichen/Frequenz); ein einziger durch Brownout-Write verlorener `node_fversion`-Eintrag eskaliert zum Totalverlust; Rückgabewert von `preferences.begin()` wird nie geprüft | esp32_main.cpp:717-724; esp32_flash.cpp:261-273 | Migrieren statt wipen; kritische Keys (call/freq/sset) als Backup-Kopie in zweitem Namespace; begin()-Fehler ≠ clear_flash |

### 🟡 Medium

| ID | Fund | Ort | Vorschlag |
|----|------|-----|-----------|
| K12 | **Abschalt-Sequenz bei Tiefstspannung:** `delay(1000)` + NVS-Flash-Write + **zwei** E-Ink-Voll-Refreshes back-to-back (`--display off`→sendDisplayHead-Clear, dann wpShowDeepSleep) — Brownout mitten im Refresh hinterlässt ein halbes Bild; Flash-Write bei LDO-Dropout-Grenze | batt_functions.cpp:304-317; loop_functions.cpp:1212-1225, 1789-1819 | `--display off` durch reine RAM-Flags ersetzen (siehe V2.2) → nur noch EIN Refresh, kein Flash-Write |
| K13 | **~~Button-Init ohne Pull-up~~ → FALSE POSITIVE (siehe Korrektur unten).** Verbleibt nur: `ANALOG_PIN 0` == `BUTTON_PIN 0` (beide GPIO0); `--analog on` könnte den Button-GPIO umkonfigurieren — bei nicht genutztem `--analog` aber harmlos. | onebutton_functions.cpp:307-313; configuration.h:59/63 | ANALOG_PIN für WP auf ungenutzt/99 legen (nur falls `--analog` je gebraucht wird). **Pull-up NICHT ändern.** |
| K14 | **`checkSerialCommand`: Schreiben vor Bounds-Check** → bei 600 Bytes ohne Newline fehlt der NUL-Terminator → `strlen()`-Overread (UB, Info-Leak via Debug-Ausgabe); ein 0x00-Phantombyte als erstes Zeichen legt die Konsole bis zum Reboot lahm. Direkt erreichbar über die dokumentierte Floating-RX-Phantombyte-Flut im Akkubetrieb | esp32_main.cpp:3814-3922 | Bounds-Check vor dem Schreiben, immer nullterminieren, 0x00 verwerfen, vollen Puffer ohne Newline verwerfen |
| K15 | **`save_settings()` = Komplett-Rewrite (~120 Keys) bei JEDER gesendeten Nachricht/Bake/ACK** (node_msgid++ → „Flash rewrite") + bei jedem Boot → unnötig viele Flash-Writes, vergrößert das Brownout-Korruptionsfenster | loop_functions.cpp:3200 u. a.; esp32_flash.cpp:275-524 | node_msgid aus dem Voll-Save herauslösen (eigener putInt oder RTC-RAM + periodisch); Saves unterhalb ~3,4 V verweigern/queuen |
| K16 | `tx_is_active`-Hänger in 2 Fehlerpfaden (Teil von K8) | lora_functions.cpp:1616-1623; esp32_main.cpp:1997-2030 | siehe V4.3 |

### 🟢 Low / Info (Auswahl)

| Fund | Ort | Anmerkung |
|------|-----|-----------|
| `VextON()/VextOFF()` in batt_functions für WP mit **invertierter Polarität** (Library: active LOW; Code schaltet HIGH=an) | batt_functions.cpp:53-79 | Aktuell toter Code auf WP — aber latente Falle für künftige Power-Arbeiten (würde exakt ein „Display tot"-Symptom erzeugen) |
| OOB-Read: `memcpy(pageText, …, 25)` liest hinter 3-Byte-Literalen (`#L`/`#F`/`#S`) | loop_functions.cpp:934 | Nur Lese-Zugriff; `memcpy(…, strlen+1)` oder strncpy |
| Falsche snprintf-Grenze `sizeof(cset)` statt `sizeof(clfd)` im {MCP}-Parser | loop_functions.cpp:2090-2092 | Latent (schreibt heute max. 5 Bytes) |
| strncpy ohne NUL im Nicht-WP-Klickpfad | onebutton_functions.cpp:172,176 | Auf WP unerreichbar |
| Absolut-Deadlines `millis() > X` bei DisplayOffWait/rebootAuto → Frühzündung beim Wrap | esp32_main.cpp:3095-3119 | Wrap-sicheres Muster |
| `--postime`-Clamp setzt jeden Wert ≥ 300 auf 0 | command_functions.cpp:348-361 | Logikfehler, kein Display-Bezug |
| Brownout-Detector: Framework-Default LVL7 (~2,44 V) aktiv, keine Boot-Strom-Staffelung (WiFi+LoRa+E-Ink-Refresh parallel beim Boot) | platformio.ini / sdkconfig-Default | Diagnose zuerst: `esp_reset_reason()` loggen; falls Brownout-Resets bestätigt: Boot staffeln (WiFi nach erstem Refresh), NICHT den Detector absenken |
| USB-UART + NetConsole teilen `strText`-Puffer (Interleaving möglich) | esp32_main.cpp:238, 3814 | Theoretisch, getrennte Puffer wären sauberer |
| Boot-Anzeige hängt am Sekunden-Tick (`node_date_second`); steht die Uhr, bleibt `iInitDisplay` stehen | loop_functions.cpp:1830-1876 | Nur relevant in Kombination mit K9/Clock-Probleme |
| `wpNoMsgShownAt`-Sentinel kollidiert, wenn `millis()` exakt 0 liefert | loop_functions.cpp:1779 | Theoretisch; `if(x==0) x=1` |

### ✅ Entwarnungen (gezielt geprüft, kein Fehler)

- **AKKU-LOW-Render-Kette korrekt:** Reihenfolge `bWpAkkuLow=true` → `wpShowDeepSleep()` zeichnet vor dem Sleep; Puffer/Modulo in `wpBattHistory()` verifiziert fehlerfrei.
- **`esp32_functions.cpp:135` (`bDisplayOff = true`)**: OLED-Autodetect-Fallback, für WP **wegcompiliert** — kein Risiko.
- **ADC_CTRL-Polarität (GPIO19, active LOW)** in `ADC_BATT_ON/OFF` korrekt invertiert; Re-Aktivierung nach Deepsleep vorhanden.
- **Kein ISR-Race:** OneButton ist rein poll-basiert (btn.tick() im Loop); alle Display-Zugriffe laufen im einzigen loopTask; BLE über Queue, LoRa-Flags atomic. Die Hypothese „Render-Kollision durch zweiten Task" ist entkräftet.
- **Panel-Hardware-Reset beim Boot:** jeder Boot macht einen harten RST-Toggle + jeder fastmodeOn/Off resettet — ein im Controller-Sleep hängendes Panel überlebt keinen sauberen Boot (sofern der richtige Treiber gewählt wird, siehe K4).
- **Browse-/Ring-Indizes (Browse 9, PAGE_MAX 10)** ohne Out-of-bounds; `wpMsgFont()` heapfrei; startDisplay-Puffer ohne Overflow.
- **WP-Boot-Reihenfolge:** kein Code nach dem Boot-Fix setzt `bDisplayOff` wieder true; kein RTC-Memory; Boot nach Deepsleep == normaler Power-On (Display-Init wird nie übersprungen).
- **K13 Button-Pull-up (FALSE POSITIVE, korrigiert 2026-06-12, Hinweis von Wolfgang):** Der Audit-Vorschlag „`INPUT_PULLUP` auch für WP" ist **falsch und darf NICHT umgesetzt werden.** Hintergrund: OneButton 2.6.2 hat zwei APIs mit *unterschiedlicher* Parameterreihenfolge — Konstruktor `OneButton(pin, activeLow, pullupActive)` vs. Methode `setup(pin, mode, activeLow)`. Der WP-Pfad ([onebutton_functions.cpp:307-309](../src/onebutton_functions.cpp#L307-L309)) nutzt `btn.setup(GPIO0, INPUT, /*activeLow=*/true)`; in `OneButton::setup()` geht der 2. Parameter **direkt an `pinMode(pin, mode)`**. `BUTTON_PIN = 0` ist ein **Strapping-Pin mit externem Pull-up** auf dem Heltec-Board (sonst kein Flash-Boot) — der interne Pull-up ist überflüssig und auf einem Strapping-Pin bewusst *nicht* gesetzt. `INPUT` ist hier die **absichtlich korrekte Wahl**; der Button funktioniert (extern HIGH gehalten, Tastendruck → LOW). Ein erzwungenes `INPUT_PULLUP` würde nur den (überflüssigen) internen Pull-up zuschalten und die bewusste Entscheidung überschreiben.

---

## 5. Priorisierte Umsetzungsempfehlung

**Stufe 1 — behebt das gemeldete Symptom (klein, WP-only, upstream-tauglich):**
1. Wakeup-Quelle vor `esp_deep_sleep_start()` (V1: ext1 auf GPIO0 + Hinweistext) — *eine Handvoll Zeilen in command_functions.cpp, WP-guarded*.
2. Low-Voltage-Pfad ohne `--display off`/`save_settings()` (V2.2 + K12) — *ersetzt einen Aufruf durch zwei RAM-Flag-Zeilen*.
3. Boot-Fix committen + `node_sset &= ~0x0002` ergänzen (V2.1/V2.3).
4. `is_receiving`/`tx_is_active`-Hänger fixen (V4) — *vier kleine, gut begründbare Zeilen; macht den Low-Batt-Schutz erst zuverlässig*.

**Stufe 2 — Robustheit Akku/Boot:**
5. Lade-Trend-Heuristik + Filter-Konvergenz-Gate (V3).
6. `--maxv`-Validierung + mv_to_percent-Absicherung (K10).
7. `wait()`-Timeouts + Chip-ID-Plausibilität (K4/K5) — *betrifft die Display-Library im Repo*.

**Stufe 3 — längerfristig:**
8. `prepareToSleep()`/radio.sleep/gpio_hold in den Sleep-Pfad (K6/K7).
9. millis()-Wrap-Muster projektweit (K9), beginnend mit clock.cpp und Batt-Gate.
10. Flash-Hygiene: msgid aus dem Voll-Save, Migration statt clear_flash (K11/K15).
11. Serial-Parser-Härtung (K14) — deckt sich mit der bestehenden Phantombyte-Analyse.

**Diagnose-Empfehlung vor jedem Fix:** `esp_reset_reason()` + `esp_sleep_get_wakeup_cause()` beim Boot ins Log (und ggf. klein auf den Startscreen) — unterscheidet sofort die drei Szenarien „schläft noch" (K1), „bootet und schläft wieder ein" (K3) und „Brownout-Schleife" (K16/Brownout).

---

## 6. Limitierungen

- Die geplante adversariale Verifikations-Stufe des Multi-Agent-Audits ist am API-Session-Limit gescheitert; die **Kern-Ketten K1–K3, K8 und die AKKU-LOW-Analyse wurden stattdessen manuell im Quelltext verifiziert** (Code-Zitate oben). Findings zu Library-Interna (Busy-Polaritäten, Panel-Power-Sequenzen, sdkconfig-Defaults) beruhen auf Agenten-Lektüre der Library-Quellen und tragen Restunsicherheit — vor Umsetzung dort kurz gegenlesen.
- Severity-Einstufungen beziehen sich auf das WP; einige Funde (K9, K11, K14, K15) betreffen alle ESP32-Boards des Projekts.
- Keine Laufzeit-Messungen (Schlafstrom, Brownout-Zähler) — die vorgeschlagene Reset-Reason-Diagnose würde die verbleibenden Hypothesen am Gerät trennscharf machen.
