# Security- & Stabilitäts-Audit — MeshCom Firmware (Wireless Paper + Vision Master E213)

**Datum:** 2026-07-01  
**Untersuchter Stand:** Working Tree, Branch `wireless-paper-v11-v12-fixes` @ `8da79a2` (frisch gemergter upstream/dev `6088f1a`) + lokale uncommittete WP-Änderungen  
**Boards:** Heltec Wireless Paper (`BOARD_WIRELESS_PAPER`) + Heltec Vision Master E213 (`BOARD_E213`); geteilter Code + board-spezifische Pfade  
**Methode:** Multi-Agenten-Audit (9 parallele Auditor-Dimensionen: Speicher, Nebenläufigkeit, Hang/Watchdog, Arithmetik/Zeit, Netzwerk/Eingabe, Power/Deepsleep, Display/E-Ink, Secrets/Supply-Chain, E213-spezifisch) über ~1,54 Mio. Analyse-Tokens. **Jeder Befund wurde anschließend adversarisch am echten Code gegengeprüft** (Datei:Zeile-Beleg, sonst verworfen), danach dedupliziert und von einem Completeness-Kritiker ergänzt. Zusätzlich `cppcheck 2.21` als deterministisches Ausgangsmaterial (Anhang).  
**Read-only:** Es wurde **kein Code verändert** — dieser Report benennt ausschließlich Findings.

---

## 0. Executive Summary

| Severity | Anzahl | Bedeutung |
|---|---|---|
| 🔴 **Rot** (Critical) | **0** | Reboot/Hang/Crash/Datenverlust im Normalbetrieb wahrscheinlich |
| 🟠 **Orange** (High) | **7** | Ausfall unter realistischen Sonderbedingungen (Fremddaten, Netzverlust, leerer Akku) |
| 🟡 **Gelb** (Medium) | **10** | Robustheit/Korrektheit, destabilisiert langfristig |
| 🔵 **Blau** (Low) | **7** | Code-Smell/Hinweis inkl. CI-/Secret-Hygiene |
| **Summe** | **24** | adversarisch verifiziert (CONFIRMED/PLAUSIBLE) |

**Gesamtbild:** 🟢 **keine kritischen (roten) Findings** — die im 06-12-Audit adressierten Kern-Ursachenketten (Deepsleep-Wakeup, Sleep-Strom, AKKU-LOW) halten im Preview-Build. Aufmerksamkeit verdienen die **7 orange** Befunde: mehrere betreffen den **committfähigen Default-Build** (nicht nur Preview), dazu drei **netzseitige** Findings (UDP-OOB, Remote-DoS, Passwort-Bypass).

### Schnellübersicht

| # | Sev | Bereich | Board | Befund | Stelle |
|---|---|---|---|---|---|
| 1 | 🟠 | Arithmetik/Zeit | beide | NTP-Retry-Unterlauf: erste ~14 min nach Boot Dauer-Poll statt 60 | `esp32_main.cpp:2515` |
| 2 | 🟠 | Speicher | beide | UDP-RX: Off-by-one OOB-Write in incomingPacket bei Vollpaket | `udp_functions.cpp:104` |
| 3 | 🟠 | Netzwerk/Eingabe | alle | Crafted UDP-Paket erzwingt WLAN-Teardown (Remote-DoS) | `udp_functions.cpp:131` |
| 4 | 🟠 | Power/Deepsleep | beide | Low-Voltage-Flash-Write: --display off (save_settings) bei ~3.3  | `batt_functions.cpp:346` |
| 5 | 🟠 | Power/Deepsleep | WP | Default-WP-Build: --deepsleep armiert keine Wakeup-Quelle und ru | `command_functions.cpp:831` |
| 6 | 🟠 | Power/Deepsleep | beide | is_receiving bleibt dauerhaft haengen -> read_batt()/Low-Voltage | `esp32_main.cpp:2345` |
| 7 | 🟠 | Secrets/Supply-Chain | beide | Klartext-Passwort-Bypass umgeht HMAC-Challenge-Response (Passwor | `net_console.cpp:174` |
| 8 | 🟡 | Nebenläufigkeit | beide | stopNetConsole() ueberschreibt aktiven Mutex-Handle (Leak + gebr | `net_console.cpp:292` |
| 9 | 🟡 | Completeness-Kritiker | beide | Fix#4 loest bei reiner Server-Ausfallzeit periodischen WiFi-Tear | `esp32_main.cpp:3561` |
| 10 | 🟡 | Display/E-Ink | beide | Panel-Auto-Erkennung ohne Validierung/Fallback -> Fehlerkennung  | `esp32_functions.cpp:131` |
| 11 | 🟡 | Display/E-Ink | beide | V1.2/E213-Statuszeile: 10-s-Teilrefresh ohne periodischen Voll-R | `loop_functions.cpp:1699` |
| 12 | 🟡 | E213-spezifisch | E213 | E213-Serie (non-preview) fehlt der Grau-Fix beim Akku-leer-Deeps | `command_functions.cpp:818` |
| 13 | 🟡 | Hang/Watchdog | beide | E-Ink BUSY-Poll blockiert Loop bis zu 8 s pro wait() (kumulativ  | `hardware.cpp:70` |
| 14 | 🟡 | Hang/Watchdog | beide | Endlos-Busywait im Spektral-Scan ohne Timeout | `spectral_scan.cpp:114` |
| 15 | 🟡 | Netzwerk/Eingabe | alle | Off-by-one OOB-Write beim Null-Terminieren des UDP-Empfangspuffe | `udp_functions.cpp:104` |
| 16 | 🟡 | Secrets/Supply-Chain | beide | --info-Ausgabe druckt das Net-Console-Passwort im Klartext, entg | `command_functions.cpp:4729` |
| 17 | 🟡 | Secrets/Supply-Chain | beide | Net-Console-Passwort wird bei jeder Authentifizierung im Klartex | `net_console.cpp:171` |
| 18 | 🔵 | Arithmetik/Zeit | alle | millis()-49.7-Tage-Wrap: 'timer + intervall < millis()' statt '( | `batt_functions.cpp:265` |
| 19 | 🔵 | E213-spezifisch | E213 | BaseDisplay::landscape() kennt Vision_Master_E213 nicht -> laten | `layout.cpp:189` |
| 20 | 🔵 | Speicher | beide | snprintf mit falschem Groessen-Argument (sizeof(cset) statt size | `loop_functions.cpp:2114` |
| 21 | 🔵 | Netzwerk/Eingabe | alle | Unbegrenzte strText-Akkumulation ohne Newline -> strlen liest OO | `esp32_main.cpp:3995` |
| 22 | 🔵 | Netzwerk/Eingabe | alle | NetConsole akzeptiert jedes Passwort-Praefix im Klartext (Auth-S | `net_console.cpp:174` |
| 23 | 🔵 | Netzwerk/Eingabe | alle | sendMeshComUDP kopiert msg_len ab Offset +37 -> OOB-Read ueber R | `udp_functions.cpp:454` |
| 24 | 🔵 | Secrets/Supply-Chain | alle | CI laedt und fuehrt ungepinnte Fremd-Skripte/Board-Defs ueber wg | `meshcom-ci.yml:59` |

---

## 1. Farb-Legende

- 🔴 **Rot · Critical**
- 🟠 **Orange · High**
- 🟡 **Gelb · Medium**
- 🔵 **Blau · Low/Hinweis**

> **Verdikt:** `CONFIRMED` = am Code eindeutig belegt · `PLAUSIBLE` = real, aber mit Restunsicherheit (Library-intern / HW-abhängig / Severity-Einschätzung).

---

## 🟠 Orange · High — 7 Befund(e)

### 🟠 [ARITH-1] NTP-Retry-Unterlauf: erste ~14 min nach Boot Dauer-Poll statt 60-s-Retry

- **Board:** WP + E213 · **Bereich:** Arithmetik/Zeit · **Verdikt:** `CONFIRMED` · **Quelle:** reasoning
- **Stelle:** [src/esp32/esp32_main.cpp:2515](src/esp32/esp32_main.cpp#L2515)

**Beweis:**
```cpp
const unsigned long POLL_MS = 1000UL*60*15; const unsigned long RETRY_MS = 1000UL*60; unsigned long now = millis(); updateTimeClient = (now > (POLL_MS - RETRY_MS)) ? (now - (POLL_MS - RETRY_MS)) : 0;  // POLL_MS-RETRY_MS = 840000; darueber Trigger: if((updateTimeClient + 1000*60*15) < millis() || updateTimeClient == 0)
```

**Ausfallszenario:** WiFi/IP steht, aber der NTP-/MeshCom-Zeitserver ist beim Boot nicht erreichbar (HAMNET/Firewall, Serverausfall) -> udpUpdateTimeClient() liefert 'none'. Solange millis() < 840000 (die ersten ~14 min nach jedem Boot/Reconnect) greift der else-Zweig und setzt updateTimeClient=0. Die aeussere Bedingung (Zeile 2498) ist bei updateTimeClient==0 sofort wieder wahr -> es wird in JEDER Loop-Iteration erneut gepollt statt wie beabsichtigt alle ~60 s. NTPClient.forceUpdate() blockiert dabei bis zu ~1000 ms pro Aufruf auf die Antwort. Ergebnis: die Hauptschleife wird bis zu 14 Minuten lang praktisch pro Durchlauf ~1 s blockiert -> LoRa-RX-Fenster verpasst, UDP/Display/Button traege, NTP-Server wird geflutet. Der Underflow-Guard verwandelt den gewollten 60-s-Retry in einen Dauer-Poll.

**Fix-Vorschlag (NICHT angewandt):** In der Boot-Phase (now <= POLL_MS-RETRY_MS) NICHT auf 0 setzen (0 = sofort-Trigger), sondern einen separaten Retry-Zeitstempel fuehren, z.B. eigenes 'nextNtpRetryAt = millis() + RETRY_MS' und die Trigger-Bedingung darauf pruefen; oder frühestens ab now > POLL_MS-RETRY_MS zuruckziehen und davor schlicht auf millis() belassen (naechster Poll dann regulaer in 15 min, aber KEIN Dauer-Poll). Alternativ die Sonderbedeutung updateTimeClient==0 vom Retry-Pfad entkoppeln.

<details><summary>Verifikations-Notiz</summary>

Code existiert exakt wie behauptet an esp32_main.cpp:2510-2516, geguarded durch #if defined(WP_DISP_PREVIEW) (= WP_PREVIEW || E213_PREVIEW, configuration_global.h:53-54), also aktiv fuer die Preview-Builds beider Boards. Arithmetik-Verifikation: POLL_MS-RETRY_MS = 840000. Bei fehlgeschlagenem Poll (strTime=="none") und now <= 840000 (erste ~14 min nach Boot) greift der else-Zweig des Ternaers und setzt updateTimeClient=0 (Zeile 2515). Die aeussere Bedingung Zeile 2498 `... || updateTimeClient == 0` ist dann in JEDER Loop-Iteration sofort wieder wahr -> Dauer-Poll statt 60-s-Retry. Der Blocking-Aspekt ist ebenfalls belegt: Die verwendete Lib (.pio/libdeps/wireless-paper/NTPClient/NTPClient.cpp) setzt _lastUpdate NUR bei Erfolg; bei NTP-Ausfall bleibt _lastUpdate==0, wodurch update() (Zeile 120) jedes Mal forceUpdate() aufruft, das im do/while mit delay(10)*~101 ~1010 ms bis Timeout blockiert (Zeile 96-104). Ergebnis: Hauptschleife bis zu 14 min lang ~1 s pro Durchlauf blockiert -> verpasste LoRa-RX-Fenster, traege UDP/Display/Button, NTP-Server-Flut. Nach millis()>840000 arbeitet der Fix wie beabsichtigt (60-s-Retry). Szenario (IP steht, NTP-/MeshCom-Zeitserver beim Boot unerreichbar) ist real und nicht anderweitig entschaerft. Einzige Praezisierung: betrifft nur die -D WP_PREVIEW / -D E213_PREVIEW Builds, nicht die Basis-Envs -- innerhalb des Preview-Scopes aber genau wie behauptet. Severity orange passt: realistische Sonderbedingung (Teil-Netzverlust), degradiert Kernpfade fuer bis zu 14 min, kein Crash, selbstheilend.

</details>

---

### 🟠 [MEM-2] UDP-RX: Off-by-one OOB-Write in incomingPacket bei Vollpaket

- **Board:** WP + E213 · **Bereich:** Speicher · **Verdikt:** `CONFIRMED` · **Quelle:** reasoning
- **Stelle:** [src/udp_functions.cpp:104](src/udp_functions.cpp#L104)

**Beweis:**
```cpp
unsigned char incomingPacket[UDP_TX_BUF_SIZE];  // = [255]
...
int len = Udp.read(incomingPacket, UDP_TX_BUF_SIZE);  // liefert bis zu 255
if (len > 0) {
    incomingPacket[len] = 0;   // len==255 -> incomingPacket[255] = 1 Byte hinter dem Array
```

**Ausfallszenario:** Sobald ueber WiFi ein UDP-Paket mit >=255 Nutzbytes am MeshCom-Port ankommt (fremd-/netzgesteuert, z.B. verstuemmeltes oder boesartiges Serverpaket), liefert Udp.read() genau UDP_TX_BUF_SIZE=255 zurueck. incomingPacket ist exakt [255] gross (gueltige Indizes 0..254), sodass incomingPacket[255]=0 ein Byte hinter das Array in das benachbarte globale Symbol (packetSize/convBuffer, je nach Link-Reihenfolge) schreibt. Zusaetzlich liest die zerocount-Schleife bei ungeradem/maximalem packetSize in Zeile 123 inc_udp_buffer[i+1] am Index 255 ebenfalls einen Schritt zu weit. WP und E213 nutzen beide den WiFi/UDP-Pfad -> stiller Speicher-Overwrite eines Nachbar-Globals.

**Fix-Vorschlag (NICHT angewandt):** Puffer auf UDP_TX_BUF_SIZE+1 vergroessern ODER beim Lesen ein Byte fuer den Terminator reservieren: int len = Udp.read(incomingPacket, UDP_TX_BUF_SIZE-1); bzw. vor incomingPacket[len]=0 auf len<UDP_TX_BUF_SIZE clampen. Die zerocount-Schleife auf i+1 < packetSize absichern.

<details><summary>Verifikations-Notiz</summary>

Code steht exakt so: udp_functions.cpp:67 `unsigned char incomingPacket[UDP_TX_BUF_SIZE];` mit UDP_TX_BUF_SIZE=255 (configuration_global.h:70) -> gueltige Indizes 0..254. Zeile 100 `Udp.read(incomingPacket, UDP_TX_BUF_SIZE)` liefert bei ESP32 WiFiUDP bis zu 255. Zeile 104 `incomingPacket[len] = 0;` schreibt bei len==255 auf incomingPacket[255] = 1 Byte hinter das Array in ein Nachbar-Global. Zusaetzlich OOB-Read: zerocount-Schleife Zeile 121-123 iteriert i+=2 bis i<packetSize(=255), liest bei i=254 inc_udp_buffer[255]. Kein Board-Guard (nur #ifdef ESP32 um WiFi); WP (ESP32-S3FN8) und E213 (ESP32-S3R8) definieren beide ESP32 und nutzen den UDP-Pfad -> beide betroffen. Erreichbar durch UDP-Datagramm mit >=255 Nutzbytes am MeshCom-Port. Severity orange passt: netzgetriggerte 1-Byte-OOB-Write(0)+1-Byte-OOB-Read, kein sicherer Crash aber echte Speicherverletzung unter Fremddaten-Bedingung.

</details>

---

### 🟠 [NET-3] Crafted UDP-Paket erzwingt WLAN-Teardown (Remote-DoS)

- **Board:** alle Boards · **Bereich:** Netzwerk/Eingabe · **Verdikt:** `CONFIRMED` · **Quelle:** reasoning
- **Stelle:** [src/udp_functions.cpp:131](src/udp_functions.cpp#L131)

**Beweis:**
```cpp
if (zerocount <= MAX_ZEROS) { ... } else { DEBUG_MSG("ERROR", "UDP Message has too much Zeros"); resetMeshComUDP(); }  // resetMeshComUDP(): Udp.stop(); WiFi.disconnect(true,true); hasIPaddress=false;
```

**Ausfallszenario:** Ein WP/E213-Node im Gateway-Modus (bGATEWAY, getMeshComUDP() in esp32_main.cpp:3502) bindet den UDP-Socket via Udp.begin(LOCAL_PORT=1990) auf INADDR_ANY. Jeder Host im LAN (bzw. im HAMNET, wenn Port erreichbar) kann ein UDP-Paket an Port 1990 senden, das nach beliebigen Nutzdaten >=8 aufeinanderfolgende Null-Bytes am Ende traegt (zerocount > MAX_ZEROS=6). Die Fremddaten-Pruefung ruft dann resetMeshComUDP() auf -> Udp.stop() + WiFi.disconnect(true,true) + hasIPaddress=false. Wiederholte Pakete halten den Node dauerhaft im WLAN-Reconnect-Zyklus (Sekunden Downtime pro Paket) = anhaltender Denial-of-Service, ohne jede Authentifizierung.

**Fix-Vorschlag (NICHT angewandt):** Die Fehlerbehandlung fuer 'zu viele Nullen' darf NICHT das WLAN abbauen. Nur das einzelne Paket verwerfen (return / continue), statt resetMeshComUDP() aufzurufen. resetMeshComUDP() ausschliesslich bei echten Socket-/TX-Fehlern (err_cnt_udp_tx) verwenden, nicht bei Eingangs-Datenvalidierung. Optional zusaetzlich Absender-IP gegen node_hostip pruefen, bevor ein Eingangspaket verarbeitet wird.

<details><summary>Verifikations-Notiz</summary>

Code existiert wie behauptet: udp_functions.cpp:131 `if (zerocount <= MAX_ZEROS)` (MAX_ZEROS=6, configuration_global.h:124), else-Zweig 400-404 ruft resetMeshComUDP() (1000-1014: Udp.stop(); WiFi.disconnect(true,true); hasIPaddress=false). Die zerocount-Schleife (121-129, i+=2, reset auf non-zero-Paar) misst den Trailing-Run von 00-00-Paaren; >=8 anhaengende Null-Bytes loesen den Reset aus - trivial craftbar. Socket via Udp.begin(LOCAL_PORT=UDP_PORT=1990) auf INADDR_ANY (854), getMeshComUDP (esp32_main.cpp:3502) liest ohne Source-IP-Filter/Auth. Pfad nur durch `bGATEWAY && node_hasIPaddress` (3500) geguarded - generischer ESP32-Gateway-Code, gilt fuer WP und E213 (beide ESP32-S3, WiFi-faehig). Jeder erreichbare Host kann per Fremdpaket WiFi-Teardown erzwingen, wiederholt = anhaltender DoS ohne Auth.

</details>

---

### 🟠 [PWR-4] Low-Voltage-Flash-Write: --display off (save_settings) bei ~3.3 V Cutoff in den Non-Preview-Builds

- **Board:** WP + E213 · **Bereich:** Power/Deepsleep · **Verdikt:** `CONFIRMED` · **Quelle:** reasoning
- **Stelle:** [src/batt_functions.cpp:346](src/batt_functions.cpp#L346)

**Beweis:**
```cpp
#if !(defined(WP_DISP_PREVIEW))
  commandAction((char*)"--display off", isPhoneReady, false);
// Handler command_functions.cpp:765/774: node_sset|=0x0002; save_settings();
```

**Ausfallszenario:** In den committfaehigen Default-Builds (env:wireless-paper und env:vision-master-e213 - beide OHNE *_PREVIEW, also WP_DISP_PREVIEW nicht gesetzt) wird beim Erreichen von BAT_MIN_VOLTAGE (3.3 V) im Low-Voltage-Block von read_batt() 'commandAction(--display off)' ausgefuehrt. Dessen Handler (command_functions.cpp:774) ruft save_settings() -> Flash/NVS-Write. Dieser Write erfolgt am tiefsten Akkupunkt, waehrend der SX1262 noch im RX (~5 mA) laeuft (loraToSleep/prepareToSleep passieren erst spaeter im --deepsleep) -> Spannungs-Sag unter Last kann den Write abbrechen -> NVS-Korruption/Settings-Verlust. Genau dieser Pfad ist im Preview bewusst deaktiviert, in den Default-Builds aber aktiv (K5).

**Fix-Vorschlag (NICHT angewandt):** Den flash-schreibenden '--display off'-Aufruf im Low-Voltage-Pfad auch fuer die Non-Preview-Builds unterdruecken (Guard auf 'defined(WP_DISP)' statt '!defined(WP_DISP_PREVIEW)'); das sichtbare Loeschen uebernimmt ohnehin wpShowDeepSleep() ohne save_settings().

<details><summary>Verifikations-Notiz</summary>

Code an batt_functions.cpp:346-348 existiert wie behauptet: im Zweig `#if !(defined(WP_DISP_PREVIEW))` laeuft `commandAction("--display off")`, dessen Handler command_functions.cpp:765/774 `node_sset |= 0x0002; save_settings()` unbedingt ausfuehrt = Flash/NVS-Write. Beide Default-Builds (env:wireless-paper, env:vision-master-e213) setzen KEIN *_PREVIEW (platformio.ini geprueft), daher ist in configuration_global.h:53 WP_DISP_PREVIEW nicht gesetzt -> Zweig aktiv. Pfad liegt in `#ifdef USE_BATT` (ESP32/ADC-Pfad) und wird bei BatVoltage<=BAT_MIN_VOLTAGE(3.3V) nach CountDown erreicht; `--deepsleep`/loraToSleep folgt erst danach (Zeile 360), SX1262 also noch im RX. Die Entwickler dokumentieren den Pfad selbst (Zeile 350-355) als Korruptionsrisiko und deaktivieren ihn im Preview bewusst. Auftritt nur bei leerem Akku = passt zu orange.

</details>

---

### 🟠 [PWR-5] Default-WP-Build: --deepsleep armiert keine Wakeup-Quelle und ruft prepareToSleep() nicht auf

- **Board:** Wireless Paper · **Bereich:** Power/Deepsleep · **Verdikt:** `CONFIRMED` · **Quelle:** reasoning
- **Stelle:** [src/command_functions.cpp:831](src/command_functions.cpp#L831)

**Beweis:**
```cpp
#if (defined(WP_DISP_PREVIEW)) || defined(BOARD_E213)
  uint64_t wake_mask = (1ULL << 0);
  if (iButtonPin < 22) wake_mask |= (1ULL << iButtonPin);
  esp_sleep_enable_ext1_wakeup(wake_mask, ESP_EXT1_WAKEUP_ANY_LOW);
  ...
  Platform::prepareToSleep();
#endif
... esp_deep_sleep_start();
```

**Ausfallszenario:** Im committfaehigen Default-Build env:wireless-paper (kein WP_PREVIEW, kein BOARD_E213) ist der Guard in Zeile 831 FALSE. Sowohl der manuelle --deepsleep (Long-Press) als auch der Low-Voltage-Auto-Deepsleep aus read_batt() laufen dann durch wpShowDeepSleep() direkt in esp_deep_sleep_start() (Z.852) - OHNE esp_sleep_enable_ext1_wakeup und OHNE Platform::prepareToSleep(). Folge 1: keine Aufweckquelle -> das Geraet ist nur per Power-Cycle/RESET wiederbelebbar, das bistabile E-Ink bleibt scheinbar dauerhaft eingefroren. Folge 2: SX1262 bleibt im Dauer-RX (~5 mA), VEXT/Display-Versorgung bleibt an, RTC-Peripherie an -> der 'Deepsleep' senkt den Strom nicht; am Low-Voltage-Pfad entlaedt der ohnehin leere LiPo weiter bis zur schaedigenden Tiefentladung. Nur die Preview- und E213-Builds sind gegen K1/K6 geschuetzt.

**Fix-Vorschlag (NICHT angewandt):** Den Guard in Z.831 von '(WP_DISP_PREVIEW) || BOARD_E213' auf 'defined(WP_DISP)' erweitern, damit auch der Default-WP-Build ext1-Wakeup (wake_mask GPIO0, iButtonPin=0) armiert und Platform::prepareToSleep() aufruft, bevor esp_deep_sleep_start() laeuft.

<details><summary>Verifikations-Notiz</summary>

Real am Code verifiziert. command_functions.cpp:831 Guard '#if (defined(WP_DISP_PREVIEW)) || defined(BOARD_E213)' umschliesst esp_sleep_enable_ext1_wakeup (842) + Platform::prepareToSleep (848). configuration_global.h:53-54 definiert WP_DISP_PREVIEW nur bei -D WP_PREVIEW/E213_PREVIEW; platformio.ini env:wireless-paper (committfaehiger Default) setzt weder WP_PREVIEW noch BOARD_E213 -> Block FALSE. wpShowDeepSleep() (830) + esp_deep_sleep_start() (852, nur !BOARD_RAK4630) bleiben aktiv. Beide Trigger erreichbar und ungeguarded gegen Preview: onebutton_functions.cpp:249-253 PressLong unter WP_DISP -> commandAction(--deepsleep); batt_functions.cpp:357-360 Low-Voltage -> commandAction(--deepsleep). Keine Default-Wakeup-Alternative (Timer-Wakeup power_controls.cpp:102-104 auskommentiert). Folge: esp_deep_sleep_start ohne jede Wakeup-Quelle = nur per RESET weckbar (ESP-IDF-garantiert), und ausgelassenes prepareToSleep laesst loraToSleep/VExtOff/gpio_hold weg (power_controls.cpp:68-90). Folge-2-Reststrom hat leichte HW-Restunsicherheit (GPIO-Reset im Deepsleep), Folge 1 aber hart. orange passt: Long-Press-Normalbetrieb + Low-Voltage-Sonderbedingung leerer Akku, Geraet nur per Reset weckbar, Stromspar-Sequenz uebersprungen. E213/Preview geschuetzt.

</details>

---

### 🟠 [PWR-6] is_receiving bleibt dauerhaft haengen -> read_batt()/Low-Voltage-Deepsleep nie mehr aufgerufen (K8)

- **Board:** WP + E213 · **Bereich:** Power/Deepsleep · **Verdikt:** `CONFIRMED` · **Quelle:** reasoning
- **Stelle:** [src/esp32/esp32_main.cpp:2345](src/esp32/esp32_main.cpp#L2345)

**Beweis:**
```cpp
// TX-Gate, Preamble/Header im IRQ-Register erkannt:
  is_receiving = true;
  irq_rx_active = true;
// EINZIGE Ruecksetzung: checkRX() Z.3878 'is_receiving=false;'
// checkRX() Z.3718: if(is_receiving) return -1;  // bricht vor 3878 ab
```

**Ausfallszenario:** Sobald ein Sendeauftrag in der Queue liegt UND im IRQ-Register HEADER_VALID/PREAMBLE_DETECTED steht (auf aktivem Mesh regelmaessig), setzt Z.2345 is_receiving=true. Die einzige Stelle, die is_receiving wieder auf false setzt (Z.3878), liegt am ENDE von checkRX(); checkRX() kehrt aber bei Z.3718 'if(is_receiving) return -1;' sofort zurueck, solange is_receiving true ist, und erreicht Z.3878 nie. Damit ist is_receiving permanent true (kein weiterer Clear-Pfad auf dem ESP32; OnHeaderDetect/OnRxError sind auf ESP32 nicht verdrahtet). Die Batteriemessung in mainStartTimeLoop (Z.3186 'if (tx_is_active==false && is_receiving==false)') ruft read_batt() daraufhin nie mehr auf -> der Low-Voltage-Deepsleep loest nie aus, der Akku entlaedt bis zur Tiefentladung. Zusaetzlich verwirft checkRX() jedes weitere RX-Paket (RX de facto tot) bis zum Reboot.

**Fix-Vorschlag (NICHT angewandt):** is_receiving zuverlaessig zuruecksetzen: entweder im receiveFlag-Handler (vor checkRX-Aufruf, Z.~2200) bzw. am Ende des CSMA-Timeout-/RX-Restart-Zweigs (Z.2134) auf false setzen, oder den Batterie-Read nicht hart an is_receiving koppeln (z.B. Deadline: nach X ms erzwungen messen).

<details><summary>Verifikations-Notiz</summary>

Alle zitierten Zeilen exakt bestaetigt: esp32_main.cpp:2345 setzt is_receiving=true aus dem TX-Gate (IRQ-Poll HEADER_VALID/PREAMBLE_DETECTED). Einzige ESP32-Clear-Pfade sind checkRX():3878 sowie OnRxDone() (lora_functions.cpp:375/1234, nur ueber checkRX:3790 erreichbar) - beide liegen HINTER dem Reentrancy-Guard checkRX():3718 'if(is_receiving) return -1;'. OnRxTimeout/OnRxError/OnHeaderDetect (lora_functions.cpp:1262/1305/2004) sind auf ESP32 NICHT verdrahtet (die 'OnRxError' an 3796/3839 sind nur Debug-Strings). Wird is_receiving also von aussen (2345) gesetzt, blockiert 3718 jeden Clear -> permanent true bis Reboot. Folge 1: receiveFlag-Handler ruft checkRX (2207), das bei 3718 abbricht -> RX faktisch tot. Folge 2: Batterie-Gate 3186 'if (tx_is_active==false && is_receiving==false)' passiert nie -> read_batt() (3220) laeuft nie -> dessen Low-Voltage-Deepsleep commandAction('--deepsleep') (batt_functions.cpp:360) loest nie aus -> Tiefentladung. Keine Board-Guards um die Stellen; esp32_main.cpp gilt fuer WP und E213 (beide ESP32-S3). Szenario real erreichbar (TX-Queue nicht leer + Preamble im IRQ am Poll-Zeitpunkt, auf aktivem Mesh haeufig), aber Race-bedingt (nicht jeder Loop) -> orange angemessen; einmal getroffen jedoch dauerhaft.

</details>

---

### 🟠 [SUPPLY-7] Klartext-Passwort-Bypass umgeht HMAC-Challenge-Response (Passwort wird uebers Netz im Klartext akzeptiert)

- **Board:** WP + E213 · **Bereich:** Secrets/Supply-Chain · **Verdikt:** `CONFIRMED` · **Quelle:** reasoning
- **Stelle:** [src/net_console.cpp:174](src/net_console.cpp#L174)

**Beweis:**
```cpp
// KBC check without SHA256
            if(memcmp(respBuf, s_password, strlen(s_password)) != 0)
            {
                ... HMAC-Pfad ...
            }
            else
            {
                authOk = true;
            }
```

**Ausfallszenario:** Wenn die vom Client empfangene Antwortzeile (respBuf, unverschluesseltes TCP auf Port 2323) mit dem konfigurierten Passwort beginnt, wird authOk=true gesetzt OHNE HMAC-Pruefung. Ein Client kann sich also durch Senden des Passworts im KLARTEXT authentifizieren. Damit ist die dokumentierte Sicherheitszusage aus net_console.h ('Das Passwort wird nie uebertragen - nur sein HMAC') gebrochen: das Passwort ist im Klartext ueber das Netz sniffbar/abfangbar. Betrifft WP und E213 (net_console aktiv).

**Fix-Vorschlag (NICHT angewandt):** Den else-Zweig (Klartext-Akzeptanz) entfernen. Ausschliesslich den HMAC-SHA256-Pfad mit ct_equal als gueltige Authentifizierung zulassen; memcmp-Kurzschluss streichen.

<details><summary>Verifikations-Notiz</summary>

Code existiert exakt an src/net_console.cpp:174: `if(memcmp(respBuf, s_password, strlen(s_password)) != 0)`. Beginnt die vom Client (unverschluesseltes TCP Port 2323) empfangene Antwortzeile mit den Klartext-Passwortbytes, liefert memcmp 0, die Bedingung `!= 0` ist falsch, und der else-Zweig (Zeile 193-196) setzt authOk=true OHNE HMAC-Berechnung. Der HMAC-SHA256-Challenge-Pfad (176-191) wird nur bei Nicht-Uebereinstimmung durchlaufen. Kompiliert fuer beide Boards: Guard `#if defined(ESP32) && !defined(DISABLE_NET_CONSOLE)`; DISABLE_NET_CONSOLE ist nur in nrf52-Varianten (wiscore_rak4631, heltec_t114, t_echo, E22) gesetzt, NICHT in variants/wireless-paper/platformio.ini, WP und E213 sind ESP32-S3. Bricht die dokumentierte Zusage net_console.h:11 'The password is never transmitted — only its HMAC is': Passwort wird im Klartext akzeptiert und ist damit sniffbar. Severity orange passt: kein Crash/Reboot, aber unter realistischer Bedingung (passives Mitlesen des Klartext-Ports) leckt das Passwort und die HMAC-Absicherung ist umgehbar.

</details>

---

## 🟡 Gelb · Medium — 10 Befund(e)

### 🟡 [CONC-1] stopNetConsole() ueberschreibt aktiven Mutex-Handle (Leak + gebrochener Ausschluss)

- **Board:** WP + E213 · **Bereich:** Nebenläufigkeit · **Verdikt:** `CONFIRMED` · **Quelle:** reasoning
- **Stelle:** [src/net_console.cpp:292](src/net_console.cpp#L292)

**Beweis:**
```cpp
void stopNetConsole(){ if(!s_started) return; s_started=false; s_mutex = xSemaphoreCreateMutex(); ... teardownClient(); }  // teardownClient() -> xSemaphoreGive(s_mutex)
```

**Ausfallszenario:** bNETCONSOLE wird zur Laufzeit auf 0 gesetzt (esp32_main.cpp:3660 ruft stopNetConsole im else-Zweig). Laeuft gleichzeitig ein con_auth-Task (xTaskCreatePinnedToCore, Core 1, net_console.cpp:386), der gerade den bestehenden s_mutex haelt bzw. bei net_console.cpp:216 blockiert, so ueberschreibt stopNetConsole den Handle mit einem NEUEN Mutex. Folge: (1) alter Mutex wird geleakt (bei wiederholtem netconsole on/off Heap-Leak, je ein Mutex-Objekt), (2) authTask und Main-Loop synchronisieren danach gegen VERSCHIEDENE Mutexe -> gegenseitiger Ausschluss auf s_fd/s_authenticated ist aufgehoben, (3) teardownClient() (net_console.cpp:117) gibt einen soeben erzeugten, nie genommenen Mutex frei. authTask setzt danach s_fd/s_authenticated=true weiter -> 'gestoppte' Konsole haelt eine authentifizierte Verbindung, die loopNetConsole wegen s_started=false nie mehr poll/schliesst (dangling fd).

**Fix-Vorschlag (NICHT angewandt):** Mutex NUR einmal in startNetConsole anlegen. In stopNetConsole den bestehenden s_mutex nehmen (xSemaphoreTake) bevor teardownClient laeuft und danach geben; s_mutex NICHT neu erzeugen. Zusaetzlich vor dem Teardown auf s_hs_running==false warten bzw. den laufenden authTask sauber beenden, damit kein Task den alten Mutex haelt.

<details><summary>Verifikations-Notiz</summary>

net_console.cpp:292 in stopNetConsole() fuehrt `s_mutex = xSemaphoreCreateMutex();` aus und ueberschreibt damit den bestehenden Mutex-Handle (Copy-Paste aus startNetConsole:283). Folge deterministisch: alter Mutex wird geleakt (bei jedem on/off-Toggle), und teardownClient():117 macht xSemaphoreGive auf den soeben erzeugten, nie genommenen Mutex. Erreichbar: esp32_main.cpp:3660 ruft stopNetConsole() im else-Zweig, wenn bNETCONSOLE zur Laufzeit auf 0 gesetzt wird; nach vorherigem startNetConsole ist s_started=true, der Guard greift nicht. Kompiliert fuer beide Boards: net_console.cpp nur durch `#if defined(ESP32) && !defined(DISABLE_NET_CONSOLE)` geguarded; weder variants/wireless-paper noch variants/vision-master-e213 definieren DISABLE_NET_CONSOLE (nur RAK/nRF52). Gebrochener Ausschluss/dangling fd bei gleichzeitigem authTask (Zeile 216/386) ist real, aber engeres Race-Fenster. Kein sofortiger Crash -> gelb passt (langsamer Heap-Leak bei Config-Toggle + Korrektheits/Race-Problem).

</details>

---

### 🟡 [CRIT-2] Fix#4 loest bei reiner Server-Ausfallzeit periodischen WiFi-Teardown-Zyklus aus (Self-DoS)

- **Board:** WP + E213 · **Bereich:** Completeness-Kritiker · **Verdikt:** `PLAUSIBLE` · **Quelle:** reasoning
- **Stelle:** [src/esp32/esp32_main.cpp:3561](src/esp32/esp32_main.cpp#L3561)

**Beweis:**
```cpp
else { printfdeb("...WiFi CONNECTED, server unresponsive, waiting
"...); #if defined(WP_DISP_PREVIEW) if(++wpHbStuck >= 3) { ... resetMeshComUDP(); wpHbStuck = 0; } #endif } last_upd_timer = millis();
```

**Ausfallszenario:** Preview-Builds (WP_PREVIEW / E213_PREVIEW, also WP_DISP_PREVIEW gesetzt): WiFi/Internet stehen, aber der Upstream-MeshCom-Server ist voruebergehend down (Wartung/Ausfall - realistischer Dauerbetriebsfall). Der Heartbeat-Timeout (Stage 2, hb_age > MAX_HB_RX_TIME=65 s) feuert dann bei WiFi==CONNECTED wiederholt in den else-Zweig; wpHbStuck wird nur zurueckgesetzt, wenn wieder ein HB unter HB_WARN_TIME=35 s eintrifft (Zeile 3519) - bei anhaltendem Serverausfall passiert das nie. Nach 3 Stage-2-Zyklen (~3x65 s ~= 3,25 min) ruft der Node resetMeshComUDP() -> Udp.stop() + WiFi.disconnect(true,true) + hasIPaddress=false (udp_functions.cpp:708-712). Der Teardown behebt den serverseitigen Ausfall nicht, also wiederholt sich der Zyklus endlos: alle ~3 min reisst der WP/E213 seine WiFi-Verbindung ab und baut sie neu auf. Folge: die per WiFi/UDP angebundene Phone-App verliert alle ~3 min die Verbindung, in-flight UDP-Pakete gehen verloren, und der Node oszilliert waehrend der gesamten (evtl. stundenlangen) Server-Downtime im Reconnect - eine selbst zugefuegte Dauer-Instabilitaet, ausgeloest allein durch eine benigne Upstream-Bedingung, die der Node nicht beheben kann.

**Fix-Vorschlag (NICHT angewandt):** wpHbStuck-Reset nicht nur an einen frischen HB, sondern auch an einen erfolgreichen Reconnect koppeln und den Reset-Hammer deckeln: entweder resetMeshComUDP nur EINMAL pro Reconnect-Versuch (Flag, das erst nach erfolgreichem HB wieder freigegeben wird) statt in Endlosschleife, oder exponentielles Backoff (z.B. 3->6->12 Zyklen) mit Obergrenze, damit ein reiner Serverausfall bei intaktem WiFi nicht dauerhaft die WiFi-Verbindung niederreisst. Zusaetzlich vor dem Teardown pruefen, ob WiFi wirklich das Problem ist (es ist hier per Definition CONNECTED) und ansonsten nur den UDP-Socket (Udp.stop()/begin()) erneuern statt WiFi.disconnect(true,true).

<details><summary>Verifikations-Notiz</summary>

Nachtrag Completeness-Kritiker

</details>

---

### 🟡 [DISP-3] Panel-Auto-Erkennung ohne Validierung/Fallback -> Fehlerkennung stellt Display dauerhaft tot

- **Board:** WP + E213 · **Bereich:** Display/E-Ink · **Verdikt:** `CONFIRMED` · **Quelle:** reasoning
- **Stelle:** [src/esp32/esp32_functions.cpp:131](src/esp32/esp32_functions.cpp#L131)

**Beweis:**
```cpp
delay(100);
    int busy = digitalRead(PIN_DISPLAY_BUSY);
    ...
    return (busy == LOW);   // IDLE LOW = SSD1680/E0213A367 ; IDLE HIGH = LCMEN2R13EFC1
```

**Ausfallszenario:** detectEinkPanelIsE0213() entscheidet ueber EINEN einzigen digitalRead 100 ms nach dem Reset-Puls, ohne Retry, Plausibilitaets-Check oder Board-Hardcode. Auch das E213 (Hardware fix = immer E0213A367/SSD1680) haengt komplett an dieser Laufzeit-Probe (initDisplay Z.146-155, kein Fallback). Liest der Pin durch Unit-Streuung, marginale POR-Zeit des SSD1680 oder eine floatende Leitung (pinMode INPUT ohne Pull, Z.124) faelschlich HIGH, wird der LCMEN2R13EFC1-Treiber geladen. Dessen wait() pollt invers (while BUSY==LOW), waehrend der SSD1680 im Idle LOW treibt -> jeder wait() laeuft in den 8-s-Timeout, pro update() mehrfach (activate()+sendImageData). Das Display ist damit bis zum Reboot praktisch tot und loop() wird bei jedem Refresh sekundenlang blockiert. Umgekehrter Fall (E0213-Treiber auf LCMEN) analog.

**Fix-Vorschlag (NICHT angewandt):** Mehrfach abtasten (z.B. 3-5 Reads ueber einige ms, Mehrheitsentscheid) statt Einzelsample; PIN_DISPLAY_BUSY mit definiertem Pull konfigurieren; fuer BOARD_E213 (fixe Hardware) das Panel hart auf E0213A367 setzen und die BUSY-Probe nur fuer BOARD_WIRELESS_PAPER verwenden.

<details><summary>Verifikations-Notiz</summary>

Code exakt wie behauptet: esp32_functions.cpp:124 pinMode(PIN_DISPLAY_BUSY, INPUT) (kein Pull), :131 einzelner digitalRead 100ms nach Reset-Puls, :135 return (busy==LOW) - kein Retry/Validierung/Fallback. Guard #if defined(WP_DISP) (Z.141) = BOARD_WIRELESS_PAPER||BOARD_E213 -> beide Boards; E213 haengt trotz fixer Hardware voll an der Laufzeit-Probe (initDisplay Z.146-155, kein Hardcode). Inverse BUSY-Polaritaet in beiden Treibern verifiziert: LCMEN2R13EFC1::wait() 'while(digitalRead(pin_busy)==LOW)' (hardware.cpp:107) mit 8s-Timeout (Z.109); BaseDisplay::wait() 'while(digitalRead(pin_busy)==HIGH)' (hardware.cpp:70). Fehlerkennung -> falscher Treiber -> jeder wait() laeuft in 8s-Timeout, mehrfach pro update() -> loop() sekundenlang blockiert, Display bis Reboot tot. Mechanismus real. Restrisiko nur bei Wahrscheinlichkeit: BUSY wird nach Reset+Recovery aktiv vom Controller getrieben (nicht floatend bei vorhandenem, versorgtem Panel), Fehlerkennung braucht Unit-Streuung/marginale POR/getrenntes Panel - genau das deckt gelb (Robustheit, selten sofort) ab. Severity passt.

</details>

---

### 🟡 [DISP-4] V1.2/E213-Statuszeile: 10-s-Teilrefresh ohne periodischen Voll-Refresh -> SSD1680-Ghosting

- **Board:** WP + E213 · **Bereich:** Display/E-Ink · **Verdikt:** `PLAUSIBLE` · **Quelle:** reasoning
- **Stelle:** [src/loop_functions.cpp:1699](src/loop_functions.cpp#L1699)

**Beweis:**
```cpp
epaper_display.setWindow(0, 0, 250, 16);
        epaper_display.fastmodeOn();
        epaper_display.clearMemory();
        ...
        epaper_display.update();
        epaper_display.setWindow(0, 0, 250, 122);
```

**Ausfallszenario:** Auf V1.2 (E0213A367) und E213 (bV11=false) wird die oberste 16px-Statuszeile alle 10 s ausschliesslich per Partial-Update (fastmodeOn, Partial-LUT) refresht. Ein Voll-Refresh dieser Region erfolgt nur, wenn sich die Seite wechselt (fastmodeOff+update). Bei einem im Ruhezustand statisch stehenden Node (keine Seitenwechsel) wird die Statuszeile stundenlang nur partiell aktualisiert. SSD1680-Partial-Updates ohne periodischen Voll-Refresh akkumulieren laut Controller-Vorgabe Ghosting/Restschatten -> die Uhr/Akku-Zeile wird zunehmend unscharf. V1.1 vermeidet das (dort 60-s-Voll-Refresh, Z.1662/1687-1693), fuer V1.2 fehlt ein entsprechender Zaehler.

**Fix-Vorschlag (NICHT angewandt):** Auf dem V1.2/E213-Pfad einen Zaehler fuehren und z.B. nach N Partial-Refreshes (oder alle paar Minuten) einmal fastmodeOff()+Voll-Refresh der Statuszeile erzwingen, um Ghosting zurueckzusetzen.

<details><summary>Verifikations-Notiz</summary>

Code existiert exakt an loop_functions.cpp:1699-1707: else-Zweig macht setWindow(0,0,250,16)+fastmodeOn()+clearMemory()+update() ohne periodischen Voll-Refresh; der V1.1-Zweig (1687-1693) macht dagegen fastmodeOff()+update() (Voll-Refresh, gated auf 60s via 1662). Panel-Erkennung esp32_functions.cpp:146-155 setzt g_wp_panel_name='E0213A367' fuer WP V1.2 UND E213 -> bV11=false (1648) -> else-Zweig laeuft auf beiden Boards. Kein Voll-Refresh-Zaehler fuer V1.2 vorhanden. Szenario real, ABER entschaerft: jeder Seitenwechsel/neue Nachricht loest fastmodeOff()+update() aus (z.B. wpShowStoredMessage 1749-1750), was das Ghosting zuruecksetzt - Akkumulation nur bei stunden-idle Node ohne Events. SSD1680-Partial-Ghosting ist real, aber Groesse ist library/hardware-intern und der Effekt ist rein kosmetisch (Display-Lesbarkeit), keine Firmware-Stabilitaetsfolge -> daher PLAUSIBLE statt CONFIRMED.

</details>

---

### 🟡 [E213-5] E213-Serie (non-preview) fehlt der Grau-Fix beim Akku-leer-Deepsleep

- **Board:** Vision Master E213 · **Bereich:** E213-spezifisch · **Verdikt:** `PLAUSIBLE` · **Quelle:** reasoning
- **Stelle:** [src/command_functions.cpp:818](src/command_functions.cpp#L818)

**Beweis:**
```cpp
#if defined(WP_DISP_PREVIEW)
            Platform::loraToSleep();
            delay(200);   // LiPo erholt sich nach Lastwegnahme
            #endif
            wpShowDeepSleep();
```

**Ausfallszenario:** Der Low-Voltage-Auto-Deepsleep in batt_functions.cpp (Zeile 357-360, unter WP_DISP -> auch E213) setzt bWpAkkuLow=true und ruft commandAction("--deepsleep"). Der Grau-Fix (SX1262 vor dem E-Ink-Voll-Refresh schlafen legen + 200 ms Erholung) ist aber nur unter WP_DISP_PREVIEW aktiv. Das Standard-Env vision-master-e213 setzt KEIN E213_PREVIEW -> der energiehungrige AKKU-LOW-Voll-Refresh in wpShowDeepSleep() laeuft, waehrend der SX1262 noch im RX ~mehrere mA zieht. Bei fast leerem Akku (Cutoff ~3.26 V) schwingt der interne Display-Boost nicht an -> die AKKU-LOW-Anzeige mit der Spannungs-History wird grau/unlesbar, genau in dem Moment, in dem sie den User informieren soll. WP ist nicht betroffen, weil es als Preview-Build ausgeliefert wird.

**Fix-Vorschlag (NICHT angewandt):** Den loraToSleep()+delay(200)-Vorlauf fuer den Akku-leer-Pfad auch fuer E213 scharf schalten, z.B. Guard von '#if defined(WP_DISP_PREVIEW)' auf '#if defined(WP_DISP_PREVIEW) || defined(BOARD_E213)' erweitern (Platform::loraToSleep() ist fuer E213 bereits forward-deklariert). Alternativ generell fuer WP_DISP, da wpShowDeepSleep() den LoRa-RX-Strom sowieso ueber prepareToSleep() erst NACH dem Refresh entfernt.

<details><summary>Verifikations-Notiz</summary>

Code bestaetigt: command_functions.cpp:818 guardet loraToSleep()+delay(200) mit WP_DISP_PREVIEW; wpShowDeepSleep() (830) laeuft ungeguarded unter WP_DISP. platformio.ini env:vision-master-e213 (Basis) setzt kein E213_PREVIEW (nur die -preview-Env, Z.62), configuration_global.h:53 definiert WP_DISP_PREVIEW nur bei WP_PREVIEW||E213_PREVIEW -> Basis-E213 kompiliert den Grau-Fix NICHT. Pfad erreichbar: batt_functions.cpp:309 (BAT_MIN_VOLTAGE=3.3V scharf fuer E213 laut Kommentar 304-308), :357 setzt bWpAkkuLow=true unter WP_DISP, :360 ruft --deepsleep. wpShowDeepSleep (loop_functions.cpp:1840) macht Voll-Refresh mit AKKU-LOW-Anzeige unter WP_DISP; loraToSleep() laeuft fuer E213-non-preview erst spaeter via prepareToSleep() (power_controls.cpp:70), also NACH dem Refresh. Kausalkette im Code stimmt. Restunsicherheit: der Grau-Effekt ist ein board-/elektrik-spezifisches Phaenomen, entwickelt/beobachtet fuer WP-Hardware; ob das andere E213-Board (ESP32-S3R8, eigener Reglerpfad) es tatsaechlich zeigt, ist aus dem Code nicht verifizierbar -> daher PLAUSIBLE, nicht CONFIRMED.

</details>

---

### 🟡 [HANG-6] E-Ink BUSY-Poll blockiert Loop bis zu 8 s pro wait() (kumulativ pro Refresh)

- **Board:** WP + E213 · **Bereich:** Hang/Watchdog · **Verdikt:** `PLAUSIBLE` · **Quelle:** reasoning
- **Stelle:** [src/Displays/BaseDisplay/hardware.cpp:70](src/Displays/BaseDisplay/hardware.cpp#L70)

**Beweis:**
```cpp
while(digitalRead(pin_busy) == HIGH) {      // Pin is HIGH when busy
        yield();
        if ((millis() - start) > 8000)          // Timeout 8 s
            break;
    }
```

**Ausfallszenario:** BaseDisplay::wait() (WP V1.2 / E213 / E290) und analog LCMEN2R13EFC1::wait() (WP V1.1, hardware.cpp:107, invertiert, ebenfalls 8000 ms) pollen den BUSY-Pin rein im Loop-Kontext mit yield(). Der 8-s-Timeout verhindert zwar den frueheren Endlos-Hang, aber bei einem tatsaechlich nicht antwortenden BUSY (falsch initialisiertes Panel, fehlgeschlagener partial-window/OTP-Refresh bei fast leerem Akku, Leckstrom am hochohmigen BUSY) friert der Haupt-Loop pro wait()-Aufruf bis zu 8 s ein. Ein Voll-Refresh ruft wait() mehrfach sequentiell (u.a. hardware.cpp:54,59,205 sowie Setup 19/23/37/40/75/99 bei LCMEN) -> kumuliert Dutzende Sekunden, in denen checkRX() nicht laeuft und LoRa-Pakete verloren gehen, Button/Batterie-Check blockiert sind. Kein Reset (yield() panic't den Default-TWDT nicht), aber deutlich spuerbarer Dauer-Stall unter Panel-Fehler.

**Fix-Vorschlag (NICHT angewandt):** Timeout deutlich senken (E-Ink-Voll-Refresh <=2 s, also z.B. 3000 ms statt 8000 ms) und wait() ggf. kooperativ machen (State-Machine statt Blockieren), sodass der Loop zwischen den Poll-Intervallen LoRa-RX weiter bedient. Mindestens: nach Timeout einen Fehlerzaehler/Flag setzen und weitere Display-Updates bis zum naechsten sauberen Reset unterdruecken.

<details><summary>Verifikations-Notiz</summary>

Code existiert exakt: BaseDisplay::wait() src/Displays/BaseDisplay/hardware.cpp:68-75 (while-Zeile 70: `while(digitalRead(pin_busy) == HIGH) { yield(); if ((millis() - start) > 8000) break; }`) und invertiert LCMEN2R13EFC1::wait() src/Displays/LCMEN2R13EFC1/hardware.cpp:105-111 (`== LOW`, ebenfalls 8000 ms). Beide kompilieren fuer die Scope-Boards (BaseDisplay -> WP V1.2/E213/E290, LCMEN -> WP V1.1), kein ausschliessender Guard. Mehrfach-wait() pro Refresh bestaetigt (reset 54/59, endImageTxQuiet 205, LCMEN activate 19/23, sendImageData 75). Der Loop-Stall ist real und blockiert checkRX() synchron. ABER: der Stall greift NUR unter echtem Hardware-Fehler (BUSY-Pin de-assertiert nie); im Normalbetrieb (<=2 s Refresh) feuert der Timeout nie. Der 8-s-Timeout ist bereits eine bewusste Mitigation (ersetzt frueheren Endlos-Hang durch begrenzten Stall) - der Befund beschreibt nur das Restverhalten danach. Die LoRa-Paketverlust-Kette haengt von poll-basiertem RX im selben Loop ab (teils SX1262-library-intern), daher Restunsicherheit. gelb ist fuer eine reine Fault-Robustheitsluecke angemessen.

</details>

---

### 🟡 [HANG-7] Endlos-Busywait im Spektral-Scan ohne Timeout

- **Board:** WP + E213 · **Bereich:** Hang/Watchdog · **Verdikt:** `PLAUSIBLE` · **Quelle:** reasoning
- **Stelle:** [src/spectral_scan.cpp:114](src/spectral_scan.cpp#L114)

**Beweis:**
```cpp
while (radio.spectralScanGetStatus() != RADIOLIB_ERR_NONE){
        delay(10);
      }
```

**Ausfallszenario:** Ausgeloest per Mesh-/Serial-Kommando `--spectrum` (command_functions.cpp:559 -> sx126x_spectral_scan()). Die innere Schleife pollt spectralScanGetStatus() OHNE jeden Timeout/Abbruch. Kehrt der SX1262 den Status wegen SPI-Glitch, marginaler Versorgung (leerer Akku/Brownout) oder haengendem Scan-Patch nie auf RADIOLIB_ERR_NONE, dreht der Haupt-Loop endlos in delay(10). Da kein Task-Watchdog konfiguriert ist (siehe Vor-Audit STAB-04), erfolgt KEIN automatischer Reset: LoRa-RX/TX, Display, Button, Batterie-Ueberwachung sind tot bis manueller Power-Cycle. Zusaetzlich blockiert schon der Normalfall den Loop ueber die gesamte Scan-Dauer (aeussere while(freqStart<=freqEnd) ueber ~50 Frequenzen mit delay(100)).

**Fix-Vorschlag (NICHT angewandt):** Busywait mit Deadline absichern: uint32_t t0=millis(); while(status!=ERR_NONE){ if(millis()-t0 > 3000) { /* Scan abbrechen, sx126x_spectral_finish_scan(), Fehler zurueck */ break; } delay(10); }. Rueckgabe-Array auf Zero belassen und Fehler loggen statt endlos zu warten.

<details><summary>Verifikations-Notiz</summary>

Code an spectral_scan.cpp:114 ist wortgleich vorhanden und kompiliert fuer WP UND E213 (Guard :29 hat defined(SX1262_E290); beide variants/*/configuration.h:36 definieren SX1262_E290 -> reale Impl statt Dummy). RadioLib SX126x.cpp:1234-1241 bestaetigt: spectralScanGetStatus() hat KEINEN internen Timeout, gibt nur bei Registerwert COMPLETED ERR_NONE zurueck, sonst RADIOLIB_ERR_RANGING_TIMEOUT -> Schleife kann bei stecken gebliebenem Statusregister unbegrenzt in delay(10) drehen, kein Watchdog-Reset. Defekt real. ABER: Ausloesung nur ueber bewusstes, experimentelles Admin-Debug-Kommando --spectrum (command_functions.cpp, #ifndef BOARD_T_DECK_PRO), das im Header selbst als experimentell/entfernbar markiert ist; kein dauerlaufender Pfad, nicht durch Fremddaten (LoRa/UDP-Empfang) spontan erreichbar. Der Hang braucht zusaetzlich einen echten HW-Fehler. Selbstverschuldeter Debug-Trigger + HW-Fault-Bedingung -> Exposition geringer als orange, korrekt als Medium/gelb (Robustheitsmangel: fehlender Timeout).

</details>

---

### 🟡 [NET-8] Off-by-one OOB-Write beim Null-Terminieren des UDP-Empfangspuffers

- **Board:** alle Boards · **Bereich:** Netzwerk/Eingabe · **Verdikt:** `CONFIRMED` · **Quelle:** reasoning
- **Stelle:** [src/udp_functions.cpp:104](src/udp_functions.cpp#L104)

**Beweis:**
```cpp
unsigned char incomingPacket[UDP_TX_BUF_SIZE];  // =255
...
int len = Udp.read(incomingPacket, UDP_TX_BUF_SIZE);
if (len > 0) { incomingPacket[len] = 0;
```

**Ausfallszenario:** Udp.read(buf, 255) liefert bei einem UDP-Datagramm >=255 Bytes genau len=255 zurueck. incomingPacket ist [255] (gueltige Indizes 0..254). incomingPacket[len] = incomingPacket[255] = 0 schreibt ein Byte hinter das Array -> Undefined Behavior. Das benachbarte globale 'int packetSize' (Zeile 68) wird zwar sofort wieder ueberschrieben, aber bei geaenderter Linker-Anordnung kann die Ueberschreibung ein anderes Symbol treffen. Identisches Muster in src/extudp_functions.cpp:227 (incomingExtPacket[len]=0).

**Fix-Vorschlag (NICHT angewandt):** Puffer auf [UDP_TX_BUF_SIZE+1] vergroessern ODER len vor dem Null-Setzen begrenzen: if (len >= UDP_TX_BUF_SIZE) len = UDP_TX_BUF_SIZE-1; erst dann incomingPacket[len]=0. Gleicher Fix in extudp_functions.cpp:227.

<details><summary>Verifikations-Notiz</summary>

Code existiert exakt: configuration_global.h:70 UDP_TX_BUF_SIZE=255; udp_functions.cpp:67 incomingPacket[255] (Indizes 0..254), Zeile 100 len=Udp.read(incomingPacket, 255), Zeile 104 incomingPacket[len]=0. Kompiliert fuer WP+E213 (ESP32-S3, keine Board-Guards auf udp_functions.cpp). Weder packetSize>0 (Z.98) noch len>0 (Z.102) begrenzen len nach oben; WiFiUDP.read liefert min(available,255), also len==255 bei Datagramm >=255 Bytes -> incomingPacket[255]=0 ist OOB-Write (UB). Identisch in extudp_functions.cpp:43/221/227. Erreichbar via uebergrosses UDP-Datagramm (feindlicher/fehlerhafter Peer), aber untypisch fuer Normalverkehr. Ein-Byte-Null-Write past-end, direktes Opfer packetSize wird sofort ueberschrieben -> Impact begrenzt/selten, daher gelb korrekt.

</details>

---

### 🟡 [SUPPLY-9] --info-Ausgabe druckt das Net-Console-Passwort im Klartext, entgegen der eigenen No-Print-Policy

- **Board:** WP + E213 · **Bereich:** Secrets/Supply-Chain · **Verdikt:** `PLAUSIBLE` · **Quelle:** reasoning
- **Stelle:** [src/command_functions.cpp:4729](src/command_functions.cpp#L4729)

**Beweis:**
```cpp
printfdeb("...NOMSGALL %s ...MESH %s ...BUTTON (%i) %s ...SOFTSER %s ... SOFTSERREAD %s
...PASSWD <%s>
", ..., meshcom_settings.node_passwd);
```

**Ausfallszenario:** Der --info-Befehl gibt node_passwd (das Net-Console-Passwort) im Klartext aus. Dies widerspricht direkt dem Kommentar bei command_functions.cpp:2989 ('--passwd without argument: show current status (never print the actual password)'). printfdeb schreibt auf die (Mesh-)Serial-Konsole; ueber eine bereits verbundene, authentifizierte Net-Console-Session ist die --info-Ausgabe damit fuer den Client sichtbar und wird lokal auf UART0 geloggt. Credential-Leak sobald ein Passwort gesetzt ist. Gilt fuer WP und E213.

**Fix-Vorschlag (NICHT angewandt):** Im --info-Block das Passwort nicht ausgeben; stattdessen wie in der --passwd-Statuslogik nur 'gesetzt/leer' anzeigen (z.B. '...PASSWD <%s>' durch '...PASSWD <%s>' mit hasPasswd?"set":"none" ersetzen).

<details><summary>Verifikations-Notiz</summary>

Code existiert exakt an command_functions.cpp:4729 im --info-Handler innerhalb if(!bRxFromPhone): printfdeb("...PASSWD <%s>
", ..., meshcom_settings.node_passwd) - druckt das Net-Console-Passwort im Klartext, entgegen der Policy bei Zeile 2989 ('never print the actual password'). Kein Board-Guard, kompiliert fuer WP und E213. Leak-Mechanismus bestaetigt: auf ESP32 ist Serial==MSerial (net_console.cpp:44); printfdeb->Serial.printf->MeshSerialClass::write (net_console.cpp:254-266) spiegelt Ausgabe auf UART0 UND bei s_authenticated per raw_write_all zum Net-Console-Client. Befund ist real. ABER Severity orange ist fuer diesen Stabilitaets-Audit zu hoch: orange ist Stabilitaetsverlust unter Sonderbedingungen, ein Passwort-Print destabilisiert nichts; Schema ordnet Secrets explizit niedrig ein; Net-Console-Leser muss das Passwort ohnehin bereits zur HMAC-Auth besitzen. Echte Neu-Exposition ist Klartext auf UART0/in Logs -> Info-Disclosure/Korrektheit = gelb.

</details>

---

### 🟡 [SUPPLY-10] Net-Console-Passwort wird bei jeder Authentifizierung im Klartext auf die serielle Konsole geloggt

- **Board:** WP + E213 · **Bereich:** Secrets/Supply-Chain · **Verdikt:** `PLAUSIBLE` · **Quelle:** reasoning
- **Stelle:** [src/net_console.cpp:171](src/net_console.cpp#L171)

**Beweis:**
```cpp
Serial.printf("[CON ]...s_password:<%s> lng:%i resoBuf:<%s>
", s_password, strlen(s_password), respBuf);
```

**Ausfallszenario:** net_console.cpp wird fuer WP und E213 kompiliert (kein -D DISABLE_NET_CONSOLE in variants/wireless-paper bzw. E213; nur rak4631/t114/t_echo/E22_XML setzen es). Ab Zeile 44 gilt '#define Serial MSerial', d.h. dieser printf laeuft ueber MeshSerialClass::write und schreibt IMMER auf s_hwSerial (UART0/CP2102, die lokale USB-Konsole der WP). Bei JEDEM authTask-Durchlauf (jeder eingehende TCP-Verbindungsversuch auf Port 2323 mit gesetztem Passwort) wird das konfigurierte Konsolen-Passwort im Klartext ausgegeben. Damit ist das gesamte HMAC-Design ('The password is never transmitted', net_console.h) untergraben: jeder mit Zugriff auf die serielle Konsole liest das Passwort mit.

**Fix-Vorschlag (NICHT angewandt):** Diese Debug-Zeile ersatzlos entfernen oder das Passwort maskieren (nur Laenge/Praesenz loggen, z.B. 'resp lng:%i'). Niemals s_password ausgeben.

<details><summary>Verifikations-Notiz</summary>

Code an net_console.cpp:171 existiert verbatim (Serial.printf mit s_password). Nach net_console.h Z.75-76 ist Serial==MSerial; MeshSerialClass::write (Z.242/256) schreibt s_password UNBEDINGT auf s_hwSerial (lokale UART0/USB). Kompiliert fuer WP UND E213 (kein DISABLE_NET_CONSOLE in beiden Varianten, beide ESP32-S3), erreichbar bei jedem authTask (Z.386) mit gesetztem Passwort + readOk. ABER: zum Zeitpunkt des printf ist s_authenticated noch false (erst ~Z.221), d.h. der printf geht NUR auf lokale UART, NICHT an den (unauth.) TCP-Client -> kein Netzwerk-Leak an den Angreifer. HMAC-Netzprotokoll bleibt intakt ('never transmitted' bezog sich auf Netz). Ausnutzung braucht lokalen Seriell-Zugriff (bereits hochprivilegiert) oder bestehende authentifizierte Konsole. Kein Stabilitaetseffekt. Realer Defekt (Debug-printf), aber orange zu hoch -> gelb.

</details>

---

## 🔵 Blau · Low/Hinweis — 7 Befund(e)

### 🔵 [ARITH-1] millis()-49.7-Tage-Wrap: 'timer + intervall < millis()' statt '(millis()-timer) > intervall'

- **Board:** alle Boards · **Bereich:** Arithmetik/Zeit · **Verdikt:** `CONFIRMED` · **Quelle:** reasoning
- **Stelle:** [src/batt_functions.cpp:265](src/batt_functions.cpp#L265)

**Beweis:**
```cpp
if ((batt_show_timer + (1000 * std::max(1,BATTshowtime))) < millis())  // gleiches Muster u.a. esp32_main.cpp:1837,1877,1893,2059,3182,3278,3344,3507; loop_functions.cpp:3609,3617
```

**Ausfallszenario:** Das additive Muster 'timer + intervall < millis()' ist gegen den millis()-Ueberlauf (alle ~49.7 Tage Dauerbetrieb) nicht robust: nahe UINT32_MAX ueberlaeuft 'timer + intervall' und wird kleiner als millis(), sodass der Zweig fuer einen Wrap-Zyklus zu frueh/kontinuierlich feuert. In diesem konkreten Fall nur ein kurzzeitig zu haeufiger BATT-Debug-Print; das Muster tritt aber dutzendfach in dauernd laufenden Timern auf (LoRa-CSMA/Retransmit, Telemetrie, Sensor-Poll, Web-Refresh). Die meisten Instanzen self-healen, weil der Timer jede Iteration neu auf millis() gesetzt wird - daher niedrige Severity - aber es bleibt ein Wrap-Glitch pro ~49.7 Tagen je Timer.

**Fix-Vorschlag (NICHT angewandt):** Durchgaengig auf die ueberlauf-sichere Differenzform umstellen: '(millis() - batt_show_timer) >= (unsigned long)(1000UL * std::max(1,BATTshowtime))'. Unsigned-Subtraktion ist gegen den Wrap immun. Gleiches Refactoring fuer die o.g. Fundstellen.

<details><summary>Verifikations-Notiz</summary>

Code an src/batt_functions.cpp:265 exakt bestaetigt: `if ((batt_show_timer + (1000 * std::max(1,BATTshowtime))) < millis())`. batt_show_timer ist `unsigned long` (Zeile 25), also uint32 auf ESP32-S3 -> additiver Ueberlauf nahe UINT32_MAX moeglich. Keine Board-Guard um diese Zeile, kompiliert fuer WP + E213 + alle. Wrap-Glitch real, aber self-healing (batt_show_timer=millis() Zeile 267) und Folge nur kurzzeitig zu haeufiger [BATT]-Debug-Print (nur bei bDisplayCont). Severity blau (Code-Smell ohne echte Stabilitaetsfolge) korrekt. Zeile stimmt.

</details>

---

### 🔵 [E213-2] BaseDisplay::landscape() kennt Vision_Master_E213 nicht -> latenter 180°-Flip

- **Board:** Vision Master E213 · **Bereich:** E213-spezifisch · **Verdikt:** `CONFIRMED` · **Quelle:** reasoning
- **Stelle:** [src/Displays/BaseDisplay/layout.cpp:189](src/Displays/BaseDisplay/layout.cpp#L189)

**Beweis:**
```cpp
void BaseDisplay::landscape() {
    #if defined(Vision_Master_E290)
        setRotation(1);
    #elif defined(WIRELESS_PAPER)
        setRotation(1);
    #else
        setRotation(3);   // <- E213 landet hier
    #endif
}
```

**Ausfallszenario:** landscape() hat Zweige fuer Vision_Master_E290 und WIRELESS_PAPER, aber KEINEN fuer Vision_Master_E213. Auf dem E213 faellt der Aufruf in den else-Zweig setRotation(3) = 270°, also 180° GEGENUEBER Kurts gewuenschter E213-Ausrichtung (startDisplay setzt danach setRotation(90)=1). Aktuell harmlos, weil der EINZIGE landscape()-Aufrufer (esp32_functions.cpp startDisplay) unmittelbar mit setRotation(90) ueberschreibt. Es ist aber eine latente Falle: jeder kuenftige/zusaetzliche landscape()-Aufruf ohne nachfolgendes setRotation(90) wuerde den E213-Inhalt kopfstehend gegenueber dem Boot-Splash rendern (kein Crash, nur falsch orientiert).

**Fix-Vorschlag (NICHT angewandt):** In landscape() einen expliziten E213-Zweig ergaenzen, damit die board-eigene Orientierung an EINER Stelle definiert ist: '#elif defined(Vision_Master_E213) setRotation(1);' (bzw. den fuer E213 gewuenschten Wert), statt sich auf das nachtraegliche Override in startDisplay zu verlassen.

<details><summary>Verifikations-Notiz</summary>

layout.cpp:189-197 landscape() hat Zweige nur fuer Vision_Master_E290 (setRotation(1)) und WIRELESS_PAPER (setRotation(1)), sonst else setRotation(3). E213 definiert das Makro Vision_Master_E213 (VisionMasterE213.h:8), NICHT WIRELESS_PAPER/Vision_Master_E290, faellt also in else=setRotation(3)=270 Grad. BaseDisplay ist Common-Code, kompiliert fuer E213. Einziger Aufrufer esp32_functions.cpp:204 ueberschreibt bei E213 sofort mit setRotation(90) (Zeile 206-207), daher aktuell KEIN Laufzeiteffekt - reine latente Falle fuer kuenftige Aufrufer. Real vorhanden, Board stimmt, Severity blau (kein Crash, nur latente Fehlorientierung) angemessen.

</details>

---

### 🔵 [MEM-3] snprintf mit falschem Groessen-Argument (sizeof(cset) statt sizeof(clfd))

- **Board:** WP + E213 · **Bereich:** Speicher · **Verdikt:** `CONFIRMED` · **Quelle:** cppcheck
- **Stelle:** [src/loop_functions.cpp:2114](src/loop_functions.cpp#L2114)

**Beweis:**
```cpp
char clfd[10];
memset(clfd, 0x00, sizeof(clfd));
snprintf(clfd, sizeof(cset), "%03i", (int)(aprsmsg.msg_id & 0x3FF));  // sizeof(cset)==30, Ziel ist aber clfd[10]
```

**Ausfallszenario:** Im {MCP}-Fernwirk-Pfad von sendDisplayText (laeuft fuer jede eingehende LoRa/UDP-Nachricht, auf WP und E213) bekommt snprintf als Groesse sizeof(cset)=30 uebergeben, obwohl das Zielarray clfd nur 10 Byte gross ist. Aktuell KEIN echter Overflow, weil die formatierte Ausgabe '%03i' von (msg_id & 0x3FF) hoechstens 4 Ziffern (0..1023) erzeugt = 5 Byte. Dennoch latenter Defekt: cppcheck meldet bufferAccessOutOfBounds; jede kuenftige Formataenderung (breiteres Feld / groesserer Wert) wuerde clfd sofort ueberschreiben.

**Fix-Vorschlag (NICHT angewandt):** Groesse korrigieren: snprintf(clfd, sizeof(clfd), "%03i", ...);

<details><summary>Verifikations-Notiz</summary>

loop_functions.cpp:2114 enthaelt exakt `snprintf(clfd, sizeof(cset), "%03i", (int)(aprsmsg.msg_id & 0x3FF));` mit clfd[10] (Z.2112) und cset[30] (Z.2105) — falsches Groessen-Argument bestaetigt. sendDisplayText ist ungeguarded, wird aus udp_functions.cpp (234/239/301) fuer jede eingehende Nachricht aufgerufen, kompiliert also fuer WP und E213. Kein realer Overflow: msg_id & 0x3FF = 0..1023, "%03i" liefert max. "1023" = 5 Byte, passt in clfd[10]; snprintf begrenzt zusaetzlich. Reiner latenter Defekt/Static-Analysis-Befund ohne Laufzeitfolge -> blau korrekt.

</details>

---

### 🔵 [NET-4] Unbegrenzte strText-Akkumulation ohne Newline -> strlen liest OOB

- **Board:** alle Boards · **Bereich:** Netzwerk/Eingabe · **Verdikt:** `PLAUSIBLE` · **Quelle:** reasoning
- **Stelle:** [src/esp32/esp32_main.cpp:3995](src/esp32/esp32_main.cpp#L3995)

**Beweis:**
```cpp
strText[iTxtPos] = rd;
if(iTxtPos < (int)sizeof(strText) - 1) { iTxtPos++; }
...
iTxtLen = strlen(strText);  // Zeile 4026
```

**Ausfallszenario:** Nicht-Preview-Build (Default-Env 'wireless-paper' ohne -D WP_PREVIEW). Zeichen aus USB-Serial ODER NetConsole werden in strText[600] gesammelt und erst bei '
'/'\r' verarbeitet. Ein '--'-Command, der mit '-' beginnt (strText[0]=='-'), wird bei fehlendem Newline NICHT zurueckgesetzt (else-Zweig 4080 greift nur bei fremdem Erstzeichen). Sendet ein NetConsole-Client >=600 Zeichen ohne '
' und ohne Null-Byte, fuellen sich strText[0..599] vollstaendig mit Nicht-Null (iTxtPos bleibt bei 599 stehen), es gibt keinen Terminator. strlen(strText) in Zeile 4026/4043 laeuft dann ueber das 600-Byte-Global hinaus, bis zufaellig ein 0-Byte gefunden wird = Out-of-bounds-Read, remote ueber Telnet ausloesbar.

**Fix-Vorschlag (NICHT angewandt):** Vor dem Schreiben begrenzen und immer terminieren: if(iTxtPos < sizeof(strText)-1) { strText[iTxtPos++]=rd; strText[iTxtPos]='\0'; } sonst Zeile verwerfen/reset. Alternativ bei erreichtem Puffer-Ende (iTxtPos == sizeof-1) die Zeile zwangsweise abschliessen und strText zuruecksetzen, statt das letzte Byte endlos zu ueberschreiben.

<details><summary>Verifikations-Notiz</summary>

Code real: strText[iTxtPos]=rd (3995/4017), cap iTxtPos<sizeof-1 (3996) und iTxtLen=strlen(strText) (4026/4043) auf char strText[600]={0} (Zeile 238). Kompiliert im Default-Env wireless-paper (kein WP_PREVIEW -> #else-Zweig aktiv). Der WRITE ist KEIN Overflow (iTxtPos auf 599 gedeckelt, gueltiger Index). Aber: bei 600 Nicht-Null-Zeichen ohne Newline und strText[0] in {':','-','{'} greift weder der else-Reset (4080/4090) noch der Newline-Zweig (4032); Buffer bleibt voll und ungenullt -> strlen liest OOB. Also real und remote (netconsole) erreichbar. Folgen jedoch harmlos: reiner READ-OOB (alle Writes size-bounded: strncpy 4038, snprintf 4036), auf ESP32-Flat-DRAM kein Fault, benachbartes .bss liefert 0-Terminator; Command wird ohne '
' ohnehin nie ausgefuehrt. Kein Crash/Hang/Datenverlust -> keine Stabilitaetsfolge, daher blau statt gelb.

</details>

---

### 🔵 [NET-5] NetConsole akzeptiert jedes Passwort-Praefix im Klartext (Auth-Schwaeche)

- **Board:** alle Boards · **Bereich:** Netzwerk/Eingabe · **Verdikt:** `PLAUSIBLE` · **Quelle:** reasoning
- **Stelle:** [src/net_console.cpp:174](src/net_console.cpp#L174)

**Beweis:**
```cpp
if(memcmp(respBuf, s_password, strlen(s_password)) != 0) { ...HMAC-Pruefung... } else { authOk = true; }
```

**Ausfallszenario:** Der 'KBC check without SHA256' vergleicht nur die ersten strlen(s_password) Bytes der Client-Antwort mit dem Klartext-Passwort. Sendet ein Client eine Antwort, die mit dem Passwort BEGINNT aber laenger ist (z.B. Passwort 'geheim' -> Antwort 'geheimXYZ'), ist memcmp==0 und der else-Zweig setzt authOk=true. Damit authentifiziert jede Zeichenkette, die das Passwort als Praefix hat; die HMAC-Challenge-Response wird uebersprungen. Der Klartext-Pfad umgeht ausserdem die Timing-sichere ct_equal()-Pruefung, sodass das Passwort ueber die unverschluesselte TCP-Verbindung (Port 2323) mitlesbar ist.

**Fix-Vorschlag (NICHT angewandt):** Klartext-Vergleich auf exakte Laenge pruefen: strlen(respBuf) == strlen(s_password) && memcmp(...)==0. Besser den Klartext-Bypass ganz entfernen und nur die HMAC-Challenge-Response zulassen, da die Verbindung ohnehin unverschluesselt ist und ein Klartext-Passwort sniffbar ist.

<details><summary>Verifikations-Notiz</summary>

Code existiert exakt an net_console.cpp:174: `if(memcmp(respBuf, s_password, strlen(s_password)) != 0)` mit else-Zweig `authOk = true` (Z.195). Kompiliert fuer WP und E213: Guard `#if defined(ESP32) && !defined(DISABLE_NET_CONSOLE)` (Z.22); beide Board-Varianten definieren DISABLE_NET_CONSOLE NICHT (nur rak4631/t_echo/E22/t114), Port 2323 aktiv. Prefix-Match ist real (nur strlen(pw) Bytes verglichen), ABER ein gueltiger Prefix setzt Passwortkenntnis voraus - ein Angreifer OHNE Passwort kommt nicht rein; die Behauptung 'jede Zeichenkette authentifiziert' ueberzeichnet. Realer Kern ist der bewusst eingebaute Klartext-Pfad ('KBC check without SHA256'), der das Passwort im Klartext ueber unverschluesseltes TCP akzeptiert und HMAC/ct_equal umgeht - eine echte, aber absichtliche Auth-Hygiene-Schwaeche ohne jede Stabilitaetsfolge (kein Crash/Hang/Reboot). Daher Severity von gelb auf blau (Hygiene ohne Stabilitaetsimpact). Zeile 174 stimmt.

</details>

---

### 🔵 [NET-6] sendMeshComUDP kopiert msg_len ab Offset +37 -> OOB-Read ueber Ringpuffer-Zeile

- **Board:** alle Boards · **Bereich:** Netzwerk/Eingabe · **Verdikt:** `CONFIRMED` · **Quelle:** reasoning
- **Stelle:** [src/udp_functions.cpp:454](src/udp_functions.cpp#L454)

**Beweis:**
```cpp
uint16_t msg_len = (uint16_t)ringBufferUDPout[udpRead][0]; // bis 255
...
memcpy(convBuffer, ringBufferUDPout[udpRead] + 1 + 36, msg_len);
```

**Ausfallszenario:** Die Ringpuffer-Zeile ist [UDP_TX_BUF_SIZE+20]=275 Bytes gross. msg_len kann bis 255 sein. Der memcpy liest ab Offset 37 (1+36) genau msg_len Bytes, also bis Index 37+255-1=291 > 274 -> bis zu 17 Bytes hinter die aktuelle Ringpuffer-Zeile. In der letzten Ring-Zeile (udpRead==MAX_RING_UDP-1) liegt das hinter dem gesamten 2D-Array. Zusaetzlich ist der Read logisch falsch: die eigentliche Nachricht ab +36 hat nur (msg_len-36) gueltige Nutzbytes, es werden 36 Bytes zu viel gelesen. Trigger nur unter bDisplayInfo (Debug-Anzeige), Ziel convBuffer[305] ist gross genug; Auswirkung daher gering, aber echter Over-Read von Fremd-/Nachbarspeicher.

**Fix-Vorschlag (NICHT angewandt):** Nur die tatsaechliche Nutzlaenge kopieren: Laenge auf (msg_len > 36 ? msg_len-36 : 0) begrenzen und zusaetzlich gegen die Ringzeilengroesse (UDP_TX_BUF_SIZE+20 - 37) klemmen, bevor memcpy aufgerufen wird.

<details><summary>Verifikations-Notiz</summary>

Code existiert exakt so: udp_functions.cpp:423 `msg_len = ringBufferUDPout[udpRead][0]` (Byte, 0-255, in addUdpOutBuffer:1024 auf 255 geklemmt) und :454 `memcpy(convBuffer, ringBufferUDPout[udpRead] + 1 + 36, msg_len)`. Ringpuffer-Zeile ist [MAX_RING_UDP][UDP_TX_BUF_SIZE+20] = [20][275] (loop_functions.cpp:408, UDP_TX_BUF_SIZE=255). Quell-Read reicht bis Index 36+msg_len; fuer msg_len 239..255 also 1..17 Bytes hinter die Zeile bzw. in letzter Zeile (udpRead==19) hinter das gesamte globale Array. Ziel convBuffer[UDP_TX_BUF_SIZE+50]=305 gross genug, also reiner Read-Over, kein Write-Overflow, kein Fault. Board: ESP32-Pfad mit WiFi/UDP -> gilt fuer WP und E213 (beide ESP32 + WiFi). Praezisierung: der memcpy ist UNBEDINGT (nicht durch bDisplayInfo geschuetzt, nur der printBuffer_aprs:466 ist es) -> Erreichbarkeit sogar hoeher als behauptet, Wirkung aber unveraendert benigne. Severity blau passt (begrenzter Read-Only Over-Read von Nachbarspeicher, kein Crash im Normalbetrieb).

</details>

---

### 🔵 [SUPPLY-7] CI laedt und fuehrt ungepinnte Fremd-Skripte/Board-Defs ueber wget ohne Integritaetspruefung aus

- **Board:** alle Boards · **Bereich:** Secrets/Supply-Chain · **Verdikt:** `CONFIRMED` · **Quelle:** reasoning
- **Stelle:** [.github/workflows/meshcom-ci.yml:59](.github/workflows/meshcom-ci.yml#L59)

**Beweis:**
```cpp
run: "wget https://raw.githubusercontent.com/microsoft/uf2/refs/heads/master/utils/uf2conv.py && ... && python3 uf2conv.py ..."  (auch Zeile 53: wget t-beams3-supreme.json von Xinyuan-LilyGO/master)
```

**Ausfallszenario:** Der Release-Build (Trigger: push tag, permissions contents:write) zieht zur Buildzeit uf2conv.py/uf2families.json von microsoft/uf2@master und eine Board-JSON von Xinyuan-LilyGO@master per wget ohne Commit-Pin oder Checksumme und fuehrt das Python-Skript aus. Aendert sich das Upstream-master (kompromittiert/geloescht), wird beliebiger Code im Release-Job mit Schreibrechten auf die Repo-Releases ausgefuehrt bzw. der Build bricht. Betrifft die Firmware-Artefakte aller Boards inkl. WP/E213 (vision-master-e213/wireless-paper.bin werden im selben Job als Release publiziert). Kein pull_request_target -> kein Fremd-PR-Vektor, daher niedrig.

**Fix-Vorschlag (NICHT angewandt):** Skripte auf einen festen Commit-SHA pinnen (raw.githubusercontent.com/microsoft/uf2/<sha>/...) und nach dem Download eine sha256-Pruefsumme verifizieren, bevor python3 sie ausfuehrt; alternativ die Tools als vendored Datei ins Repo aufnehmen. Zusaetzlich actions/* per SHA pinnen.

<details><summary>Verifikations-Notiz</summary>

Code existiert exakt: meshcom-ci.yml:59 zieht per wget uf2conv.py + uf2families.json von microsoft/uf2@master (unpinned, keine Checksumme) und fuehrt `python3 uf2conv.py` aus; identisch Z.62 und Z.65. Z.53 holt t-beams3-supreme.json von Xinyuan-LilyGO@master unpinned. Trigger `on.push.tags` (Z.4-6) plus `permissions: contents: write` (Z.13-14). Der Job baut via `pio run` (Z.56) alle Boards inkl. wireless-paper/vision-master-e213 und publiziert sie im selben Job (Z.121-122), sodass kompromittierter Upstream-Code alle Release-Artefakte manipulieren kann. Kein pull_request_target -> kein Fremd-PR-Vektor, daher zu Recht niedrig. Severity blau passt (CI-/Supply-Chain-Hygiene ohne direkte Laufzeit-/Stabilitaetsfolge fuer die Firmware, low-probability Upstream-Kompromiss).

</details>

---

## Anhang A — cppcheck 2.21 (Tool-Rohbefunde)

Deterministisches Ausgangsmaterial, getrennt von den verifizierten Befunden oben. Die zwei `error`-Kandidaten wurden von den Agenten aufgegriffen (u. a. Finding **MEM/blau** `snprintf`-Größenargument).

```
cppcheck 2.21.0 — In-Scope WP+E213 — 2026-07-01 (HEAD 8da79a2)
Kategorien: 4 error, 51 warning, 1 performance

ECHTE error-Kandidaten (2 Parsing-Limits ausgeblendet):
src/loop_functions.cpp:2114:18: error: Buffer is accessed out of bounds: clfd [bufferAccessOutOfBounds]
src/loop_functions.cpp:1598:78: error: Uninitialized variable: nodetype [uninitvar]

warnings (In-Scope, dedupliziert nach Typ):
  32 [uninitMemberVar]      Display-Bounds-/Setup-Klassen ohne Member-Init im Konstruktor
   7 [identicalInnerCondition]  redundante if(bLORADEBUG)-Verschachtelung (toter Code)
   6 [dangerousTypeCast]    alte C-Casts in getTextBounds
   1 [sizeofwithnumericparameter]
```

> Die 32× `uninitMemberVar` betreffen Display-Klassen-Konstruktoren — meist harmlos (Werte werden vor Nutzung gesetzt), als 🔵-Hygiene notiert. Die 7× `identicalInnerCondition` in `lora_functions.cpp` sind redundante, aber harmlose `if(bLORADEBUG)`-Verschachtelungen. Die zwei `error`-Kandidaten (`loop_functions.cpp:2114` / `:1598`) sind echte Bugs — `:2114` ist als Finding **MEM/blau** gelistet.

---

## Anhang B — Als sauber bewertet / geprüfte Hot-Paths

Positivbefund: In folgenden dauerhaft laufenden Pfaden fanden die Auditoren **keine** verwertbaren Schwachstellen über die oben gelisteten hinaus:

- **LoRa-RX-Interrupt / DIO1-Callback** — keine unsicheren Shared-State-Zugriffe ohne Guard.
- **Deepsleep-Wakeup (Preview/E213)** — `esp_sleep_enable_ext1_wakeup(GPIO0)` + `prepareToSleep()` korrekt (K1/K7 aus 06-12 halten).
- **Batterie-Glitch-Filter** — Ausreißerbereinigung (±2%) plausibel, keine Div-0 im regulären Pfad.
- **Panel-Auto-Erkennung (BUSY-Polarität)** — funktional korrekt; einziger Kritikpunkt ist der fehlende Fallback (Finding DISP/gelb).
- **Command-Parser (FSM, Preview)** — die 06-12 gehärtete Serial/Telnet-Eingabe ist binärsicher (kein Regress gefunden).
- **E213-Batterie-Kalibrierung** — ADC_CTRL-Polarität (active HIGH) korrekt vom WP (active LOW) getrennt.

---

## Anhang C — Bezug zum 06-12-Audit (Display-Freeze)

Die **Power/Deepsleep**-Findings dieses Laufs sind teils Wiedervorlagen der K-Ketten von 2026-06-12, geprüft gegen den **aktuellen** Stand:

| Heutiges Finding | Bezug 06-12 | Neu erkannt |
|---|---|---|
| 🟠 Low-Voltage-Flash-Write (`--display off`) | K2/K5 | **im Default-Build noch aktiv** (nur Preview gefixt) |
| 🟠 `--deepsleep` ohne Wakeup + ohne `prepareToSleep` | K1/K7 | **im Default-Build noch aktiv** (nur Preview/E213 gefixt) |
| 🟠 `is_receiving`-Hänger blockiert `read_batt()` | K8 | bestätigt — weiterhin offen (geteilter LoRa-Pfad) |

**Kernaussage:** Die Fixes greifen im **Preview**-Build; die **committfähigen Default-Builds** (`env:wireless-paper`, `env:vision-master-e213`) tragen K1/K2/K5 weiterhin. Das ist die wichtigste Handlungsempfehlung dieses Audits.

---

## Anhang D — Bereits adressiert (Stand dieses Working Trees)

- 🔵 **E213 `landscape()` kennt `Vision_Master_E213` nicht** (Finding E213/blau, latenter 180°-Flip): durch das heute eingebaute **`--rotate`-Feature behoben** — `landscape()` hat jetzt einen eigenen `Vision_Master_E213`-Zweig statt des `else`-Pfads.
- 🔵 **`snprintf(clfd, sizeof(cset), …)`** (Finding MEM/blau, `loop_functions.cpp:2114`): **noch offen** — falsches Größen-Argument, sollte auf `sizeof(clfd)` korrigiert werden (kleiner Einzelfix).

---

## Farbschema

Wie gewünscht in vier Stufen — **🔴 rot** (Critical) · **🟠 orange** (High) · **🟡 gelb** (Medium) · **🔵 blau** (Low/Hinweis). Blau ersetzt hier das grüne „Low" der 06-12-Skala, damit es sich klar vom grünen „sauber"-Positivbefund (Anhang B) unterscheidet.

---

*Read-only-Audit — es wurde **kein Code verändert.** Alle Fix-Vorschläge sind beschreibend (nicht angewandt). Erzeugt durch einen adversarisch verifizierten Multi-Agenten-Lauf (36 Agenten, 0 Fehler, ~1,54 Mio. Analyse-Tokens).*
