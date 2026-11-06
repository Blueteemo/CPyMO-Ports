# Launcher-only regression

QA root contains `repo/` checked out at v0.24.3a and `evidence/retroarch-1.14.0/`
from this engineering archive. That folder includes byte-exact upstream sources,
headers and SOURCES.json Git blob identifiers. Only POSIX file I/O is adapted in
ra_posix_io.c; the parser/hash/append/path/string implementations are upstream.

From QA root:

```sh
sh repo/tests/v0243a/build_ra_probe.sh
python3 repo/tests/v0243a/test_launcher_volume.py
python3 repo/tests/v0243a/test_launcher_edges.py
```

Dependencies: Linux, cc, Python3, `/bin/sh`, `/usr/bin/busybox` (tested 1.36.1).
The first suite also uses the normal host awk, then isolated BusyBox applets.
Each invocation executes the actual launcher against a recording process which
runs the real RA config parser. No real audio output, core, device or TF is used.
`test-runs/`, `edge-runs/` and evidence results are scratch fixtures only.

The edge suite's direct parser tests require smoke/global.cfg and session.cfg:
`audio_volume="1.5"` and `audio_volume="-2"` respectively; both files may also
contain config_save_on_exit and auto_overrides_enable with true/false values.
The packaged evidence includes these fixtures. No ARM core compilation occurs.
