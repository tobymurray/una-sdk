import subprocess, sys, os, shutil

HDR = "Libs/Header/SDK/SensorLayer/DataParsers/SensorDataParserRrInterval.hpp"
orig = open(HDR).read()

MUTATIONS = [
 ("M1 invert NO_SKIN_CONTACT",
  [("return (flags() & Flag::NO_SKIN_CONTACT) != 0u;",
    "return (flags() & Flag::NO_SKIN_CONTACT) == 0u;")]),
 ("M2 getFieldsNumber() returns 1 (the parse minimum)",
  [("            static constexpr uint8_t getFieldsNumber()\n            {\n                return Field::COUNT;\n            }",
    "            static constexpr uint8_t getFieldsNumber()\n            {\n                return 1;\n            }")]),
 ("M3 swap SOURCE/FLAGS indices",
  [("                SOURCE = 1, ///< producer of this interval, u32 (0 = not reported)\n                FLAGS  = 2, ///< quality / continuity bits, u32 (0 = not reported)",
    "                SOURCE = 2, ///< producer of this interval, u32 (0 = not reported)\n                FLAGS  = 1, ///< quality / continuity bits, u32 (0 = not reported)")]),
 ("M4 read rr_ms through .u instead of .f",
  [("                return (mData.getFieldCount() >= 1) &&\n                       std::isfinite(mData.f[Field::RR_MS]) &&\n                       (mData.f[Field::RR_MS] > 0.0f);",
    "                return (mData.getFieldCount() >= 1) &&\n                       std::isfinite(static_cast<float>(mData.u[Field::RR_MS])) &&\n                       (static_cast<float>(mData.u[Field::RR_MS]) > 0.0f);"),
   ("                return isDataValid() ? mData.f[Field::RR_MS] : 0.0f;",
    "                return isDataValid() ? static_cast<float>(mData.u[Field::RR_MS]) : 0.0f;")]),
 ("M5 isDataValid() drops the fieldCount>=1 short-circuit",
  [("                return (mData.getFieldCount() >= 1) &&\n                       std::isfinite(mData.f[Field::RR_MS]) &&",
    "                return std::isfinite(mData.f[Field::RR_MS]) &&")]),
 ("M6 isDataValid() drops the isfinite/positive value guard",
  [("                return (mData.getFieldCount() >= 1) &&\n                       std::isfinite(mData.f[Field::RR_MS]) &&\n                       (mData.f[Field::RR_MS] > 0.0f);",
    "                return (mData.getFieldCount() >= 1);")]),
 ("M7 Source::ECG drifts 3 -> 4",
  [("                ECG      = 3, ///< electrocardiogram", "                ECG      = 4, ///< electrocardiogram")]),
 ("M8 Source::EXTERNAL drifts 2 -> 5 (kernel-arbiter drift)",
  [("                EXTERNAL = 2, ///< external BLE strap", "                EXTERNAL = 5, ///< external BLE strap")]),
 ("M9 swap DISCONTINUITY / ARTIFACT_SUSPECT bits",
  [("                DISCONTINUITY    = 1u << 0, ///< not contiguous with the previous interval (gap / reconnect / first)\n                ARTIFACT_SUSPECT = 1u << 1, ///< producer flags this interval as likely artefactual",
    "                DISCONTINUITY    = 1u << 1, ///< not contiguous with the previous interval (gap / reconnect / first)\n                ARTIFACT_SUSPECT = 1u << 0, ///< producer flags this interval as likely artefactual")]),
 ("M10 rr_ms silently reinterpreted as seconds (getBpm uses 60.0f)",
  [("                const float bpm = 60000.0f / rr;", "                const float bpm = 60.0f / rr;")]),
 ("M11 getBpm() drops the infinity guard",
  [("                const float bpm = 60000.0f / rr;\n                return std::isfinite(bpm) ? bpm : 0.0f;",
    "                return 60000.0f / rr;")]),
 ("M12 timestamps no longer gate on validity",
  [("                return isDataValid() ? mData.getTimestamp() : 0;", "                return mData.getTimestamp();"),
   ("                return isDataValid() ? mData.getTimestampUs() : 0;", "                return mData.getTimestampUs();")]),
]

results = []
for name, edits in MUTATIONS:
    txt = orig
    ok = True
    for a, b in edits:
        if a not in txt:
            ok = False
            break
        txt = txt.replace(a, b, 1)
    if not ok:
        results.append((name, "PATCH-DID-NOT-APPLY", ""))
        continue
    open(HDR, "w").write(txt)
    bp = subprocess.run(["cmake","--build","/src/_b","-j","8","--target","una-sdk-host-tests"],
                        capture_output=True, text=True)
    if bp.returncode != 0:
        tail = "\n".join([l for l in bp.stdout.splitlines()+bp.stderr.splitlines() if "error" in l.lower()][:3])
        results.append((name, "KILLED (compile error)", tail))
        continue
    rp = subprocess.run(["/src/_b/una-sdk-host-tests","--gtest_filter=RrIntervalParser.*"],
                        capture_output=True, text=True)
    out = rp.stdout + rp.stderr
    failed = [l.strip() for l in out.splitlines() if l.startswith("[  FAILED  ] RrIntervalParser.")]
    failed = sorted(set(failed))
    if rp.returncode == 0:
        results.append((name, "*** SURVIVED ***", ""))
    else:
        results.append((name, "KILLED (%d test(s))" % len(failed), "; ".join(f.replace("[  FAILED  ] ","") for f in failed[:6])))

open(HDR,"w").write(orig)
subprocess.run(["cmake","--build","/src/_b","-j","8","--target","una-sdk-host-tests"], capture_output=True)

print("\n%-58s %-24s %s" % ("MUTATION","VERDICT","DETAIL"))
print("-"*140)
for n,v,d in results:
    print("%-58s %-24s %s" % (n,v,d[:60]))
