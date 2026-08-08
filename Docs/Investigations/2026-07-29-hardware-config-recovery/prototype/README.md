# Standalone FTS prototype

`una_ble_client.py` is a phone-free Linux client for the UNA Watch's File Transfer Service,
validated end-to-end against a real watch: lists directories, pulls `.fit` activity files with
correct FIT headers and matching CRC-16 checksums, and writes/reads back arbitrary files up to
29 MiB byte-exact. Full protocol writeup, including how this was built and what it corrected in
the original phone-capture analysis, is in `../BLE-COMPANION-protocol-spec.md` §2.2/§6a; the
write-path throughput measurements and failure-mode findings live in
`../../2026-08-07-ble-write-path/README.md`.

`auto_pair.py` and `auto_connect_pull.py` in this directory handle pairing and connecting more
reliably than plain `bluetoothctl` commands -- the watch's advertising window is short enough
that one-shot `bluetoothctl connect` calls routinely lose the race. If BlueZ reports the device
as Paired/Bonded but connects keep cycling `['In Progress']` / `['Operation already in
progress']` forever, check the watch's own screen: if it's showing a fresh pairing prompt, the
bond is desynced (BlueZ still trusts an old bond the watch no longer recognises). Fix with
`bluetoothctl remove <address>` followed by `python3 auto_pair.py`, run directly by a human in
their own terminal (its docstring explains why -- passkey entry has to beat a short timeout that
relaying through a chat assistant reliably loses).

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
python3 una_ble_client.py <device-address> write local_file.bin /Apps/HelloWorld/ble_bench_foo.bin
```

`write`'s remote destination must fall under `WRITE_PATH_ALLOWLIST_PREFIXES` in
`una_ble_client.py` -- a hard safety allowlist, not a suggestion. Writing to an unrecognised
path, especially `0:/ble.ota` (where firmware OTA images stage), risks bricking the watch.

## Known caveats

- The watch's BLE advertising window is short (both for pairing-mode discovery and for
  reconnection after a normal disconnect) — if a connection attempt hangs, wake the watch and
  retry.
- The connection can time out during a long idle wait; reconnect via `bluetoothctl connect
  <address>` if so.
- This script assumes the device is already connected (or connectable) at the D-Bus level; it
  does not drive pairing or connection itself.
