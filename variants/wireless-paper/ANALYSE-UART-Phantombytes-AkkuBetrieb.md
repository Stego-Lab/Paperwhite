# Wireless Paper – Phantombyte‑Flut in `checkSerialCommand` bei Akku‑Betrieb

**Analyse / Hypothese · Stand 2026‑06‑07 · OE3WAS‑66 (Christian) für Kurt (OE1KBC) & Wolfgang**

> ⚠️ **Wichtiger Hinweis:** Diese Analyse beruht auf **Code‑Lektüre + Auswertung des Netconsole‑Logs**.
> Ich (Christian) habe das **noch nicht selbst am Board gegengeprüft / mit Messgerät verifiziert**.
> Bitte als gut begründete Hypothese verstehen, nicht als bewiesene Tatsache – die Board‑Tests am
> Ende stehen noch aus.

---

## 1. Symptom (Beobachtung Wolfgang)

- **Nur bei Akku‑Versorgung** (USB ab), **Telnet‑Client sendet nichts**, **nichts an USB angesteckt**.
- In der Netconsole flutet es mit Zeilen wie:
  ```
  ?MSG:80..not sent
  SG:FF..not sent
  ?MSG:FE..not sent
  ?MSG:F8..not sent
  ```
- Lokalisiert in `esp32_main.cpp` → `checkSerialCommand()`.
- Mit USB‑Versorgung (Dauertest zuvor) trat das **nicht** auf.
- Der **Netconsole‑Crashfix (PR #983) behebt das nicht** – es ist ein **anderer** Fehler.

---

## 2. Ursache (Hypothese): floatende UART0‑RX‑Leitung (GPIO44)

Die Wireless Paper hat **kein natives USB**, sondern einen **CP2102‑USB‑UART** auf **UART0**
(GPIO43 = U0TXD / **GPIO44 = U0RXD**). Bewusst so gebaut:

```ini
; variants/wireless-paper/platformio.ini
; ARDUINO_USB_CDC_ON_BOOT bewusst NICHT gesetzt: die Wireless Paper haengt ueber
; einen CP2102-USB-UART (kein natives USB) am Rechner. Serial muss daher auf UART0 laufen.
```

Damit ist `Serial` = **UART0**, und `checkSerialCommand()` liest genau dieses UART0
(`net_console.cpp:41` bindet `s_hwSerial = Serial` **vor** dem `#define Serial MSerial`).

**Der CP2102 wird aus der USB‑5 V versorgt:**

| Versorgung | CP2102 | GPIO44 (U0RXD) | Ergebnis |
|---|---|---|---|
| **USB angesteckt** | bestromt | wird sauber auf **Idle‑HIGH** getrieben | ruhig, kein Empfang |
| **Nur Akku** | **stromlos / hochohmig** | **floatet** (kein Pull‑Up im Code) | UART0 liest **Rauschen** → Phantombytes |

Das erklärt **alle vier** Beobachtungen: nur bei Akku, Telnet sendet nichts, USB ab, in
`checkSerialCommand`.

---

## 3. Beweisführung

### a) Der Telnet‑Pfad scheidet als Quelle aus

`MSG:FF..not sent` erscheint **sehr häufig**. Ein `0xFF` aus der Netconsole würde aber als
**Telnet‑IAC** verschluckt (`esp32_main.cpp`, IAC‑Behandlung mit 2 Folgebytes) und **nie** als
`MSG:FF` geloggt. Der **UART0‑Pfad hat keine IAC‑Filterung** → die `FF`‑Flut **muss** von UART0
kommen. (Und Telnet sendet ohnehin nichts → `recv` = `EAGAIN`.)

### b) Byte‑Histogramm aus dem realen Log (690 Phantombytes)

| Byte | Anzahl | Anteil |
|---|---:|---:|
| `FF` | 300 | 43,5 % |
| `FE` | 147 | 21,3 % |
| `FC` | 43 | 6,2 % |
| `80` | 38 | 5,5 % |
| `F8` / `F0` / `E0` / `C0` | 22 / 20 / 18 / 9 | ~10 % |

**86,5 %** aller Bytes liegen in der „Glitch‑Familie" `FF FE FC F8 F0 E0 C0 80` – die
**Lehrbuch‑Signatur einer Idle‑High floatenden 8N1‑Leitung** (kurzes falsches Start‑Bit →
schneller Rücksprung auf HIGH → 1‑Bits LSB‑first in die oberen Positionen). Echter ASCII‑Input
läge bei `0x20–0x7E` mit MSB = 0 – also genau umgekehrt.

---

## 4. Abgleich mit `dev d9f7645` (Kurt: „netconsole with no usb connection")

Kurt arbeitet bereits **am selben Code** – das bestätigt die Richtung. **Aber der Fix greift auf
der Wireless Paper nicht:**

- **`if(!Serial) return;`** am Anfang von `checkSerialCommand`:
  Auf UART0 ist `Serial` (HardwareSerial) **nach `begin()` immer „truthy"** (= Treiber
  initialisiert, **nicht** „USB‑Host vorhanden"). Der Guard wirkt nur auf native‑USB‑CDC‑Boards
  (z. B. T‑Deck‑Pro). → **WP liest weiterhin den Floating‑Müll.** Kein Pull‑Up auf GPIO44, kein
  ASCII‑Filter.

---

## 5. Drei eigenständige Code‑Punkte (in `d9f7645` noch offen)

> Diese Pfade sind **unabhängig von der Floating‑Quelle** gefährlich – jeder Müll, der in
> `checkSerialCommand` ankommt, kann sie auslösen.

- **P1 – fehlender Längen‑Check (durch den Umbau neu kritisch):**
  `strText` wurde von Arduino‑`String` auf **`char strText[600]` + `iTxtPos++`** umgestellt –
  **ohne Bounds‑Check auf `iTxtPos`**. Beginnt ein Strom mit `:` / `-` / `{` und kommt kein
  `\n`/`\r`, läuft `iTxtPos` ungeprüft über 600 → **globaler Buffer‑Overflow** (vorher „nur"
  Heap‑Wachstum). → `iTxtPos < sizeof(strText)` einbauen.

- **P4 – `sendMessage` (KRITISCH):**
  `memcpy(msg_text_check, msg_text+ispos, len_check)` schreibt in `char msg_text_check[200]`
  **ohne Cap**, während der Aufrufer `len = inext` bis ~598 liefern kann
  (`loop_functions.cpp` ~2915/2922). `::`‑Zeile > 200 Byte → **Stack‑Smash**. Längen‑Cap auf
  `sizeof(msg_text_check)` nötig.

- **P7 – Format‑String:**
  `printfdeb(msg_detail)` (`command_functions.cpp:172`) reicht Daten **roh als Format‑String**
  durch → `%n` / `%s` im Müll = undefiniertes Verhalten. Besser `printfdeb("%s", msg_detail)`.

---

## 6. Was noch am Board zu verifizieren ist (steht aus!)

1. **A/B‑Test:** identische Konfig, einmal Akku‑only, einmal USB – tritt die Flut nur bei Akku auf?
2. **GPIO44 messen** (Oszi/LA): Akku = undefiniert/Glitches? USB = sauberes HIGH‑Idle?
3. **Brownout ausschließen:** VBAT/3V3 unter LoRa‑TX messen + Reset‑Reason/Backtrace
   (`esp32_exception_decoder` ist aktiv) – ein Akku‑only‑Reset kann **auch** Brownout sein.
4. **Pull‑Up‑Gegenprobe:** interner Pull‑Up auf GPIO44 → verschwindet die Flut? Falls **nein**,
   ist die Quelle aktiv eingekoppelt (z. B. RF vom LoRa‑TX), nicht reines DC‑Floating.
5. **Null‑Code‑Soforttest:** `--debug off` lässt die `MSG:XX`‑Zeilen verstummen (sie hängen an
   `bDEBUG`), **die Bytes werden aber weiter gelesen & verarbeitet** → P1/P4/P7 bleiben aktiv.
   `--debug off` ist also nur Symptom‑Unterdrückung, **kein** Fix.

---

## 7. Vorgeschlagene Fix‑Richtung (Wireless Paper)

| Ebene | Maßnahme | Wirkung |
|---|---|---|
| **Primär** | Input‑Filter in `checkSerialCommand`: Nicht‑ASCII / `>0x7F` verwerfen **+** harter `strText`/`iTxtPos`‑Cap | wirkt **quellenunabhängig**, killt Flut + P1 |
| **Wurzel** | interner **Pull‑Up auf GPIO44** nach `Serial.begin()` (WP‑only) | beseitigt false start bits an der Quelle – **erst nach Board‑Gegenprobe** (Punkt 4) |
| **Härtung** | `sendMessage`‑`memcpy`‑Cap (P4) + `printfdeb("%s", …)` (P7) | eigenständige, board‑übergreifende Robustheit |

---

*Erstellt aus Log‑ + Code‑Analyse (HEAD `d9f7645`). Noch nicht am Board gegengeprüft – Rückmeldung
und gemeinsame Verifikation willkommen. 73!*
