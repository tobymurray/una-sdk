# Standalone FTS prototype

`una_ble_client.py` is a phone-free Linux client for the UNA Watch's File Transfer Service,
validated end-to-end against a real watch: lists directories and pulls `.fit` activity files
with correct FIT headers and matching CRC-16 checksums. Full protocol writeup, including how
this was built and what it corrected in the original phone-capture analysis, is in
`../BLE-COMPANION-protocol-spec.md` §6a.

## Setup

```
pip install dbus_fast
```

Requires Linux with BlueZ and a Bluetooth adapter. Pair the watch first via `bluetoothctl`
(interactively, so you can read the passkey off the watch's screen):

```
bluetoothctl
scan on
# wait for "UNA Watch ..." to appear, note its current address
pair <address>
# watch shows a 6-digit passkey; type it when prompted
trust <address>
```

Once paired, BlueZ resolves the watch's stable identity address on future connections
regardless of its rotating advertised address — use that identity address with the script
below (or whatever address `bluetoothctl devices Paired` shows).

## Usage

```
python3 una_ble_client.py <device-address> list /Apps/
python3 una_ble_client.py <device-address> read /Apps/GpsLab/ActivityArchive/202607/activity_YYYYMMDDTHHMMSS.fit out.fit
```

## Known caveats

- The watch's BLE advertising window is short (both for pairing-mode discovery and for
  reconnection after a normal disconnect) — if a connection attempt hangs, wake the watch and
  retry.
- The connection can time out during a long idle wait; reconnect via `bluetoothctl connect
  <address>` if so.
- This script assumes the device is already connected (or connectable) at the D-Bus level; it
  does not drive pairing or connection itself.
