## The bug

`VariantConfig` can adopt the wrong app's identity. Its sandbox-root scan builds each candidate path as `"/" + name`, and `ObjectInfo::name` is as wide as a whole path, so the join can be clipped. A clipped path can name a different, existing alias, and the app then comes up with that alias's name and FIT sport.

The name is measured before the path is built, and a name too long to form a path is skipped and the scan carries on, as the kernel's scan does. Nothing is truncated, which also clears the `-Wformat-truncation` this line earns at the `-Os -Wall` app builds use.

## Testing the scan

The scan had no host tests, and `InMemoryFileSystem` could not carry them: it had no directories of its own, so `mkdir()` recorded nothing, every directory existed, and one never made opened as empty. It now:

- **models directories**, both those made by `mkdir()` or `IDirectory::create()` and those implied by a file's path, and fails to open one that is not there;
- **refuses what the device refuses**: one name as both a file and a directory, a removed path as a `rename()` or `copy()` source, `rename()` onto an existing file (FatFs `f_rename` gives FR_EXIST), and `close()` on a handle that is not open. `failRenames` still makes every rename fail;
- **lists in creation order** rather than name order. `WorkoutLibrary.WhenFullOfOthersTheEndOfTheNameOrderGoes` writes its files out of name order so the library has to evict within one kind; a sorted listing would hand them over in order and that eviction would go untested;
- **reads `openHandles` and `flushCounts` the same for `"/a.fit"` and `"a.fit"`**, so a leak check cannot pass because of a leading slash.

Docs/unit-testing.md has a section on testing code that scans a directory: what the fake refuses, and where it still differs from a real backend.

## Questions

- At what name length does the kernel's scan skip? This PR skips when `"/" + name` will not fit `skMaxPathLen`. If the kernel joins onto a longer prefix, the two thresholds differ.
- Does the kernel match `.uapp` case-insensitively, and does it skip hidden `.uapp` files? `Config` matches case-sensitively and does not look at `isHidden`.
