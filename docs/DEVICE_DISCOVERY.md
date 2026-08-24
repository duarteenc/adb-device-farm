# Device discovery

Every start of the farm discovers devices automatically; the operator never
types IPs at launch. Defaults (Settings › Device Discovery):

| Setting | Default |
| --- | --- |
| Automatic discovery | ON |
| Subnet | `192.168.100.0/24` → usable hosts `.1 – .254` (`.0`/`.255` are never probed) |
| ADB port | 5555 |
| Quick refresh (`adb devices -l`) | every 4 s |
| Full subnet rescan | every 45 s, adaptive (×2, ×4 while nothing changes) |
| Probe concurrency / timeout | 64 sockets / 800 ms |
| Auto connect / auto mirror | ON / ON |
| mDNS / ARP | ON / ON |

## Methods (all combined, none blocking the GUI)

| | Method | Implementation |
| --- | --- | --- |
| A | `adb devices -l` | `AdbExecutor::devices` every quick-refresh tick; parsed into `AdbDeviceInfo` (serial, state, product, model, transport) |
| B | `adb mdns services` | same tick; `_adb._tcp` advertisements are connected directly, `_adb-tls-connect` (pairing required) are listed only. Disabled automatically when the host adb lacks the command |
| C | Known-device registry | previously seen `ip:port` endpoints are probed **first** (priority hosts) |
| D | LAN probe | `NetworkScanner` (own thread) opens up to 64 `QTcpSocket`s to `subnet:port`; a full /24 takes ~2–4 s. Hosts that answer become `Discovered` and get a bounded `adb connect` (Settings › ADB › max simultaneous connect, 6 s timeout) |
| E | ARP neighbours | `arp -a` on the network lane; entries are probed before the rest of the range |
| F | Last-known IPs | folded into C |

The PC's own addresses are excluded from the probe. Hosts that answer on the
port but are `unauthorized` are retried at most once every 2 minutes and shown
with the ⚠ badge ("approve USB debugging on the phone").

## State transitions driven by discovery

- host answers on 5555 → `Discovered`
- `adb connect` ok → `Connecting (verifying)` → next `adb devices` → `AdbOnline` / `Unauthorized`
- device drops out of `adb devices` → `Offline` (+ `deviceDisappeared` → `DeviceService` reconnect loop when the device was mirroring/wanted)
- a known TCP device that did not answer the full sweep → `Offline`

## Manual actions

- Devices page › *Scan LAN* (Ctrl+Shift+F5) — immediate full sweep
- Devices page › *WiFi connect* — range (`192.168.100.10-40`), CIDR or single host, custom port
- *USB → WiFi ADB (selected)* — `adb tcpip 5555`, reads the `wlan0` IPv4 (`ip -o -4 addr`, `ifconfig` fallback), connects, and links the USB/WiFi identities (name, group, hardware serial carry over)
- `QtScrcpy.exe --scan` — headless sweep that prints the resulting device table

## Reconnect

`DeviceService` schedules retries with exponential backoff **1, 2, 5, 10, 30,
60 s** (capped at 60 s, unlimited attempts by default — Settings › Mirroring).
For TCP devices each attempt does `adb connect`, a 5 s `echo` probe, then
restarts the mirror; USB devices are probed and restarted when adb re-enumerates
them. Keep-awake policy is re-applied on every return to an online state.
