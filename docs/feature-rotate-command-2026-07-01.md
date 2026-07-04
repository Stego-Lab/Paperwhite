# Feature: Terminalkommando `--rotate 0/90/180/270` (E-Ink Display drehen)

**Erstellt:** 2026-07-01 · **Aktualisiert:** 2026-07-03 (Live-Fix, am Gerät verifiziert)
**Boards:** Heltec Wireless Paper (`BOARD_WIRELESS_PAPER`) + Heltec Vision Master E213 (`BOARD_E213`)
**Idee:** Kurt (OE1KBC) — Terminalkommando statt Tastengeste
**Status:** ✅ implementiert · alle Boards bauen · **am Gerät verifiziert** (WP + E213: `--rotate 180` dreht live, board-übergreifend konsistent, überlebt Reboot)

---

## 1. Kurzfassung

Persistentes Terminalkommando, das das E-Ink-Display dreht:

```
--rotate 0      → Werksausrichtung (Standard)
--rotate 180    → auf dem Kopf   (Antenne unten statt oben)
--rotate 90     → Hochformat, 90° im Uhrzeigersinn      *)
--rotate 270    → Hochformat, 270° im Uhrzeigersinn     *)
```

*) **`90`/`270` kippen ins Hochformat.** Das Textlayout (Statuszeile, Nachrichten) ist für **Querformat** ausgelegt — bei 90/270 passt es nicht mehr. Sinnvoll sind praktisch nur **`0`** und **`180`** (Antenne oben/unten). 90/270 bleiben zugelassen (Kurts Vorschlag), sind aber nur für eine physische 90°-Montage gedacht.

- **Persistent:** überlebt Reboot/Stromverlust (NVS).
- **Sofort wirksam:** greift live (Voll-Refresh), nicht erst nach Reboot.
- **Board-übergreifend konsistent:** `--rotate 180` dreht WP und E213 **gleich** (dieselbe relative Wirkung), obwohl die Panels physisch um 180° zueinander verbaut sind.
- **Rückwärtskompatibel:** `--rotate 0` (Default) = Verhalten **exakt wie bisher**.
- Verfügbar über **USB-Serial, BLE (MeshCom-App), Telnet/NetConsole**.

---

## 2. Konzept — additiver Offset auf die Werks-Basisrotation

`--rotate` setzt **keinen absoluten** Panel-Wert, sondern einen **additiven Offset** auf die board-spezifische Werks-Basisrotation:

```
effektive_rotation = (Werks-Basis + node_disp_rot) % 360
```

**Warum additiv?** Die beiden Panels haben unterschiedliche Werks-Basiswerte (um 180° zueinander verbaut). Ein absoluter Wert (`--rotate 90` = `setRotation(90)`) ergäbe auf WP und E213 **verschiedene** physische Ausrichtungen. Der additive Offset macht die **Wirkung** von `--rotate` auf beiden Boards identisch.

### Werks-Basiswerte (effektiv, verifiziert)

| Board | Effektive Basis (Boot **und** Betrieb) | GFX-Rotation |
|---|---|---|
| **Wireless Paper** | 270° | 3 (Querformat) |
| **Vision Master E213** | 90° (Kurt `ef998aa`) | 1 (Querformat) |
| **Vision Master E290** | 270° | 3 (kein `--rotate`) |

> **Wichtig:** Die effektive Rotation kommt an **einer** Stelle her — `applyDisplayRotation()`. Der Aufruf `BaseDisplay::landscape()` im Boot-Startscreen setzt zwar zuerst einen Board-Grundwert, wird aber unmittelbar von `applyDisplayRotation()` überschrieben; im laufenden Betrieb wird `landscape()` gar nicht aufgerufen. Daher ist `applyDisplayRotation()` die **Single Source of Truth**.

### Effekt von `--rotate 180`

| Board | (Basis+180)%360 | GFX-Rotation | Ergebnis |
|---|---|---|---|
| WP | (270+180)%360 = **90°** | 1 | Querformat, um 180° gedreht ✓ |
| E213 | (90+180)%360 = **270°** | 3 | Querformat, um 180° gedreht ✓ |

Beide drehen sich um 180° — board-übergreifend konsistent.

### Warum nur 0/180 sinnvoll sind

Querformat = **ungerade** GFX-Rotationen (1, 3). Ein Offset von 90 oder 270 wechselt die Parität → **gerade** Rotation (0, 2) = **Hochformat**:

| Offset | WP (Basis 270°) | E213 (Basis 90°) | |
|---|---|---|---|
| 0 | 270° → rot 3 | 90° → rot 1 | ✅ Querformat |
| 180 | 90° → rot 1 | 270° → rot 3 | ✅ Querformat |
| 90 | 0° → rot 0 | 180° → rot 2 | ⚠️ Hochformat |
| 270 | 180° → rot 2 | 0° → rot 0 | ⚠️ Hochformat |

---

## 3. Persistenz

- **Struct-Feld:** `s_meshcom_settings::node_disp_rot` (`int`, Default `0`) in `esp32_flash.h`
- **NVS-Key:** `"node_disrot"` (Namespace `"Credentials"`)
- **Laden:** `init_flash()` → `getInt("node_disrot", 0)`
- **Speichern:** `save_settings()` → `putInt("node_disrot", …)` (vom Handler ausgelöst)
- **Boot-Anwendung:** `initDisplay()` spiegelt `node_disp_rot` → globale `g_dispRotOffset`. Reihenfolge garantiert: `init_flash()` läuft **vor** `initDisplay()`.

Der Display-Layer (`layout.cpp`) kennt die Settings-Struktur nicht — Schnittstelle ist die entkoppelte globale `int g_dispRotOffset`.

---

## 4. Architektur — `applyDisplayRotation()` als Single Source

Kern des Designs (nach dem Live-Fix, siehe Abschnitt 6):

```c
// esp32_functions.cpp — EINZIGE Stelle, die die Betriebs-Rotation setzt.
// Wird beim Boot (startDisplay) UND live (--rotate-Handler) aufgerufen.
void applyDisplayRotation() {
    #if defined(BOARD_E213)
    epaper_display.setRotation((90  + g_dispRotOffset) % 360);   // E213-Basis 90°
    #else  // BOARD_WIRELESS_PAPER
    epaper_display.setRotation((270 + g_dispRotOffset) % 360);   // WP-Basis 270°
    #endif
}
```

- **Boot:** `startDisplay()` ruft nach `landscape()` → `applyDisplayRotation()`.
- **Live:** der `--rotate`-Handler ruft `applyDisplayRotation()` **vor** `sendDisplayHead(true)` (Voll-Refresh). Reihenfolge zwingend: erst drehen, dann zeichnen.

Damit nutzen Boot und Live **garantiert dieselbe** Rotationsquelle → keine Divergenz.

---

## 5. Geänderte Stellen (für PR-Review)

Alle Änderungen additiv, `WP_DISP`-gegatet; bei `--rotate 0` ändert sich **kein** Board-Verhalten.

| # | Datei | Änderung |
|---|---|---|
| 1 | `src/esp32/esp32_flash.h` | Feld `int node_disp_rot = 0;` + Doku |
| 2 | `src/esp32/esp32_flash.cpp` | `init_flash()` `getInt` + `save_settings()` `putInt` (`"node_disrot"`) |
| 3 | `src/configuration_global.h` | `extern int g_dispRotOffset;` unter `#if defined(WP_DISP)` |
| 4 | `src/esp32/esp32_functions.cpp` | Def. `g_dispRotOffset` + **`applyDisplayRotation()`** + Boot-Sync in `initDisplay()` + `startDisplay()` ruft `applyDisplayRotation()` |
| 5 | `src/esp32/esp32_functions.h` | Deklaration `void applyDisplayRotation();` |
| 6 | `src/command_functions.cpp` | `--rotate`-Handler: validiert, persistiert, ruft `applyDisplayRotation()` + `sendDisplayHead(true)` |
| 7 | `src/command_functions.cpp` | `--help`: Zeile `--rotate 0/90/180/270 …` (WP_DISP-gegatet) |
| 8 | `src/Displays/BaseDisplay/layout.cpp` | **unverändert zum Original** (Werks-Grundausrichtung); die Rotation läuft komplett über `applyDisplayRotation()` |

> **Hinweis:** `layout.cpp` bleibt bewusst auf dem Original-Stand. Ein früherer Ansatz, den Offset dort in `landscape()` einzurechnen, war wirkungslos (landscape läuft im Betrieb nie und wird im Boot überschrieben) — deshalb zentralisiert in `applyDisplayRotation()`.

---

## 6. Warum es zuerst nicht wirkte (Live-Fix, 2026-07-03)

Die erste Fassung persistierte korrekt, drehte aber **live nichts** — nur nach Reboot. Ein Diagnose-Workflow (5 parallele Code-Ermittler) fand die Ursache:

- Der Handler rief nur `sendDisplayHead(true)` — das macht **kein** `setRotation()`/`landscape()`.
- `landscape()` wird im gesamten Betrieb **nie** aufgerufen (nur einmal im Boot-Startscreen).
- Ergebnis: `g_dispRotOffset` wurde gesetzt, aber nie auf das `epaper_display` angewandt → GFX-Rotation blieb auf dem Boot-Wert.

**Fix:** `applyDisplayRotation()` als gemeinsame Funktion, die der Handler **vor** dem Refresh aufruft. Der Kommentar „sendDisplayHead ruft landscape()" (falsch) wurde korrigiert.

---

## 7. Test-Ergebnis (am Gerät)

- ✅ **WP:** `--rotate 180` dreht live, `--rotate 0` zurück
- ✅ **E213:** gleiche relative Wirkung wie WP (board-übergreifende Konsistenz bestätigt)
- ✅ **Persistenz:** überlebt Reset
- ✅ **Ungültiger Wert** (`--rotate 45`): keine Änderung, Debug-Hinweis

---

## 8. Kompatibilität / Risiko

- **`--rotate 0` = identisches Verhalten** zu vorher (`(Basis+0)%360` = Basis).
- **NVS-Migration** unkritisch: fehlender Key → Default `0`.
- **E290 und alle anderen Boards:** unberührt (`WP_DISP`-Gating). Alle 33 Board-Varianten bauen.
- **Kurts `ef998aa`** (E213-Basis 90°) bleibt erhalten.
