# Standalone phone-free prototypes

Two scripts, both validated end-to-end against a real watch with no phone involved.

`una_ble_client.py` is a client for the **File Transfer Service**: lists directories and pulls
`.fit` activity files with correct FIT headers and matching CRC-16 checksums. Full protocol
writeup, including how this was built and what it corrected in the original phone-capture
analysis, is in `../BLE-COMPANION-protocol-spec.md` §6a.

`una_hr_probe.py` is a black-box probe for the **Custom Command Service**'s two daily-health
commands — `0x10` (whole-day aggregate) and `0x14` (hourly per-minute HR matrix). It sends the
requests, prints and decodes every reply, and deliberately includes impossible requests (a
future date, `hour=25`) to characterise the error behaviour. Findings are written up in
`../BLE-COMPANION-protocol-spec.md` §3.1; the headline is that the watch answers *everything*
with status `0x01`, signalling "no data" by an all-zero payload rather than a status code.
Every command it sends is a read — nothing is written to the watch.

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

python3 -u una_hr_probe.py <device-address>
python3 -u una_hr_probe.py <device-address> --hours 18 --retention-days 3
```

Use `python3 -u` for the probe: it prints progress as it goes, and buffering hides that.

## Known caveats

- The watch's BLE advertising window is short (both for pairing-mode discovery and for
  reconnection after a normal disconnect) — if a connection attempt hangs, wake the watch and
  retry.
- The connection can time out during a long idle wait; reconnect via `bluetoothctl connect
  <address>` if so.
- `una_ble_client.py` assumes the device is already connected (or connectable) at the D-Bus
  level; it does not drive pairing or connection itself. `una_hr_probe.py` does call `Connect()`
  with patient retries, but still will not pair.
- `le-connection-abort-by-local` on every connect attempt means the watch is not advertising.
  Either it is asleep (wake it and keep it awake) or **a phone already holds the connection** —
  this firmware takes one central at a time, so disconnect Gadgetbridge and the UNA app first.
- BlueZ may keep the device under an object path named after an old resolvable private address
  (e.g. `dev_7F_74_2A_ED_D8_77`) while its `Address` property reads the stable identity address.
  Both scripts therefore match on the `Address` **property**, never on the object path — do the
  same in anything new.
