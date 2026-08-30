# Companion data channel: what was missing, what landed, what is left

**Answered, mostly.** This began as an RFC arguing that a third-party watch app had no
supported way to receive data from off the watch: an athlete id, an account token, a setting
chosen on a website, a list curated on a phone. It had to be compiled in or typed on the watch,
and a four-button device is not a serious way to enter anything longer than a few characters.
Whole categories of app were therefore unbuildable, the motivating case being a parkrun athlete
barcode, and the shape generalising to transit passes, membership ids, loyalty codes and saved
routes.

Upstream has since shipped `SDK::AppConfig` (`Docs/app-config-fields.md`). An app declares
configuration fields in its `app-manifest.json`, the companion app prompts the user and writes
the answers to a JSON file beside the `.uapp` over the File Transfer Service, and the app reads
them at launch. That is the request, answered in a better shape than it was asked: declared
fields in a file a service that already existed can write, needing no new kernel channel, no
envelope and no chunking.

The inventory of the SDK that used to make up most of this document is deleted rather than
maintained. It was an argument that a thing was absent, the thing is no longer absent, and
every claim in it was re-derivable by reading the tree, which is why the parts that went stale
did so silently. One that did: it recommended reusing the per-app `Sender` idiom, and upstream
has since retired `Sender` in favour of `SDK::send_msg<T>(kernel, args...)`.

## What is still not answered

`Docs/app-config-fields.md` § 1.1 says the first two of these itself, so they are constraints
rather than oversights:

- **Nothing reaches a running app.** Config is read once at launch. A change made on the phone
  appears the next time the app starts. This is provisioning, not a data channel, and an app
  that wants to react to something now still has nowhere to receive it.
- **Nothing may be secret.** The values file is plaintext on a FAT volume readable over USB
  mass storage and over BLE, which is why there is no `secret` field type. The API-token case
  in the original problem statement is still unserved.
- **`SettingsSerializer` is still copied into each app that wants it.** Only the file plumbing
  generalises; the per-app structs differ in enum values, nested types and load-time
  normalisation, so the split is a per-app codec over shared plumbing rather than a templated
  settings struct.

## Why the phone has to be the transport

Kept because it is the part of this analysis that was never derivable from the SDK, and it
still bounds anything built on top of `AppConfig`.

BLE is the only radio: no Wi-Fi, no `IWifi`, nothing WiFi-shaped anywhere in the SDK. A watch
app cannot reach a website, so any "get data from a website" story is necessarily
`website -> phone app -> BLE -> watch -> app`. The first hop is ordinary web work and no
concern of a watch SDK. The last already existed. The third was the missing one.

This is the same architecture Garmin and Apple arrived at, from the same constraints:

- **Garmin** (Connect IQ): the user enters a value in the Garmin Connect phone app's per-app
  settings screen, Connect syncs it over BLE into the watch app's persistent storage, and the
  app reads it at runtime. Almost exactly what `AppConfig` now does. Worth knowing that Connect
  IQ has no native barcode widget either, so the community parkrun apps render the bars
  themselves, which is what an app here would also have to do.
- **Apple**: not an app at all. A `.pkpass` bundle carries a `barcode` field, message plus
  format such as `PKBarcodeFormatCode128`, is generated server-side, added to Wallet on the
  phone, and syncs to the Watch on its own. No third-party watch code renders it; one system
  component draws any pass.

The Apple model is the more instructive one, and it is the argument against solving this per
app: do not make every app that needs to show a user id solve provisioning and rendering
independently. Provision once, through a shared mechanism.

Nor is any of this Una-specific. InfiniTime and Gadgetbridge, Bangle.js and WatchY each solved
the same problem separately for custom watch faces and per-app settings.
