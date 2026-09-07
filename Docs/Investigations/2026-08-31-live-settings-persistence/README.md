# UNA Watch live settings persistence

Own-hardware investigation, continuing from `watch-apps/NotifyToggle`: a live, in-RAM,
immediate-effect write of `phone.notifications` (one byte in the kernel's live settings
struct) was already working and verified before this doc's session started — see
`HANDOFF-PROMPT.md` in this folder for the full prior-session context this investigation
picks up from. The open problem: nothing yet makes that write survive a reboot, i.e. nothing
writes the change back to `2:/settings.json` on flash.

**Ground rule:** same convention as `../2026-07-29-hardware-config-recovery/` — every claim
below is tagged CONFIRMED / LIKELY / UNVERIFIED / REFUTED / CONTRADICTORY with its
corroborating method.

**Method so far:** static disassembly only (Ghidra headless + `arm-none-eabi-objdump`) against
`firmware-dumps/1.4.0/flash_08000000_4MB.bin` (CRC32 `0x14009D03`, re-verified this session).
No live device writes have been attempted yet for this specific goal — everything below is
read-only analysis of the flash image, plus (new this session) static analysis of the
companion Android app's decompiled JS bundle.

## Verification ledger

| Claim | Status | Method |
|---|---|---|
| Settings struct base = `0x20010cb0`, `phone.notifications` = base+5, `watchFaceId` = base+8 | **CONFIRMED**, 3 independent sources | JSON loader's field-write offsets; `save()`'s independent read of the same offsets; a factory-reset/constructor's own default-init values (notifications default=1). Also live-verified on real hardware in the prior session (read/write/readback matched `settings.json`'s actual stored value). |
| Settings class vtable base = `0x0817579c` | **REFUTED** | Original single-pass finding was one slot off. The region `0x08175760`-`0x081757e0` is actually 4 contiguous 6-word Itanium-ABI vtables for 4 different classes; the Settings class is the 4th, at `0x081757b0`, not `0x0817579c`. |
| Corrected Settings vtable base = `0x081757b0`, slots: `+0x08`=load(), `+0x0c`=save() | **CONFIRMED** | Found the real vptr-store instruction inside the constructor: `0x080abc46: ldr r3,[pc]@(→0x081757b0); 0x080abc48: str r3,[r4,#0]`, r4=`this`. |
| Constructor = `0x080abbb4` (thumb `0x080abbb5`) | **CONFIRMED** — **NEVER CALL LIVE, see guardrails in HANDOFF-PROMPT.md** | 3 real callers found (`0x0806b5aa`, `0x0806e972`, `0x0807f9b4`, all magic-static lazy-init guards). This is the object's real constructor/default-initializer — "construct" and "reset to factory defaults" are the same code path for this class, confirming why the prior session's ban on calling it is correct. |
| The lock at `0x0811ea74`/`0x0811eaac` is Settings-specific | **REFUTED** | It's a compiler-emitted guard-variable helper shared by ~149 unrelated function-local statics kernel-wide, not a dedicated Settings mutex. |
| `local_settings.json` is the same class as Settings (shares this vtable) | **REFUTED** | Found a structurally identical but distinct constructor at `0x080ab998` writing a *different* vtable pointer (`0x08175798`) into a differently-shaped struct — a sibling class with its own vtable/save/load pair. (Link to `local_settings.json` specifically not yet confirmed.) A general persistence mechanism built here will need a second offset table + vtable for that file — it isn't free. |
| `dailyGoals.activityMinutes/steps/floors` = base+`0x18`/`0x1c`/`0x20` | **CONFIRMED** (corrected from an earlier guess of +24/+28/+32) | Cross-checked against `save()`'s decompile, the constructor's own defaults (30/5000/10), and live `settings.json`. |
| `weight` = float at base+`0x28` | **CONFIRMED** | `save()` calls a distinct float-taking writer on this offset. |
| Undocumented 4-byte int at base+`0x24` (between `floors` and `weight`) | **UNVERIFIED** | Likely `height` by elimination/struct layout; not cross-checked against a live value. |
| `heartRateZones` = 6 bytes at base+`0x10`..`0x15` | **CONFIRMED**, contradiction resolved | An earlier pass flagged a contradiction (constructor writes what looked like a 4-byte pointer at this offset, not a 6-byte array). Resolved: that "pointer" is a pointer to a hardcoded 6-byte default-value blob (`[95,114,133,152,171,190]`) being copied in via `0x0806f944`, a generic struct-default-initializer — not a real struct field at that offset. The struct field itself is confirmed at `+0x10`..`+0x15`. |
| `save()` (flash `0x080aba60`/thumb `0x080aba61`) has a real caller reachable via direct call or vtable dispatch | **REFUTED** (genuine negative result, exhaustively checked) | Zero direct `bl 0x80aba60` hits anywhere in the 4MB image. Zero indirect-vtable-dispatch hits through slot `+0x0c` near any of ~3700 `blx` sites (checked both tight and 10-instruction windows). For contrast, `load()` **does** have 2 confirmed real callers (`0x0806b61c`, `0x0806bcd0`), so the class/offset math is doubly cross-validated for load, but the save path is unreached by every static pattern checked so far. |
| The `"2:/settings.json"` string literal (`0x0815954f`) leads to the real flash-write function | **REFUTED as a generic writer** | Its only real caller (`0x08073270`-`0x080733fe`, via a `pathEquals` helper at `0x08072fa0`) is a file-transfer-FSM state handler that, on a path match, calls a struct default-initializer (`0x0806f944`, same one from the heartRateZones finding) **twice** and copies 48 bytes between two local buffers — it looks like a disguised factory-reset trigger reachable via some external write (**LIKELY** BLE FTS, given it routes through `FileSystemGuard::getFullPath` first — **UNVERIFIED** whether the FTS write handler is really what dispatches into this), not a path that parses and stores arbitrary caller-supplied JSON. Neither struct base (`0x20010cb0`) nor object base (`0x20010ca8`) appear anywhere in this handler's code region, meaning it operates on a **local/staging struct**, not the live global Settings object. |

## Bottom line as of this pass

**No confirmed flash-write entry point for `2:/settings.json` exists yet**, after two
independent static-analysis passes (the Settings class's own save()/vtable path, and the
`"2:/settings.json"` string-literal xref path) both dead-ended. The struct-offset picture for
`phone.notifications` itself remains solid (now via a third independent source), so the live
in-RAM write in `watch-apps/NotifyToggle/Software/Libs/Sources/LiveSettings.cpp` is not in
question — only the reboot-survival half of the goal is still open.

## Companion Android app analysis — the mystery resolved

The real UNA phone app (`com.unawatch`, React Native/Hermes, APKs at
`/home/toby/una-apk/com.unawatch-2.1.16/` and `/home/toby/Downloads/una-2-2-6.apk`) toggles
`phone.notifications` over Bluetooth in normal use. Its business logic is not in the Java/Kotlin
layer (that's all bundled third-party libraries — React Native core, Firebase, BLE plumbing
libraries) — it's compiled to Hermes bytecode in `assets/index.android.bundle` (7.3 MB, magic
`c6 1f bc 03 c1 03 19 1f`, confirmed HBC). Decompiled with `hbc-decompiler`
(`/home/toby/.local/bin/hbc-decompiler`, a pre-existing pipx install from a prior session) to
1.4M lines of reconstructed JS.

| Claim | Status | Method |
|---|---|---|
| The app persists settings by calling the watch's internal `Settings::save()` via some BLE command | **REFUTED** | Read the actual decompiled code, 3 levels deep (below). |
| The app persists settings by a **whole-file JSON overwrite over BLE File Transfer** | **CONFIRMED** | `saveSettings` (`decompiled.js:1258464`) builds a merged JS object with exactly the fields this investigation already found in the firmware struct (`units`, `watchFaceId`, `phone.notifications`, `heartRateZones`, `dailyGoals`, `height`, `weight`, `gender`, `dateOfBirth`, `version`), `JSON.stringify`s it, and passes it to `writeSettingsJson` (`decompiled.js:826558-826567`), which calls a generic bridge closure with `('/settings.json', jsonString, undefined, 5000)`. That closure (`_closure2_slot33`, defined at `1102822`) traces to `useHandleWriteFile` — **the identical generic file-write hook** the app also uses for firmware updates and activity-file uploads (confirmed via the sibling handler map at `826474-826478`: `handleReadFile`/`handleWriteFile`/`handleUpdateFirmware` share the same `useHandleRead/WriteFile` hooks). Its body calls a native-bridge `writeFile(path, bytes, progressCb, timeout)` with a `"File upload progress: %"` log — a **generic chunked upload**, not a short opcode/command write. |
| CCS/CANS (the `554E4100-...` custom-command services) are involved in settings sync | **REFUTED** | Only 6 hits for that UUID prefix anywhere in the 1.4M-line bundle, all in a Settings-screen UI/help-text block, not BLE-protocol code. The app's settings-sync path never touches CCS/CANS. |

**This directly explains the `save()`-has-zero-callers finding above: the phone app never calls
it.** Persistence in normal operation happens entirely client-side (the phone reads the file,
mutates a plain object, re-serializes, pushes the whole file back over the same FTS write path
already characterized — byte-exact, extensively benchmarked — in `investigate/ble-large-file-transfer`'s
`Docs/Investigations/2026-08-07-ble-write-path/`), and the watch's own `load()` (confirmed to have
live callers) re-parses it, presumably on next boot or next access. It also reframes the
`"2:/settings.json"`-string-xref finding above: the struct-initializer called twice in that FSM
handler is now much more plausibly `load()`'s own documented "reset-to-defaults, then parse JSON
over it" fallback pattern (see the original handoff's loader description), triggered right after
a real FTS write to that path completes — not a disguised factory-reset trap. Not independently
re-verified this pass; flagged as the likely correct reading, not yet CONFIRMED.

## Path 2 (in-process route): the real write primitive, found and evidenced

Traced forward from the FTS write-handler FSM. **`FUN_08073270`** (flash `0x08073270`-`0x080733fe`)
is the real FTS write-completion handler — confirmed via an embedded diagnostic log call,
`FUN_080c4b98("Kernel","Backend",3,"onFileWrite",0x5c0,"Settings_updated")` — reached via a live
tail-branch from the FSM dispatcher (not dead code). On a `settings.json` path match it calls
`FUN_0806dd54`/`FUN_0806de64`, which implement an **atomic tmp-file + backup + rename write**
built from a small, non-virtual `File`-object method cluster at `0x0809b2cc`-`0x0809b648`.
`FUN_0806dd54`'s success path logs `"Settings %s recovered from backup"` — this is the actual
settings-file durability machinery, not a guess.

| Function | Address | Signature (register-evidenced, not decompiler-guessed) | Verdict |
|---|---|---|---|
| open | `0x0809b254` | `(File* this, int flag1, int flag2)` → `f_open(FIL* fp=this+0x108, path=this+4, mode)` at `0x080d1298`. Mode: `(1,1)`→`0xb` (CREATE_ALWAYS\|WRITE\|READ), `(1,0)`→`0x13` (OPEN_ALWAYS\|WRITE\|READ), `(0,_)`→`0x1` (READ) | **CONFIRMED** |
| write | `0x0809b334` | `(File* this, void* buf, uint32 len, uint32* writtenOut)` → literally `f_write(FIL*, buff, btw, bw)` at `0x080d1730` (r1/r2/r3 never touched between entry and the call — exact passthrough match to FatFs's real signature). Returns 1 iff `f_write`==FR_OK **and** `*writtenOut==len` | **CONFIRMED** |
| close | `0x0809b450` | `(File* this)` → `f_close(FIL* fp=this+0x108)` at `0x080d191c` | **CONFIRMED** |
| release/reset | `0x0809b2cc` | `(File* this)` — safe to call unconditionally (no-op if never opened); clears isOpen byte and zeroes the FIL region | **CONFIRMED** |
| set path (avoid) | `0x0809b4a8` | Internally does a **virtual** call through `[this+0]+0x24`/`+0x28` before copying the path — requires a valid vtable pointer. **Recommend skipping.** | **CONFIRMED**, flagged unsafe for a zero-vtable object |
| set path (use instead) | `0x0802b3ea` | `(char* dst, const char* src, uint maxlen)` — bounded strcpy, zero-pads, no vtable dependency | **CONFIRMED** |
| exists/delete/rename | `0x0809b5a0` / `0x0809b648` / `0x0809b5d8` | delegate through a filesystem singleton via `FUN_0806e52c()` | **CONFIRMED** to exist, singleton itself not traced |

**Object layout** (856 bytes / `0x358`, zero-initialized; CONFIRMED via a constructor-shaped
helper at `0x0809b498` calling `0x811ea64(this, 856)`): `+0x00` = vtable ptr (**only needed if
calling the skipped `0x0809b4a8`** — irrelevant if path is set via `0x0802b3ea` instead),
`+0x04`..`+0x103` = 256-byte path buffer, `+0x104` = isOpen flag byte, `+0x108`..`+0x357` =
592-byte FIL struct region.

**Recommended call site shape**: zero-init an 856-byte local `File` buffer (leave the vtable slot
zero — safe as long as `0x0809b4a8` is never called on it), set the path via `0x0802b3ea`, then
`open(0x0809b254)` → `write(0x0809b334)` → `close(0x0809b450)` → `release(0x0809b2cc)` directly.
This is a **much smaller, better-evidenced, entirely non-virtual call surface** than
`Settings::save()` would have been, with real confirmed callers exercising the exact same
open/write/close/release shape in normal firmware operation, for this exact file.

**The read counterpart is also now confirmed**, closing the gap needed for a safe read-modify-write
(rather than regenerating the file from only partially-known struct fields, which risked dropping
real personal data):

| Function | Address | Signature | Verdict |
|---|---|---|---|
| `f_read` (raw FatFs) | `0x080d1588` | Same argument-save/passthrough shape as the confirmed `f_write` | **CONFIRMED** |
| File-class read wrapper | `0x0809b4e8` | `(File* this, void* buf, uint32 len, uint32* bytesReadOut)` → `f_read(fp=this+0x108, buf, len, bytesReadOut)`, returns 1 on `FR_OK`. **Does not** itself check `*bytesReadOut==len` — a caller wanting to detect a short read must check that itself | **CONFIRMED**, 5 real callers found (`0x0808b05e`, `0x0808b420`, `0x0808b58a`, `0x080a0946`, `0x080bacf2`) |
| file size (no call needed) | `File_object + 0x118` (`fp+0x10`) | 8-byte `FSIZE_t`, readable directly after a successful open — matches real FatFs `FFOBJID` layout once 64-bit alignment padding is accounted for | **CONFIRMED** |

**Full toolkit for the call site**: open read-only (`0x0809b254(this,0,0)`) → read size at `+0x118`
→ read content (`0x0809b4e8`) → close/release → do the minimal textual field edit → open
write/create (`0x0809b254(this,1,1)`) → write (`0x0809b334`) → close/release.

**Confirmed exact current file content** (from `NotifyToggle/DeviceBackups/settings.json.20260831T205112.bak`,
245 bytes, no whitespace):
```json
{"units":"metric","watchFaceId":0,"phone":{"notifications":false},"heartRateZones":[92,110,129,147,166,184],"dailyGoals":{"activityMinutes":30,"steps":5000,"floors":5},"height":190,"weight":90,"gender":"M","dateOfBirth":"1990-01-01","version":2}
```
This resolves the previously-unidentified struct field at `+0x24` as almost certainly `height`
(190, an int — fits between `dailyGoals.floors` at `+0x20` and `weight` at `+0x28`), and confirms
the target substring for a minimal splice is unambiguous: `"notifications":false` /
`"notifications":true`, compact (no spaces), inside `"phone":{...}`. Not yet implemented or
called live — see "what's next" below.

## Path 2: end-to-end, CONFIRMED live on real hardware (2026-09-01)

The full plan was implemented (`NotifyToggle/Software/Libs/{Header,Sources}/SettingsPersist.{hpp,cpp}`,
wired into `Gui.cpp`'s R1 handler) and proven end-to-end on the real device:

1. Backed up `settings.json` fresh (`DeviceBackups/settings.json.20260901T133623.bak`, md5-verified
   identical to the live file before the test).
2. Watch showed notifications **off**. Pressed R1 (enable). `gui-debug.log`/`service-debug.log`
   show the full sequence: `LiveSettings` RAM write (raw=0x01, readback confirmed) → `SettingsPersist`
   reads the real current file content → splices exactly the `notifications` field → writes the
   whole file back → **`SettingsPersist: write verified OK`**.
3. **Full power cycle** (not just app exit).
4. Reconnected over USB. `settings.json` on flash now reads
   `"phone":{"notifications":true}` — diffed against the pre-write backup, confirming **every
   other byte of the file is unchanged**, only the one field flipped (245→244 bytes, `false`→`true`).
5. The post-reboot `gui-debug.log` session's very first read shows `LiveSettings: read raw=0x01`
   — i.e. the kernel's own `load()` picked up the new on-flash value fresh at boot, independently
   confirming the write reached real flash and the standard boot path reads it back correctly, not
   just this app's own file-read code.

This satisfies Phase D's falsification bar exactly ("must still read correctly after a full power
cycle... an in-RAM-only change confirmed by app readback is not sufficient") using the direct
overwrite (not the atomic tmp+backup+rename pattern — see below, deferred at this point, closed
in the next session).

## Crash-atomic commit (2026-09-01, follow-up session)

The direct overwrite above was deliberately not crash-safe. Investigated whether the firmware's
own atomic-write functions (`0x0806dd54`/`0x0806de64`, confirmed above to exist) could be called
directly instead of hand-rolling the pattern.

**REFUTED as directly callable.** Both take exactly one argument — r0 only, r1/r2/r3 never
explicitly set at either of the two call sites in their one confirmed caller (`FUN_08073270`).
That argument is the live Settings object pointer (`0x20010ca8`), obtained via a magic-static
accessor (`FUN_0806e95c()`) whose guarded body calls the already-banned constructor (`0x80abbb4`)
and returns that fixed literal. `FUN_0806dd54` is not a generic "write these bytes atomically"
function — it's a self-check/repair routine coupled to the live Settings object's own internal
state: a dirty-flag gate at `obj+0x398` (via `FUN_0806dd24`), a path-string field at `obj+0x38`,
an *embedded* `File` object at `obj+0x40`, and a serialization source buffer at `obj+0x3f0` fed
into an uncharacterized helper (`FUN_0806d8ac`). It serializes the object's current fields,
self-validates by round-tripping through `load()` (vtable `+0x08`), and only on success proceeds
toward a commit; on failure it falls into `FUN_0806de64`'s restore-from-`.bak` logic (confirmed
via the literal-pool string `"Settings '%s' recovered from backup"`, matching the earlier pass's
finding). **Not a portable `(path, buf, len)` shape** — calling it standalone would mean depending
on `FUN_0806d8ac`'s uncharacterized buffer format, the same class of risk `Settings::save()`
would have been. Refused for the same reason.

**VIABLE instead: hand-roll the same algorithm using independently-evidenced primitives.**
Disassembled the three general-purpose kernel file utilities the firmware's own pattern delegates
to:

| Function | Address | Signature | Verdict |
|---|---|---|---|
| exists | `0x0809b5a0` | `bool exists(const char* path)` — resolves the filesystem singleton fresh via `FUN_0806e52c()`, tail-dispatches vtable slot `+0xc` | **CONFIRMED**, 55 real callers (full-image `bl` scan) |
| delete | `0x0809b648` | `bool delete(const char* path)` — same shape, vtable slot `+0x10` | **CONFIRMED**, 41 real callers |
| rename | `0x0809b5d8` | `bool rename(const char* oldPath, const char* newPath)` — same shape, vtable slot `+0x14` | **CONFIRMED**, 21 real callers |

All three obtain the filesystem singleton internally on every call (same magic-static pattern
already trusted elsewhere in this investigation — guard variable, lock at
`0x811ea74`/`0x811eaac`, constructs via `0x809baf0`; singleton lives at fixed RAM address
`0x2002dd08`, confirmed via its literal pool) — the caller never manages it, and no singleton
address needed to be added to `SettingsAddresses::AddressSet`. 55/41/21 real callers each is
overwhelming evidence these are genuine general-purpose kernel utilities, not Settings-specific —
by far the best-evidenced primitives in this whole investigation.

Re-verified `FUN_0806d930` (the "clean reusable form" an earlier pass summarized) instruction-by-
instruction to confirm the exact algorithm to replicate:
```
tmpPath = realPath + ".tmp"; bakPath = realPath + ".bak"
setPath(File, tmpPath); open(File, CREATE_ALWAYS)
  if open ok: write(File, buf, len, &writtenOut)
    if written == len: close(File)
    else: treat as failed
  release(File)   // always
if write succeeded:
    if exists(realPath):
        if exists(bakPath): delete(bakPath)
        rename(realPath, bakPath)
    rename(tmpPath, realPath)      // commit
else:
    if exists(tmpPath): delete(tmpPath)
```
`.tmp`/`.bak` suffix literals re-confirmed identical to the earlier pass's finding. This is
Settings-object-coupled in its own implementation (uses `obj+0x40` as its embedded `File`,
`obj+0x38` as the real path) — not itself callable — but the *algorithm* is fully portable using
primitives already independently evidenced: the existing `File` open/write/close/release plus the
now-confirmed `exists`/`delete`/`rename`. No need for the string-concat helper (`FUN_0806d914`)
either — `.tmp`/`.bak` paths are plain compile-time string constants in this app.

**Implemented and built** (not yet re-tested live on hardware since this change — the original
direct-overwrite path was already proven live; this changes the write mechanism, not the RAM read
path or the splice logic, but a fresh live confirmation is still warranted before calling this
closed): `SettingsAddresses::AddressSet` gained three new fields (`fileExistsAddr`,
`fileDeleteAddr`, `fileRenameAddr`); `SettingsPersist.cpp`'s direct-overwrite `writeWholeFile()`
was replaced with `writeTmpFile()` + `commitTmpFile()` implementing the algorithm above exactly,
with the same fail-closed logging discipline as the rest of this app.

## Status (2026-09-01, follow-up session)

The choice above was made: the in-process route, fully self-contained on-watch, no external
BLE/phone round-trip. Both routes' open questions from this section are now resolved — `0x806dd54`/
`0x806de64` (named in route 2 as still-untraced) turned out not to be directly callable at all (see
"Crash-atomic commit" above), and the algorithm was replicated instead using independently-evidenced
primitives.

**What's genuinely still open, as of this session:**
- **Live re-test needed.** The atomic-commit change hasn't been re-run end-to-end on real hardware
  yet — the direct-overwrite path was proven live; this changes the write mechanism underneath the
  same splice logic, and deserves its own live confirmation before being called fully closed.
- **Single-field scope.** Everything here handles `phone.notifications` only. Extending to another
  field (or to `local_settings.json`, confirmed to be a *different* class with its own vtable, not
  free) means deriving and cross-validating one more offset/class, same process as this field.
- **`SettingsAddresses`'s table has exactly one entry (1.4.0).** Any firmware update — even one
  that still satisfies the manifest's `minKernelVersion` floor — is refused outright by the
  runtime version gate (`Gui.cpp`'s `resolveFirmwareSupport()`) until it gets its own RE pass and
  table row.

## Related investigations (other branches, not merged here)

- `../2026-07-29-hardware-config-recovery/` (this branch) — MCU identity, no-isolation
  primitive, the original BLE FTS protocol characterization this investigation's phone-app
  work builds on.
- `investigate/ble-large-file-transfer` branch,
  `Docs/Investigations/2026-08-07-ble-write-path/README.md` — an already-proven, extensively
  benchmarked BLE FTS **write** path (byte-exact up to 29 MiB, resume works, and critically:
  the firmware performs almost no path validation on write, only a client-side scratch-prefix
  allowlist restricted testing to `/Apps/HelloWorld/ble_bench_*`). This is a live candidate
  alternative to the save()/vtable route above: write a minimally-edited `settings.json`
  straight back over the already-proven FTS write path, sidestepping the (apparently
  unreachable) `save()` function entirely. Not yet attempted against this specific path or
  file — flagged as the likely next real step once the phone-app analysis is in.
