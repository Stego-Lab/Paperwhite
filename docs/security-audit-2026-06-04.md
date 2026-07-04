# MeshCom/Paperwhite Security Audit

Datum: 2026-06-04
Scope: `Paperwhite/` Firmware, Tools, PlatformIO-Konfiguration, GitHub Actions
Ziel: Findings-Report mit priorisierten Risiken und konkreten Fix-Empfehlungen

## Kurzfazit

Der Audit bestaetigt mehrere sicherheitsrelevante Risiken in den exponierten
Firmware-Flaechen: Safeboot-OTA ist ohne Authentifizierung erreichbar, die
HMAC-Konsole akzeptiert neben HMAC auch das Klartext-Passwort und loggt dieses,
BLE-Settings koennen ohne Link-Security gelesen und geschrieben werden, und
mehrere Laengenfelder werden vor Kopien in feste Buffer nicht ausreichend
validiert. Zusaetzlich ist die Release-Pipeline supply-chain-anfaellig, weil
Code und Boarddefinitionen zur Buildzeit ungepinnt aus dem Internet geladen
werden.

## Methodik

- Repository-Inventur: PlatformIO, ESP32/nRF52, Safeboot/OTA, Web-UI, HMAC
  Console, BLE, UDP/LoRa/APRS, Persistenz und GitHub Actions.
- Manuelle Hotspot-Pruefung mit `rg`, `nl`, gezielten Code-Reads und Abgleich
  mit vorhandenen Review-Notizen.
- Secret-Heuristik mit `rg` auf typische Token/Key/Password-Muster. Es wurden
  keine echten Secrets gefunden; Treffer waren Beispiele oder Passwort-Flows.
- Statische Analyse: `cppcheck --enable=warning,style,performance,portability`
  auf `Paperwhite/src`. Die Ausgabe bestaetigt u.a. uninitialisierte Member,
  unsigned-Vergleiche und verdachtige C-API-Nutzung, aber wurde wegen fehlender
  Build-Makros nicht als alleinige Quelle fuer kritische Findings verwendet.

Nicht ausgefuehrt: PlatformIO-Builds und Hardware-/OTA-Laufzeittests. Lokal war
`cppcheck` verfuegbar; `pio`, `gitleaks` und `trufflehog` waren nicht im PATH.

## Findings

### SEC-01 Kritisch: Safeboot-OTA ist ohne Authentifizierung offen

Belege:
- `Paperwhite/src/safeboot/main.cpp:63-69` startet bei fehlender SSID oder
  aktivem AP-Modus einen offenen AP mit `WiFi.softAP(hostname)`.
- `Paperwhite/src/safeboot/main.cpp:283-286` ruft `ElegantOTA.clearAuth()` und
  danach `ElegantOTA.begin(&webServer)` auf.
- `Paperwhite/src/safeboot/ElegantOTA.cpp:20-23`, `45-48`, `200-228` schuetzen
  Update-Routen nur, wenn `_authenticate` gesetzt ist.

Risiko: Jeder Client im gleichen WLAN oder im Fallback-AP kann `/update`,
`/ota/start` und `/ota/upload` erreichen und Firmware flashen. Das ist eine
Remote-Code-Execution auf dem Geraet, sobald Safeboot aktiv ist.

Reproduktion:
- Safeboot starten, AP `MeshCom-OTA` bzw. Callsign-Hostname verbinden.
- `http://192.168.4.1/update` oder `http://<hostname>.local/update` aufrufen.
- Upload ohne Basic-Auth versuchen.

Empfehlung:
- `clearAuth()` entfernen und Safeboot-OTA immer mit Passwort betreiben.
- OTA-Passwort aus `node_webpwd` oder einem eigenen OTA-Secret ableiten; leere
  Werte muessen OTA deaktivieren oder eine lokale Bestaetigung erfordern.
- Fallback-AP mit WPA2-Passphrase starten, nicht offen.
- Firmware-Integritaet mit Signatur oder mindestens pinned Hash/Version pruefen.

Tests:
- Ohne Passwort darf `/update` nicht erreichbar sein.
- Falsches Passwort muss `401` liefern.
- Korrektes Passwort erlaubt Upload.
- Offener AP darf nicht entstehen, wenn kein OTA-Passwort gesetzt ist.

### SEC-02 Kritisch: HMAC-Konsole erlaubt Klartext-Passwort und loggt Secrets

Belege:
- `Paperwhite/src/net_console.cpp:171` loggt `s_password` und `respBuf`.
- `Paperwhite/src/net_console.cpp:173-196` akzeptiert `respBuf == s_password`
  als erfolgreiche Authentifizierung, bevor HMAC geprueft wird.
- `Paperwhite/src/net_console.cpp:135-138` gibt bei leerem Passwort sofort frei.
- `Paperwhite/src/net_console.cpp:269-275` begrenzt Passwort auf 14 Zeichen.

Risiko: Das Challenge-Response-Design wird umgangen, weil ein Client das
Passwort direkt im Klartext senden kann. Gleichzeitig erscheinen Passwort und
Client-Antwort im seriellen Log. Wer Logs, Serial-Konsole oder einen
Netzwerkmitschnitt sieht, kann die Konsole uebernehmen.

Reproduktion:
- `--passwd test123` setzen und NetConsole starten.
- TCP-Verbindung auf Port 2323 oeffnen.
- Nach `NONCE:` direkt `test123\r\n` senden.
- Erwartet im aktuellen Code: `OK`.

Empfehlung:
- Klartext-Fallback `memcmp(respBuf, s_password, strlen(s_password))` entfernen.
- Niemals Passwort oder HMAC-Antwort loggen.
- Leeres Passwort fuer NetConsole nur im Debug-Build erlauben oder Service nicht
  starten.
- Passwortlaenge auf mindestens 12 Zeichen erzwingen; 14-Zeichen-Maximum
  vermeiden oder dokumentiert begruenden.

Tests:
- Klartext-Passwort muss `FAIL` liefern.
- Korrektes HMAC bleibt erfolgreich.
- Logs duerfen keine Secrets enthalten.
- Leeres Passwort startet keine offene Konsole in Release-Builds.

### SEC-03 Hoch: BLE-Settings sind ohne Verschluesselung les- und schreibbar

Belege:
- ESP32: `Paperwhite/src/esp32/esp32_main.cpp:1531-1533` setzt BLE Security Auth
  auf `false, false, false`.
- ESP32: `Paperwhite/src/esp32/esp32_main.cpp:1553-1573` erlaubt `READ`,
  `WRITE` und `NOTIFY`; `READ_ENC`, `WRITE_ENC` und Auth-Properties sind
  auskommentiert.
- nRF52: `Paperwhite/src/nrf52/nrf52_ble.cpp:273-275` setzt Settings-Characteristic
  auf `READ | WRITE` mit `SECMODE_OPEN`.
- nRF52: `Paperwhite/src/nrf52/nrf52_ble.cpp:305-322` uebernimmt ein ganzes
  `s_meshcom_settings`-Objekt und speichert es, wenn Marker und Laenge passen.

Risiko: Ein nahegelegener BLE-Client kann Einstellungen auslesen oder schreiben,
inklusive Callsign, WLAN-Daten, Gateway-/Server-Parametern und Passwoertern,
sofern BLE aktiv ist. Das erlaubt Konfigurationsdiebstahl, Umkonfiguration und
potenziell Missbrauch des Mesh/Gateway-Verhaltens.

Empfehlung:
- ESP32: Pairing/Bonding und verschluesselte Characteristics aktivieren
  (`READ_ENC`, `WRITE_ENC` bzw. Authenticated Write).
- nRF52: `SECMODE_OPEN` durch verschluesselte oder MITM-geschuetzte Permissions
  ersetzen.
- Settings-Write nur nach explizitem Unlock-Zeitfenster am Geraet erlauben.
- Sensitive Felder bei Read maskieren, wenn die App sie nicht zwingend braucht.

Tests:
- Ungepairter BLE-Client darf Settings weder lesen noch schreiben.
- Gepaierter Client kann nur im erlaubten Unlock-Zeitfenster schreiben.
- Ungueltige Settings werden abgelehnt und nicht persistiert.

### SEC-04 Hoch: nRF52 UDP-CONF Parsing vertraut Laengenfeldern

Belege:
- `Paperwhite/src/nrf52/nrf_eth.cpp:501-510` kopiert ein UDP-CONF-Paket in
  `config_buf`, fuellt danach aber mit `packetSize - UDP_MSG_INDICATOR_LEN + i`
  ueber das Ende hinaus, sobald `packetSize > UDP_MSG_INDICATOR_LEN`.
- `Paperwhite/src/nrf52/nrf_eth.cpp:519-534` verwendet `call_len` und
  `short_len` aus dem Paket fuer Variable-Length-Arrays und `memcpy`, ohne zu
  pruefen, ob die Laengen innerhalb `config_buf` liegen.

Risiko: Ein manipulierter Server oder UDP-Paket kann Stack/Buffer-Korruption
oder Crash ausloesen. Da CONF Einstellungen setzt, ist der Pfad besonders
sensibel.

Reproduktion:
- CONF-Paket mit `call_len` groesser als verbleibender Payload-Laenge senden.
- CONF-Paket mit `packetSize == UDP_CONF_BUFF_SIZE` senden und den Fill-Loop
  beobachten.

Empfehlung:
- Fill-Loop korrigieren: nur von `copied_len` bis `UDP_CONF_BUFF_SIZE - 1`
  schreiben.
- Vor jedem Zugriff `offset + len <= copied_len` pruefen.
- Feste Maximalwerte fuer Callsign und Shortname nutzen, keine VLA auf Stack.
- Malformed CONF-Pakete verwerfen und rate-limited loggen.

Tests:
- Zu kurze, zu lange und inkonsistente CONF-Pakete duerfen nicht crashen.
- Gueltige CONF-Pakete setzen Callsign/Shortname unveraendert.

### SEC-05 Hoch: BLE-Ausgabe kopiert bis 255 Bytes in 260-Byte-Slots plus Trailer

Belege:
- `Paperwhite/src/configuration_global.h:53` definiert `UDP_TX_BUF_SIZE` als 255.
- `Paperwhite/src/loop_functions.cpp:387` definiert `BLEtoPhoneBuff` als
  `MAX_MSG_LEN_PHONE + 5`; `MAX_MSG_LEN_PHONE` ist laut Nutzung 255.
- `Paperwhite/src/loop_functions.cpp:504-505` kappt nur `len > UDP_TX_BUF_SIZE`
  auf `UDP_TX_BUF_SIZE - 4`; bei `len == 255` bleibt die Laenge 255.
- `Paperwhite/src/loop_functions.cpp:508-523` kopiert `len` Bytes ab Offset 1
  und danach 4 Zeitbytes ab `len + 1`.

Risiko: Bei `len == 255` wird bis Index 259 geschrieben, was bei einem
260-Byte-Slot gerade noch passt; das Laengenbyte wird aber auf `len + 4 == 259`
gesetzt und passt nicht in ein Byte. Empfaenger sehen eine falsche Laenge, und
kleine Aenderungen an Slotgroesse/Trailer koennen sofort zu Overflows fuehren.

Empfehlung:
- Bedingung auf `if (len > MAX_MSG_LEN_PHONE - 4)` aendern, nicht auf
  `UDP_TX_BUF_SIZE`.
- Laengenfeld als `uint16_t` behandeln oder hart auf 251 Bytes begrenzen.
- Einheitliche Helper-Funktion fuer BLE-Slot-Kapazitaet einfuehren.

Tests:
- `len` 0, 1, 251, 252, 255 pruefen.
- Empfaenger muss korrekte Laenge und Trailer erhalten.

### SEC-06 Hoch: Mehrere RingBuffer-Kopien verlassen sich auf ungepruefte Encoder-Laengen

Belege:
- `Paperwhite/src/loop_functions.cpp:3192-3193`, `3702-3704`, `3780-3782`,
  `3856-3858`, `3931-3933`, `4198-4200`, `4215-4217` kopieren `aprsmsg.msg_len`,
  `ilng` oder `tlng` direkt nach `ringBuffer[iWrite] + 2`.
- `ringBuffer` hat `UDP_TX_BUF_SIZE + 5` Bytes; Nutzdaten sollten maximal
  `UDP_TX_BUF_SIZE` sein.

Risiko: Die meisten Encoder scheinen intern zu kappen, aber der sichere Vertrag
ist nicht lokal an den Kopierstellen erzwungen. Ein zukuenftiger Encoder-Bug,
ein negativer/ueberlaufender Rueckgabewert oder ein neuer Pfad kann direkt
Speicherkorruption im TX-Ring verursachen.

Empfehlung:
- Vor jeder RingBuffer-Kopie eine gemeinsame Funktion verwenden, z. B.
  `enqueueTxFrame(source, buffer, len, status)`, die `len <= UDP_TX_BUF_SIZE`
  erzwingt.
- Bei zu langen Frames verwerfen und zaehlen, nicht still kuerzen, wenn FCS oder
  Protokollkonsistenz betroffen ist.

Tests:
- Unit-/Host-Test fuer `len == UDP_TX_BUF_SIZE`, `len == UDP_TX_BUF_SIZE + 1`
  und `len == 0`.
- Fuzz-Test fuer APRS-Encoding/Enqueue-Kombination.

### SEC-07 Mittel: Web-UI uebertraegt Passwort im Query-String und nutzt IP-basierte Session

Belege:
- `Paperwhite/src/web_functions/web_functions.cpp:364-377` extrahiert
  `/?nodepassword=` aus der HTTP-Request-Line.
- `Paperwhite/src/web_functions/web_functions.cpp:235-285` merkt erfolgreiche
  Authentifizierung nur ueber Client-IP und 4-Stunden-Timer.
- `Paperwhite/src/web_functions/web_functions.cpp:405-428` erlaubt danach
  Funktionsaufrufe und Parameterzugriffe.

Risiko: Passwoerter in URLs landen in Browser-History, Logs, Proxies und
Debug-Ausgaben. IP-basierte Sessions sind in NAT-/Shared-WLAN-Szenarien
uebertragbar: Ein anderer Client mit gleicher IP-Sicht kann vom Login profitieren.

Empfehlung:
- Passwort per POST-Body senden, nicht im Query-String.
- Session-Token mit Zufallswert und Ablaufzeit setzen; Token an Cookie/Header
  binden statt nur an IP.
- Mutierende Aktionen mit CSRF-Token oder mindestens Same-Origin-Token absichern.

Tests:
- Passwort darf nicht in Request-Line oder Serial-Debug erscheinen.
- Zwei Clients hinter gleicher IP duerfen keine Session teilen.
- Logout invalidiert nur die eigene Session.

### SEC-08 Mittel: CI/Release-Pipeline laedt ungepinnten Code mit Schreibrechten

Belege:
- `.github/workflows/meshcom-ci.yml:13-14` gibt dem Job `contents: write`.
- `.github/workflows/meshcom-ci.yml:28-35` installiert aktuelle Versionen von
  `platformio`, `intelhex` und Plattformen ohne Versionspin.
- `.github/workflows/meshcom-ci.yml:52-65` laedt Boarddefinition und
  `uf2conv.py`/`uf2families.json` per `wget` aus Branch-/Ref-URLs.
- `.github/workflows/meshcom-ci.yml:96-137` veroeffentlicht danach Release-
  Artefakte mit demselben Job.

Risiko: Ein kompromittierter Upstream, Branch-Change oder Supply-Chain-Angriff
auf Build-Dependencies kann Firmware-Artefakte erzeugen und als Release
veroeffentlichen. Da Firmware auf Geraeten geflasht wird, ist das ein direkter
Vertrauenspfad.

Empfehlung:
- Dependencies und Downloads auf Version/Commit-SHA pinnen und Hashes pruefen.
- Build und Release trennen; `contents: write` nur im Release-Job.
- GitHub Actions auf SHA pinnen oder mindestens Dependabot/Renovate fuer Actions
  nutzen.
- Artefakte mit Checksums und Signatur publizieren.

Tests:
- CI muss fehlschlagen, wenn ein heruntergeladener Hash nicht passt.
- Build-Job darf ohne `contents: write` laufen.

### SEC-09 Mittel: PlatformIO-Abhaengigkeiten sind teils ungepinnt oder schwach gepinnt

Belege:
- `Paperwhite/platformio.ini:61-63`, `72`, `80` referenzieren GitHub-URLs ohne
  Commit-SHA/Tag.
- `Paperwhite/platformio.ini:62-76`, `81-83`, `126`, `129` verwenden Caret-
  Ranges wie `^1.1.0` oder `^6.13.0`.
- Safeboot nutzt eine konkrete Tasmota-Zip-URL, aber ohne dokumentierte
  Hash-Pruefung (`Paperwhite/platformio.ini:164`, `202`).

Risiko: Builds sind nicht voll reproduzierbar. Upstream-Updates koennen
unerwartet Verhalten aendern oder kompromittierten Code einbringen.

Empfehlung:
- Alle externen Libraries auf exakte Version oder Commit-SHA pinnen.
- Fuer URL-/Zip-Dependencies Hashes dokumentieren und im CI pruefen.
- SBOM oder Dependency-Liste pro Release erzeugen.

Tests:
- Rebuild eines Tags muss dieselben Dependency-Versionen verwenden.
- Dependency-Update nur ueber expliziten PR mit Changelog/Review.

### SEC-10 Mittel: UI-Helfer haben uninitialisierte/ungepruefte Indizes

Belege:
- `Paperwhite/src/t5-epaper/ui.cpp:67-81`: `j` und `len2` werden vor Verwendung
  nicht initialisiert; `remaining = buf_size - j - 1` ist undefiniert.
- `Paperwhite/src/t-deck-pro/ui_deckpro.cpp:72-91`: `line_full_format` kopiert
  `str1`/`str2` ohne Kapazitaetspruefung in `global_buf`.
- `Paperwhite/src/t-deck-pro/ui_deckpro.cpp:1863-1865` und `2518-2519` nutzen
  `sprintf(txt, "%c", keypay_v)` in `char txt[2]`; fuer das aktuelle Format
  passt es, aber es ist ein fragiles Muster.

Risiko: Undefiniertes Verhalten und moegliche Buffer-Korruption in UI-Pfaden.
Das ist primaer Robustheit, kann aber ueber BLE/Web/Keyboard-Eingaben getriggert
werden, wenn lange Strings in die UI gelangen.

Empfehlung:
- `line_full_format` in beiden Dateien mit `snprintf`/bounded append neu
  implementieren.
- `j = len1`, `len2 = strlen(str2)` initialisieren und alle Laengen auf
  `sizeof(global_buf) - 1` begrenzen.
- `txt` per `char txt[2] = {(char)keypay_v, '\0'};` setzen.

Tests:
- Sehr lange `str1`/`str2`, leere Strings und `max_c <= 1` testen.
- UI darf nicht ueber `global_buf` hinausschreiben.

### SEC-11 Niedrig: Debug-/Info-Ausgaben koennen sensitive Konfiguration leaken

Belege:
- `Paperwhite/src/command_functions.cpp:4644` druckt `node_passwd` im
  Settings-Kontext.
- `Paperwhite/src/command_functions.cpp:4706` druckt `node_webpwd`.
- `Paperwhite/src/safeboot/main.cpp:48-53` druckt SSID und Betriebsmodus im
  Safeboot.

Risiko: Wer Serial-Logs oder Support-Logs erhaelt, kann Passwoerter oder
Netzwerkdetails sehen. Das verstaerkt SEC-02 und SEC-07.

Empfehlung:
- Secrets in allen Ausgaben maskieren (`SET`/`EMPTY`, nie der Wert).
- Debug-Logging fuer sensitive Daten per Compile-Time-Flag verbieten.
- Support-Log-Export mit Redaction versehen.

Tests:
- `--info` und Debug-Logs duerfen keine Passwoerter enthalten.
- Secret-Regex auf Log-Ausgaben in Host-Tests anwenden.

## Bereits sichtbar verbessert

- `command_functions.cpp:140-142` prueft die Groesse vor der Kopie in
  `msg_detail`; das aeltere BOF-Finding aus `docs/code-review.md` ist so nicht
  mehr direkt reproduzierbar.
- `aprs_functions.cpp` nutzt in mehreren Parser-Abschnitten feste Schleifenlimits
  und lokale 255-Byte-Puffer. Das reduziert, ersetzt aber keine zentrale
  Frame-Laengenvalidierung.

## Empfohlene Reihenfolge

1. SEC-01 und SEC-02 vor dem naechsten Release beheben.
2. SEC-03 und SEC-04 als naechste High-Risk-Fixes umsetzen.
3. Gemeinsame sichere Enqueue-/Copy-Helper fuer SEC-05/SEC-06 einfuehren.
4. CI/Dependency-Pinning aus SEC-08/SEC-09 vor offiziellen Release-Artefakten
   haerten.
5. Web-Session, UI-Robustheit und Log-Redaction nachziehen.

## Abnahmekriterien fuer einen gehaerteten Stand

- Kein OTA- oder NetConsole-Zugang ohne Authentifizierung in Release-Builds.
- Keine Klartext-Passwoerter in Protokollen, URLs oder Logs.
- BLE Settings nur ueber verschluesselte/gepairte Verbindung und optionales
  Unlock-Zeitfenster.
- Alle externen Paketlaengen werden vor `memcpy`, VLA oder RingBuffer-Enqueue
  lokal validiert.
- CI nutzt gepinnte Dependencies, getrennte Release-Rechte und Hash-Pruefungen.
