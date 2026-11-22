# v0.24.3 regression tools

Run from an isolated QA root containing `repo/` checked out from the source bundle,
`handoff/` extracted from the already archived v0.24.1 engineering inputs, and
`gamebase/` extracted from its v0.24.1 game test ZIP. Set `CPYMO_QA_ROOT` to that
root when invoking run243.py outside that directory. Do not run against a TF card.
The SDK relative path is inherited from the handoff.

The final engineering archive's BUILD_AND_TEST.md gives executable steps and
external test results. All engine inputs use the real Source Han Medium font.
`audio_stream.c` includes the actual decoder and intercepts fseek only to count
loop boundaries; it does not substitute decoded PCM. `render_check.c` includes
the production compositor/config/textbox code and links other real engine objects.
`capture.c` drives normal libretro input, logs engine state read-only via symbol
offsets and writes actual RGB565 output frames and mixed PCM. It does not set
font/read/skip state in engine memory. Read flags come from normal script loops.
The C struct layout macros must match the core.
