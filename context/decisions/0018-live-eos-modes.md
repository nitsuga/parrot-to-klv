---
type: Decision
title: ADR 0018 — End a live run when the source goes idle, with an opt-in reconnect mode that rolls the output
description: An idle timeout is the primary end-of-source signal for live ingest, with EOS as an early exit and a backend error as the failure path; the default mode finalizes the output and exits, while an opt-in reconnect mode polls the source on an interval and starts a fresh pipeline, reusing a live output address and rolling a file output through a _000n counter
decision_status: accepted
fork: 16
tags: [decision, live, cli, rtsp, reconnect, streaming, timeout]
generated:
  by: claude/opus-5
  at: 2026-08-29T07:45:52Z
sources:
  - id: parrot-to-klv-94
    resource: https://github.com/nitsuga/parrot-to-klv/issues/94
    title: Live pipeline errors are not detected when no KLV is received
  - id: libmisbklv-adr-0035
    resource: https://github.com/nitsuga/libmisbklv/blob/main/context/decisions/0035-live-insert-terminal-status.md
    title: libmisbklv ADR 0035 — Nonblocking terminal status for asynchronous insert sources
  - id: libmisbklv-59
    resource: https://github.com/nitsuga/libmisbklv/pull/59
    title: libmisbklv#59 — review establishing that pipeline EOS is unobservable before close()
---

# Context

The production live path runs indefinitely: `parrot-to-klv rtsp://… -o out.ts`
sets `run_duration = std::nullopt` and drives until `SIGINT`. When a sortie
ends, the run has nothing left to do, and nothing notices — the loop keeps
spinning at its 10 ms idle sleep against a dead pipeline until a human
intervenes. `live_ingest.hpp` already concedes the hole: *"finite live sources
must be stopped manually."*

Live input is RTSP only. `is_live_input_value` accepts `rtsp://`, `rtsps://`,
and `pipeline:`, and the last is a hermetic test escape hatch gated behind
`PARROT_TEST_ALLOW_PIPELINE`. `udp:` and `srt:` are *sink* schemes, for `-o`.
So this decision concerns exactly one transport.

Waiting after the source stops is not preserving a capability. The GStreamer
backend builds `rtspsrc` once and has no reconnect, retry, or relaunch logic
anywhere, so a second sortie — a new RTSP session needing a fresh
DESCRIBE/SETUP/PLAY — is never picked up by the existing pipeline. The choice
when the source stops is between hanging on a dead pipeline and doing something
useful, not between continuous capture and stopping.

**The hard part is detecting that the source stopped.** A clean RTSP TEARDOWN
produces EOS, but that is the polite ending, and it is not the one that matters
most. An aircraft that loses power, or a link that fails badly, sends no
TEARDOWN: `rtspsrc` sits on a socket that has simply gone quiet. Whether that
ever surfaces depends on TCP keepalive or the server's session timeout, which
is typically around a minute. Terminal *failure* is already reported by
`KlvSink::poll()`,[^parrot-to-klv-94] and clean EOS is deliberately absent from
that contract because a live insert cannot observe a pipeline-level EOS before
`close()`.[^libmisbklv-adr-0035] Neither covers the silent stall, which is the
common real-world case.

# Decision

Treat *idleness* as the end-of-source signal, and give live input two operating
modes.

## Ending a session

A live session ends when any of these occurs, whichever comes first:

1. **Idle timeout (primary).** No telemetry has arrived for
   `--idle-timeout` seconds. This is the only signal guaranteed to fire, and it
   covers the cases the others miss — power loss, a hard link failure, a source
   that stops without saying so.

   **The timer arms at drive-loop entry, not at the first sample.** A source
   that connects and then never delivers is the same failure as one that stops,
   and arming late would leave exactly the hang this decision exists to remove.
   One timeout covers both: by the time the loop runs, `open_insert` has already
   built and prerolled the pipeline, so the budget is for first telemetry alone
   and no separate, longer startup timeout is warranted.
2. **EOS (early exit).** A clean RTSP teardown ends the session immediately
   rather than waiting out the timeout. An optimization, never the sole
   mechanism — and not implementable today: the accepted upstream `poll()` is
   failure-only, so parrot has no EOS signal to act on. It arrives with the same
   libmisbklv fork that closes the video-only gap, and the idle timeout covers
   the case correctly, if less promptly, until then.
3. **Backend error.** Already implemented; unchanged by this proposal.
4. **`SIGINT`/`SIGTERM`.** Unchanged.

`--idle-timeout` defaults to **10 seconds**, with `0` disabling the timeout
entirely (restoring today's run-forever behavior as an escape hatch). At 30 fps
a 10-second gap is roughly 300 missing frames, which is unambiguous, while
sitting well above realistic transient dropouts at range (1–3 s) and well below
an RTSP server's session timeout.

Making idleness primary also removes this design's only unmeasured dependency.
Whether an EOS *event* survives telemetry and video stopping together under
muxer backpressure is unknown; a watchdog on elapsed time since the last
telemetry sample does not care. If the branch stalls, the timeout fires; if EOS
arrives first, it fires sooner.

## Mode 1 — end and exit (default)

When the session ends, tear the pipeline down and finish writing the file or
live output through the normal `close()` drain. No flag; this replaces idling on
a dead pipeline, a behavior with no beneficiary.

`close()` is fallible, and its result decides the exit. A successful drain exits
0 with the output finalized; a drain or backend failure keeps the existing path
— the error propagates, the CLI reports it and exits 1, and libmisbklv may
discard the file it created (ADR 0022, no output unless the session succeeded).
Ending on an idle timeout changes *when* `close()` is called, never how its
failure is handled.

## Mode 2 — reconnect (opt-in)

Do everything mode 1 does *except* exit. Then poll the source on an interval —
default 3 seconds, configurable — indefinitely, until either:

- `SIGINT`/`SIGTERM` arrives, and the process exits 0; or
- the source accepts a connection, at which point a **new pipeline** is built
  and ingest resumes.

**A run also stops on its own when it is achieving nothing.** Consecutive
sessions that connect and then carry no telemetry are bounded by
`--max-empty-sessions` (default 5, `0` never stops), after which the run ends
with the last error. This is a deliberate narrowing of "only an interrupt ends
it", added because the failures a long unattended run actually meets — a full
disk, an output that broke mid-run — are reported by libmisbklv as a generic
`Backend` error, indistinguishable from a source dying. Without a bound they
retry forever, writing nothing, in precisely the scenario this mode exists to
serve.

The bound counts only sessions that **opened**. A source that is merely absent
never connects, is never counted, and is waited on indefinitely — the flying-day
case is untouched. A session that emits telemetry and then fails resets the
count, so a link dropping mid-sortie costs an attempt rather than the run.

Reconnect is a fresh `KlvSink` and a fresh `open_insert`, not a repair of the
old one. The previous session is already finalized before polling starts, so a
completed sortie is never held hostage to the next one.

**A session counts as successful once it emits one telemetry sample.** If it
ends having emitted none, its output file is deleted and the counter does not
advance. `open_insert` succeeding is not enough on its own: the file sink
creates its file as soon as the pipeline is built, so a source that accepts
connections and then stays silent would otherwise deposit one near-empty TS per
idle-timeout cycle, indefinitely and unattended. A live output has no file to
clean up, so the same rule costs nothing there.

Output naming on reconnect:

- **Live output** (`udp:`/`srt:`) reuses the same address. Downstream consumers
  see a gap and then a resumed stream.
- **File output** rolls: a `_000n` counter is appended to the base name before
  its extension. **Every session is numbered, including the first** — `-o
  flight.ts` produces `flight_0000.ts`, `flight_0001.ts`, `flight_0002.ts`. One
  file per sortie, each finalized when its sortie ends, and `flight_*.ts` globs
  the whole run. The path the operator typed is never itself a file in this
  mode; that surprise buys a consumer one pattern that catches everything.
- **Collisions skip forward.** The counter starts at the first index with no
  file on disk, so re-running in a directory that already holds a capture
  appends rather than destroys. This matters concretely: libmisbklv's file sink
  truncates an existing path when it opens, so reusing an index would lose the
  earlier capture. The cost is that a stale file from an aborted run leaves a
  gap, and an index therefore identifies a file rather than the nth sortie of a
  run.

Together these make unattended multi-sortie capture work without a supervising
script, and mode 1 alone makes the simpler shell form work as well:

```sh
for i in $(seq 1 5); do
  until parrot-to-klv rtsp://192.168.42.1/live -o "flight$i.ts"; do sleep 5; done
done
```

## Where the watchdog lives

Preferred: **in `live_ingest`**, measuring elapsed time since the last captured
Parrot RTP header extension. The invariant this rests on runs one way only:
telemetry rides inside the video stream's header extensions, so telemetry
arriving implies video arriving. The converse does not hold — video can flow
with no extensions at all, which is exactly the video-only gap below. Telemetry
idleness is therefore a sound proxy for a *Parrot* stream having stopped, not a
measure of video liveness. The drive loop already polls
`parrot_extension_pop_next()` on a 10 ms cadence. That makes the watchdog a
timestamp and a comparison in code this project already owns — no upstream
change, no pad probe, and no dependence on the EOS question above.

The gap is a video-only RTSP capture, which the current code supports ("For
RTSP with no metadata, no dummy is emitted — just close (video only)"). There,
telemetry-idle would fire while video is still flowing. Covering it needs
libmisbklv to expose time-since-last-video-delivery, which its `VideoCtx`
delivery probe already has the ingredient for — but that is a separate upstream
fork, and it should not block the telemetry case.

# Alternatives considered

- **EOS as the primary trigger** — rejected. It only fires on a polite
  teardown, so the failure modes an operator most cares about (power loss, hard
  link failure) would not end the run. It also depends on an unmeasured
  question about EOS delivery under muxer backpressure.
- **Keep idling after the source stops** — rejected; the pipeline is dead and
  cannot resume, so waiting costs an operator's attention and returns nothing.
- **Make end-on-idle opt-in behind a flag** — rejected as the default. A flag
  whose off position selects "hang on a dead pipeline" is not a choice worth
  offering. `--idle-timeout 0` already provides that escape hatch for anyone
  who wants it, without a second flag.
- **Per-mode default timeouts** — rejected as unnecessary. A false positive
  costs the whole capture in mode 1 but only a file split in mode 2, so the
  ideal values differ; one default and one flag is enough, and operators who
  care will tune it.
- **Reconnect inside libmisbklv** — rejected; re-establishing an RTSP session
  under a live muxer means a new request pad and PMT continuity questions
  inside the backend. Building a new pipeline one level up is a much smaller
  change and keeps the backend's session model intact.
- **Reuse one file across sorties in reconnect mode** — rejected; a single TS
  spanning a day of flights with timeline discontinuities at each join is worse
  to work with than one finalized file per sortie, and the roll counter costs
  nothing.
- **Keeping the operator's exact filename for the first session** — rejected;
  it would make `flight_*.ts` miss the first file, and a consumer that has to
  know about the special case is worse than a name that is uniformly derived.
- **Failing or overwriting on a filename collision** — both rejected. Failing
  turns an unattended restart into a manual intervention, which is the opposite
  of what this mode is for; overwriting silently destroys flight data that
  cannot be recaptured.
- **Counting a reconnect as successful when `open_insert` returns ok** —
  rejected; see the churn it produces, above.
- **Rely on the RTSP server's session timeout** — rejected; it is the server's
  policy, not ours, typically about a minute, and absent entirely when the peer
  vanishes.

# Consequences

- Unattended capture terminates on its own, so a systemd unit or a batch script
  gets a meaningful exit instead of a hang.
- Mode 1 changes existing CLI behavior for live input. Pre-1.0, and the
  behavior it replaces is a hang, but it is a visible change and belongs in
  release notes.
- A long enough transient dropout ends a session. In mode 1 that truncates the
  capture; in mode 2 it costs a file split and a few seconds. `--idle-timeout`
  is the tuning knob, and `0` disables the behavior outright.
- Reconnect mode does not exit on its own while it is working — that is the
  point, and it must be supervised for shutdown. It does stop once
  `--max-empty-sessions` consecutive connected sessions carry no telemetry,
  which is the difference between waiting for an aircraft and spinning on a
  broken output.
- File output in reconnect mode produces a variable number of files whose names
  depend on how often the source dropped. Anything consuming them must glob
  rather than expect one path.
- A video-only RTSP capture is not covered by the telemetry watchdog until the
  upstream video-delivery signal exists. Until then such a run behaves as it
  does today.

# Assumptions / open questions

- **Video-only captures are a recorded gap, not a blocker.** The watchdog
  measures telemetry, so a stream carrying video without Parrot header
  extensions keeps today's run-until-`SIGINT` behavior. This was weighed and
  accepted: the video-only cases in the suite are `pipeline:videotestsrc`
  sources with no telemetry by construction, behind
  `PARROT_TEST_ALLOW_PIPELINE`, and a real Parrot RTSP stream always carries
  telemetry. Genuine production video-only means a non-Parrot camera or a
  failed extmap negotiation. Closing the gap needs libmisbklv to expose
  time-since-last-video-delivery, which its `VideoCtx` delivery probe has the
  ingredient for — a separate upstream fork, to open when a real case appears.
- **The empty-session bound is the settled answer, not a placeholder.** Once a
  session opens, libmisbklv reports a dead source and a dead sink alike as
  `Backend`, so the bound infers from "connected but produced nothing" what a
  library-side classification would state outright. Making the library
  distinguish them was proposed and **declined**
  ([libmisbklv#63](https://github.com/nitsuga/libmisbklv/issues/63), closed as
  not planned): attribution does not decompose cleanly — a mux error caused by
  a blocked sink originates at the mux, and a stalled sink surfaces as
  backpressure almost anywhere — so it would bury occasional wrong judgments
  inside the library where a consumer could neither see nor override them. The
  bound is approximate in the open, tunable by the operator, and bounded in its
  failure: a flaky link costs at most `max_empty_sessions` attempts. Reopen only
  for a consumer that cannot tolerate that cost, or if GStreamer offers reliable
  sink-subtree attribution.
- **Startup semantics are settled**, not open: the timer arms at drive-loop
  entry, so a source that connects and never delivers ends on the same timeout
  as one that stops. Recorded here because the reverse choice — arming at the
  first sample — silently preserves the hang this decision removes, and is an
  easy thing to reintroduce.
- **Interaction with existing tests.** A `pipeline:` run that injects one
  telemetry sample and then goes quiet would now end after the idle timeout
  rather than running to its deadline. Current indefinite tests interrupt well
  inside 10 s, so none is expected to change, but this should be confirmed
  rather than assumed when the code lands.

# Citations

[^parrot-to-klv-94]: [parrot-to-klv#94](https://github.com/nitsuga/parrot-to-klv/issues/94)
    — the live-ingest failure report whose fix established the polling loop this
    builds on.
[^libmisbklv-adr-0035]: [libmisbklv ADR 0035](https://github.com/nitsuga/libmisbklv/blob/main/context/decisions/0035-live-insert-terminal-status.md)
    — the accepted failure-only `poll()` contract, and its record that clean EOS
    is unobservable before `close()`.
[^libmisbklv-59]: [libmisbklv#59](https://github.com/nitsuga/libmisbklv/pull/59)
    — the review thread carrying the `gst-launch-1.0` traces behind the EOS
    delivery and muxer-backpressure findings.
