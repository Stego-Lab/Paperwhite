## Problem

Bei **aktivierter Netconsole** (`bNETCONSOLE`) **ohne stehende WiFi-Verbindung** stürzt das Modul ab (Panic + Reboot). Backtrace (gekürzt):

```
__assert_func               (lwip)  assert.c:85
tcpip_send_msg_wait_sem             tcpip.c:455
netconn_new_with_proto_and_callback api_lib.c:166
lwip_socket                         sockets.c:1774
loopNetConsole()                    net_console.cpp:311   <-- ::socket(...)
esp32loop()                         esp32_main.cpp
```

## Ursache

`loopNetConsole()` öffnet den Listening-Socket (`::socket(AF_INET, SOCK_STREAM, 0)`) **bedingungslos**. Der `socket()`-Aufruf spricht den lwIP-TCP/IP-Stack an — ist dieser noch **nicht initialisiert/bereit** (kein verbundenes WiFi), feuert der Assert `tcpip_send_msg_wait_sem` → `panic_abort` → Reboot.

Der einzige Aufruf-Guard in `esp32loop()` ist `iWlanWait == 0`. Das ist **kein** „Netz ist bereit"-Signal: `iWlanWait` wird auch im *„SET but no Wifi connect"*-Fall auf `0` gesetzt. Seit *„use netconsole without gateway or webserver on"* kann die Konsole zudem **standalone** laufen — also auch dann, wenn vorher kein Netz (Gateway/Webserver) aufgebaut wurde.

## Fix

In `loopNetConsole()` **früh aussteigen, solange WiFi nicht verbunden ist**:

```c
void loopNetConsole()
{
    if (WiFi.status() != WL_CONNECTED)
        return;
    ...
}
```

- `s_server_pending` bleibt erhalten → der Listening-Socket wird geöffnet, **sobald die WiFi-Verbindung steht**.
- Eine TCP/IP-Konsole kann **ohne IP ohnehin nicht arbeiten** → das ist die korrekte Vorbedingung, kein Workaround.
- **Nicht board-spezifisch** — betrifft alle ESP32-Boards mit Netconsole.

## Test

- Build grün: `wireless-paper` (+ `vision-master-e290` Gegenprobe).
- Verhalten: `--netconsole on` ohne WiFi → **kein** Crash mehr; Socket öffnet erst nach WiFi-Verbindung.

🤖 Generated with [Claude Code](https://claude.com/claude-code)
