---
type: Decision
title: Unified -i/-o CLI with input/output type inference
description: Replace the positional/live-flag split with inferred -i/-o — rtsp://, rtsps://, and pipeline: (gated) as live inputs; udp:, udp://, srt:, and filesystem paths as outputs — removing --live-rtsp/--live-udp with a loud error, with file→udp replay implemented separately by ADR 0015
decision_status: accepted
fork: 13
tags: [decision, cli, live, streaming, phase-4]
generated:
  by: claude/opus-5
  at: 2026-08-21T00:00:00Z
---

# Context

The tool had two entirely separate CLI branches in `main()`: a file path
positional that went to `mux_to_ts()` (offline conversion), and a live branch
`--live-rtsp <uri>` + `--live-udp <sink>` that went to `live_ingest()`
([[0013-live-streaming-ingest]]). `live_ingest()` already handled any
`KlvSink` sink (`udp:`, `file:`, `srt:`) and an RTSP `video_source`, with
multicast knobs (`udp_ttl`, `udp_mcast_iface`, `udp_loop`) and indefinite
`run_duration` until `SIGINT`. But the CLI surface exposed it as a separate
world: users had to know which flag pair to use, and adding a new input or
output kind meant adding a new flag pair rather than teaching the tool to read
the value it was already given.

The goal is a single `-i`/`-o` surface where the value tells the tool what it
is. `rtsp://` looks like an RTSP URI; `pipeline:` looks like a pipeline;
`udp://` looks like a UDP URI; a plain path looks like a file. Inference keeps
the common case short, makes live RTSP→file and RTSP→UDP reachable without new
flags, and preserves the deterministic `path→path` conversion byte-identical by
never routing it through the realtime sink. At decision time, `file→udp` (MP4
replay as multicast) was a separate follow-up with its own pacing and clock
questions; routing it prematurely would have hidden a partially-working live
sink behind a working offline path.

# Decision

Unify the CLI on `-i`/`--input` and `-o`/`--output` with type inference:

- **Input (`-i`/`--input <arg>` or bare positional for backward compatibility).**
  Inferred from the value:
  - `rtsp://` or `rtsps://` → live RTSP (forwarded as `video_source`)
  - `pipeline:` → live test-only path, gated by `PARROT_TEST_ALLOW_PIPELINE`
    (reject otherwise, same message as before)
  - otherwise → filesystem path → MP4 offline convert / inspection

  If both a positional and `-i` are given, error with the existing
  "more than one input file given (...)" message. A trailing `-i` without a
  value is an error, mirroring the existing `-o` check.

- **Output (`-o`/`--output <arg>`).** Already exists; type inferred:
  - `udp://` or `udp:` → UDP multicast sink
  - `srt:` → SRT sink (passed through to libmisbklv)
  - otherwise → filesystem path → MPEG-TS file

  Output-type helper normalizes UDP: `udp://host:port` → `udp:host:port`,
  `udp:host:port` unchanged, `udp://[ff02::1]:5000` → `udp:[ff02::1]:5000`, and
  a raw `udp:…` unchanged. The helper strips a leading `//` right after the
  `udp:` scheme.

- **UDP sink flags, only valid with a `udp:` output:**
  - `--iface <iface>` → `cfg.udp_mcast_iface`
  - `--ttl <n>` → `cfg.udp_ttl`, validated `0..255` (reject out of range with
    "must be 0..255")
  - `--loop` → `cfg.udp_loop = true`; `--no-loop` → `false` (default remains
    `true`)
  - Any udp flag with a non-udp output → error ("--iface is only valid with a
    udp: output", etc.).

- **Removed flags fail loud (ADR 0008 style):** `--live-rtsp` and `--live-udp`
  are no longer functional; passing either prints
  "Error: --live-rtsp/--live-udp have been removed. Pass the input and output
  instead: -i <rtsp://…> -o <path|udp://…>." and returns non-zero.

- **Routing `input × output`:**
  1. `path → path`: `mux_to_ts(input, output, options)` — byte-identical, not
     through the realtime sink.
  2. `rtsp/pipeline → {path | udp:/srt:}`: `live_ingest(cfg)` with sink
     translated (`path` → `file:<path>`; `udp://…`/`udp:…` → normalized
     `udp:…`; `srt:` → as-is), `run_duration=nullopt` (indefinite until
     `SIGINT`), `transform_options` forwarded, plus udp knobs.
  3. `path → {udp:/srt:}`: not in scope this round — at the time, error
     "file input to a live sink (udp:/srt:) is not yet supported" and non-zero.
  4. `rtsp` with no `-o`: error "live input needs an output".

Inspection mode (no `-o`, path input) keeps working exactly as today.

# Alternatives considered

## Keep the --live-rtsp/--live-udp pair as the live entry point

Keeps file and live as separate branches, each with its own flag pair. Rejected:
it duplicates what the value already says, forces a new flag pair per new kind,
and hides that `live_ingest()` already speaks `file:` and `srt:` sinks — only
the CLI pretended it was UDP-only.

## Implicit -o default for live (e.g. an ephemeral file or stdout)

Rejected: live needs a sink, and guessing one hides a missing argument behind a
working-looking run that writes nowhere useful. Explicit `-o` keeps the four
`input×output` cases honest.

## Route path→path through the realtime sink for uniformity

Rejected: the realtime path paces by the running clock and rewrites timing
differently; routing the offline conversion through it would change its bytes
and its timing model for no user-visible gain. The offline path must stay
deterministic and byte-identical.

# Consequences

- `src/main.cpp` owns the inference, validation, and routing; `mux_to_ts` and
  `live_ingest` are unchanged except for being called from one place. The usage
  text now documents `-i`/positional with inference, `-o` with inference, the
  four udp flags, and live examples (rtsp→file, rtsp→udp), keeping the existing
  attitude / image-source / output-streams sections intact, plus file replay.
- Tests: `test/cli_test.cpp` gains an explicit `-i` happy-path conversion;
  `CMakeLists.txt` plus `test/cli_reject_test.sh` gain discriminant rejection
  tests for the removed flags, udp-flag-without-udp (covering `--iface`,
  `--ttl`, and `--loop`), file→live-sink, live without output, and ttl out of
  range — all on both `parrot` and `parrot-sanitize` labels.
- A `udp:` output is validated at the CLI for a `host:port` remainder (bare
  `udp:`/`udp://` is rejected as "udp output needs host:port") rather than
  deferring to a sink-open failure.
- `cli_rejects_output_without_path` ("needs a path") and
  `cli_rejects_two_inputs` ("more than one input file") keep their exact
  messages; the new `-i` path reuses that wording.
- `path→udp` file-replay/multicast was explicitly deferred here — the routing
  emitted a clear "not yet supported" error. ADR 0015 later implemented that
  bridge for UDP and SRT.

# Assumptions / open questions

- Assumes no downstream script depends on the old `--live-rtsp`/`--live-udp`
  spelling surviving silently — the loud removal is intentional per ADR 0008.
- The pacing question for `file→udp` replay was left open here and resolved by
  ADR 0015 in favor of the file's own timeline with paced realtime emission.
- `pipeline:` remains a test-only escape hatch gated on
  `PARROT_TEST_ALLOW_PIPELINE`, unchanged in semantics, now reachable via `-i`
  rather than `--live-rtsp`.

# Citations

- [[0013-live-streaming-ingest]] — the live ingest + multicast placement this
  CLI unifies
- [[0008-force-emit-attitude-cannot-work]] — precedent for loud removal with
  guidance
- `src/main.cpp` § `print_usage` / routing — the inference and `udp://` → `udp:`
  normalization this records
- `src/live/live_ingest.hpp` — `LiveIngestConfig` fields (`udp_ttl`,
  `udp_mcast_iface`, `udp_loop`, `run_duration`, `transform_options`) this wires
