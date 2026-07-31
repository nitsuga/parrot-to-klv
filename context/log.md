# Knowledge Bundle Log

<!-- Durable chronological record, newest first, ISO YYYY-MM-DD day headings.
     One thin line per decision (chronology + link — the ADR owns the rationale);
     a fuller entry per significant implementation. Routine ingests/lint go here
     too. This is the ONLY home for history — PROGRESS.md is present-tense only. -->

## 2026-07-31

* **Added a dedicated `parrot-sanitize` CI contract for the project-owned,
  GStreamer-free core.** ASan+UBSan cover the parser, normalization,
  transform, scrub, and conformance subset under an explicit label; the
  GStreamer mux tests remain in the ordinary suite and fetched libmisbklv stays
  outside client instrumentation. The selected subset was verified locally, and
  the workflow applies the same boundary to pushes and pull requests.

* **Transform rejection now preserves measurement truthfulness.** Non-finite
  source values are omitted as invalid, finite values a codec cannot represent
  are reported as not encodable, and Item 56 is omitted rather than saturated
  above 255 m/s. Airspeed and satellite count likewise no longer saturate
  malformed values. Horizontal velocity components remain independently usable;
  course at rest is reported as undefined, and Item 2 retains its existing
  handling.

* **libmisbklv is pinned to hardening revision
  `1732e0c986e4b252a030147c389585da8248ddb2`.** The client remains compatible
  with that revision's API, and the project test suite remains green. Its error
  rendering now gives the new `TypeMismatch` and `ResourceLimit` cases useful
  names, with direct tests, so failures do not fall through to the generic
  unknown-error text.

* **Project-owned targets now compile with a portable fatal-warning baseline.**
  The baseline is deliberately scoped to this project's code and tests: fetched
  libmisbklv and vendored dependencies are not made this project's warning
  liability. This established a clean, portable gate for dependency integration.

* **Muxing and scrubbing now reject input/output aliases by file identity before
  touching output.** This applies the existing no-in-place-scrub policy to
  relative paths, symlinks, and hard links rather than trusting path spelling.
  For muxing it closes a confirmed data-loss path: opening an aliased output
  truncated the still-needed source to zero bytes. Scrubbing reads its source
  into memory before opening output, so its alias case was an unintended
  in-place rewrite with partial-write risk rather than that read/truncate race.
  The respective behavior and the separate pre-existing-output limitation are
  recorded in [[mux-stage]] and [[mp4-extraction]].

## 2026-07-30

* **Backported the planning-hygiene hardening from `okf-project-template`.** New
  rule: "Now" is where the *work* is, not a feature inventory — if a sentence
  would still be true after a month of no work it is durable knowledge and
  belongs in a `context/` concept or an ADR. The rule is in `CLAUDE.md`, its
  reasoning in [`workflow-rationale.md`](./workflow-rationale.md), which now also
  marks a rule *observed* where a real incident produced it and asks future
  sessions to append their own. This repo's own "Now" does not yet comply —
  recorded under PROGRESS § Known gaps.
* **Emptied PROGRESS "Now" of its feature inventory** (~75 lines → ~20), per the
  rule adopted earlier today. It was pure deletion, as predicted: every claim was
  already owned by [`mp4-extraction`](./mp4-extraction.md),
  [`transform-stage`](./transform-stage.md), [`mux-stage`](./mux-stage.md), an
  ADR, or — for the tools' user-facing surface — `README.md`. "Now" keeps the
  phase state, the current release, and the one open piece of work.
* **The linking boundary is now structural, and wikilinks are checked.**
  [`CONVENTIONS.md`](./CONVENTIONS.md) § Linking used to pick a form by "whatever
  a person is likely to read on GitHub" — a judgment call remade at every link,
  so the boundary drifts. It is now decided by where the target is: inside
  `context/` → `[[slug]]`, outside → relative markdown. The CI learned to resolve
  `[[slug]]` against concept names, which puts **113 wikilinks under check that
  nothing had ever verified** (all resolve). Both forms are now covered; neither
  is the unchecked one.
* **Fixed the link-check CI's blind spot.** It silently remapped root-absolute
  links onto `context/` before checking existence, so the one form
  [`CONVENTIONS.md`](./CONVENTIONS.md) § Linking bans was the one form it
  repaired. It now flags them. (No occurrences here — this closes the hole that
  let ~68 accumulate in libmisbklv under green CI.)
* **Tightened two conventions** ([`CONVENTIONS.md`](./CONVENTIONS.md)): citations
  are anchored by section/message name, never by line number of a file under
  `../references/` (a positional citation silently repoints when a source is
  re-fetched); and a fork gets a *second* log line only across a genuine session
  gap, not for a same-session propose→accept.

## 2026-07-28

* **Hardened the release-notes publish path, and simplified it.** Review found
  the notes could be silently dropped on two paths that both report success.
  `v0.1.2` took neither — it published correct notes on the happy path — so
  this is the failure modes *around* a run that worked, fixed after the fact
  and landing in the next tag rather than that one.
  - **A section heading with no body yet extracted as a lone newline, which
    `-s` calls content.** The publish would then set an *empty* release body
    instead of falling back to generated notes. The test is now "has a non-blank
    line" (`grep -q '[^[:space:]]'`).
  - **The `gh release create || gh release upload --clobber` fallback set no
    notes at all.** That path is reached when the release already exists — a
    re-run after a partial failure, or a hand-made release — so the job would
    re-upload assets, exit 0, and leave the body untouched. It now runs
    `gh release edit --notes-file` on that path. `gh release edit` has no
    `--generate-notes`, so the fallback can only restore hand-written notes;
    that is exactly the case that was being lost.
  - Dropped the `have_notes` step output. `notes.md` is in the workspace and
    both steps can test it directly, so the `$GITHUB_OUTPUT` write and `env:`
    mapping were carrying one bit the filesystem already held — and were a
    second source of truth free to disagree with the file.
  - Extraction re-verified against every heading, a nonexistent tag, and a
    whitespace-only section; the last is the case the old test got wrong.

* **`v0.1.2` published.** Tagged at `4c0d804`; both deliverables and their
  checksums attached, after the bundle converted a fixture inside a bare
  `ubuntu:24.04` container as usual.
  - **The changelog-as-release-notes mechanism worked on its first real run.**
    The release page carries the hand-written `## v0.1.2` section, not commit
    subjects — which for this release was the point, since the subjects carry a
    change and its revert.
  - Carries the Item 11 inference, the CLI hardening, and the spelling
    normalization. The libmisbklv pin moved only across a revert, so the
    library source is identical to what `v0.1.1` shipped.

* **CI actions moved to their current majors** — `checkout@v5→v7`,
  `upload-artifact@v4→v7`, `download-artifact@v4→v8`, across all three
  workflows.
  - **v4 is not deprecated; its *runtime* is.** The deprecation warnings come
    from node20, which the v4 line runs on; v5 onward moved to node24. Worth
    recording because "bump the action" and "the action is deprecated" are
    different claims and only the first one was true here.
  - The current majors need an Actions runner >= 2.327.1. GitHub-hosted runners
    are well past it; noted in `release.yml` for the day this meets a
    self-hosted one. `download-artifact@v8` also makes a hash mismatch an error
    by default, which is a guarantee gained, not a risk taken.
  - Versions were read from the API rather than assumed — v5 was two to three
    majors stale by the time we looked.

* **Declared `0.1.2`, and gave the release page real notes.** `CHANGELOG.md` is
  hand-written and `release.yml` publishes the section matching the tag; the
  `--generate-notes` fallback stays for a tag with no section, because weaker
  notes beat a failed publish.
  - **The commit list was actively misleading for this release.** It contains a
    change *and its revert*, so a user would have read "video is stream 0:0" as
    a headline of a release where it is not true. Raw subjects are our churn,
    not their changes.
  - The `v0.1.2` section leads with `--infer-image-source`, states the CLI
    behavior change as the thing to check if you script the tool, and names the
    KLV-at-`0:0` stream order under *Known limitations* — the trap most likely
    to cost a user an afternoon.
  - Extraction verified locally against every heading plus a nonexistent tag: a
    failing `gh release create` is a bad place to discover an awk bug.

* **CLI hardening: unknown options are refused, and `--version` / `--help`
  exist.** Small, but the first two are what a user meets in the first minute.
  - **A mistyped flag used to be silently dropped.** `--klvv` ran to completion
    and exited 0 having quietly not printed the coverage report — measured, not
    supposed. `-o` with no path became the *input* filename. Both are errors
    now, naming the offending argument, as is a second input file.
  - `--help` was treated as a filename and printed `Failed to open file:
    --help` — the first thing a new user would try.
  - `--version` reads `project(VERSION)` through a compile definition.
    Declarative like the rest: artifacts are named from the git tag, and the
    release commit moves both together.
  - Guarded by five one-line `ctest` entries rather than a test binary, because
    the thing under test is the process's exit status. `WILL_FAIL` on the three
    rejection cases; all labelled `parrot` so CI runs them.

* **Spelling normalized to American English repo-wide** — 103 substitutions
  across 40 files, and the rule written into `CLAUDE.md` § Project preferences so
  it holds rather than being re-litigated.
  - **The reason is agreement, not correctness.** The standards this project
    implements are American-spelled — ST 0601's own item name is "Frame
    **Center**" — so a doc reading "frame centre" next to a quoted "Frame Center"
    disagrees with itself on one page.
  - Covers prose wherever it appears: comments, doc strings, CLI help and error
    text (`Unrecognized Parrot MIME type`), test messages, Markdown, and the two
    workflow step names (`Authorize the private dependency`, renamed in
    `build.yml`/`release.yml` and in the `README-ci.md` line that quotes it).
  - One identifier moved with the prose: `altitude_or_unknown(double metres)` →
    `meters`, a file-local parameter.
  - **`references/` and `external/` deliberately untouched** — immutable input
    and vendored code. The pass ran over `git ls-files` minus those two, so the
    exclusion is structural rather than remembered.

* **Re-pinned to libmisbklv `e33f2fb`, which reverts the stream-order change
  below — it was dropping our KLV stream.** The PMT is back to KLV at `0:0`,
  video at `0:1`: cosmetically wrong, and now a documented limitation rather than
  something to fix. Upstream's reasoning, both failed approaches and the
  measurements: libmisbklv ADR 0020 § Stream order.
  - **Found here, not upstream.** `mux_test` began failing about 1 run in 3 after
    the bump. The output was a playable TS with the video intact and **no KLV at
    all** — exit 0, no warning. Deferring upstream's appsrc link fixed the pad
    *order* but left the pad *set* racy against preroll.
  - **A/B'd against the old pin to place the blame before acting**: 4/60 failures
    under load on `4ced331`, 0/52 on `8e866e1`. Upstream's own
    `gst_video_insert_test` caught it too, at 2/20 — it had the right assertion
    all along and still shipped, because a single green run of a load-dependent
    race is not evidence.
  - Upstream also proved a **second** regression the same change caused, in ST
    0604 SEI timing, which a fix for the race did not touch. That is what settled
    reverting over repairing.
  - **`mux_test` still asserts the order** — now pinning KLV-first as the known
    state. The point is unchanged: this is a user-visible property that has
    broken the output once, so it should never move silently again.
  - Suites green on the new pin: 9/9 `-L parrot`, 34/34 bare, and 0/30 under the
    load that used to reproduce the failure.
  - **The shipped usage doc now warns that `0:0` is the KLV**, as does the
    README — the limitation is upstream's, but the surprise lands on whoever
    runs `ffmpeg -map 0:0` against our output, and `packaging/usage-common.md`
    goes inside the release bundle.

* **libmisbklv re-pinned to `4ced331` — the output's PMT listed the KLV before
  the video.** `mpegtsmux` assigns PMT order by sink-pad *request* order, not
  bin-add order, and upstream linked the KLV appsrc before the video demuxer's
  pad appeared, so the KLV always claimed `sink_0`. Upstream now defers the
  appsrc link until after the video-pad wait.
  - **This was our output, not a theoretical upstream bug.** Every `-o`
    conversion this project has ever produced announced the metadata stream at
    0:0. A consumer addressing streams positionally — `-map 0:0`, "stream 0 is
    the video" — got the KLV.
  - Confirmed by converting all four video fixtures before and after: v3, proto,
    the audio-bearing one and the H.265 one all flip from `0=klv, 1=video` to
    `0=video, 1=klv`.
  - **`mux_test` now asserts the order**, not just the membership — the property
    was load-bearing and held only by upstream's behavior. The old pin's output
    is on record as failing it (ffprobe read `stream 0 = klv` from the before
    files), though the assertion was written after the bump rather than run
    against the old build.
  - Suites green: 9/9 `-L parrot`, 34/34 bare.

* **Item 11 can now be inferred for v3, behind `--infer-image-source`** —
  [ADR 0011](./decisions/0011-inferred-image-source-sensor.md), fork 11. v3
  states no imaging spectrum, so every ANAFI-range conversion reported Item 11 as
  `format does not carry it`; the flag guesses `IR` / `EO` from whether the frame
  carried the thermal extension. Off by default, never overrides a stated
  spectrum, rendered `EO (inferred)` in the coverage report.
  - **Raised by a user converting real ANAFI USA footage**, who needed Item 11
    for a downstream consumer and asked whether its absence was an oversight. It
    was not — the field genuinely is not in v3 — which is why this ships as an
    inference with the failure modes named rather than as a mapping.
  - **The subtype is not guessed alongside the stem.** No v3 field distinguishes
    wide from tele, so the value is a bare `EO`, never `EO Wide`. A guessed
    qualifier would add nothing while making the guess look more precise.
  - **`(inferred)` is a report label, not a wire suffix.** Item 11 is a name that
    consumers string-match; a caveat inside it would break the match. Same
    treatment `(clamped)` already gets, and asserted in `transform_test`.
  - **`to_0601` / `mux_to_ts` now take `TransformOptions`** instead of a trailing
    `bool clamp_attitude`. Two adjacent unnamed bools at a call site is a defect
    waiting to happen; the third would have been worse. Call sites read
    `{.clamp_attitude = true}`.
  - Suites green: 9/9 `-L parrot`.
  - Doc lint caught in passing: [[transform-stage]] still described
    `--force-emit-attitude` as a live flag, two ADRs after
    [[0008-force-emit-attitude-cannot-work]] removed it.

* **`v0.1.1` published.** Both deliverables and their checksums attached, built
  and smoke-tested by the four-job `release.yml` before anything reached the
  release page. Carries the item-naming report, the container image (ADR 0010)
  and the libmisbklv CI fix; `v0.1.0` had none of them.

* **`release.yml` exercised by `workflow_dispatch` before the v0.1.1 tag.** It
  gained a fourth job (`smoke-image`) with ADR 0010 and had only ever run in its
  three-job form, so the first tag using it would have been the first test of it.
  All three build/smoke jobs green, `publish` skipped as designed — which is what
  makes a manual run safe to use this way.
  - `smoke-image`'s `--user` check passed on its first real run: the converted
    output in the container deliverable comes back owned by the caller, not root.
  - Also the first CI to build against the new pin. `Build and test` green on
    24.04 / gstreamer 1.24 — the stack where the hang and the surviving-SEI fault
    both lived, and the only one that could judge the fix.

* **Doc lint after the re-pin — three descriptions of upstream's pipeline had
  gone stale.** All from the mechanism moving underneath them, not from age.
  - **[[mux-stage]] still said `filesrc ! parsebin`**, two pins after it stopped
    being that. Now `filesrc ! demuxer ! parser`, with the point that matters
    from this side stated: no decoder need be *installed*, which is what lets the
    bundle ship without one.
  - **"The source's other tracks are dropped" was true but hid a trap.** Dropped
    means each stream gets its own queue to a fakesink, and the queue is
    load-bearing — remove it and a discarded stream deadlocks preroll. It reads
    as tidiness from here, so it is written down here; libmisbklv's ADR 0020 owns
    the decision.
  - **[[0001-mp4-extraction-approach]]'s follow-on named parsebin too.**
    Annotated rather than rewritten, beside the existing ST 0604 note — nothing
    the ADR decides turns on which chain it is, but the sentence should not read
    as current.
  - **[[transform-stage]] under-described the coverage report** after the naming
    change: "with its value" for something that now prints number, name, value
    and units, one item per line, all sourced from libmisbklv's registry.
  - Register, ROADMAP forks, bundle plugin counts and sizes, and every internal
    link all checked clean.

* **Re-pinned to libmisbklv `8e866e1` — the CI hang is fixed, and 1.24 was
  silently keeping the source's ST 0604.** Two faults upstream, both invisible on
  the 1.20 stack this machine runs, which is why only CI ever saw the first and
  nothing saw the second.
  - **The hang**: a demuxer pushes every stream from one thread, and a sink
    prerolling in PAUSED blocks that thread until PLAYING. The fakesink taking a
    dropped stream therefore held the video behind it, the muxer never prerolled,
    and the pipeline never reached PLAYING. Each dropped branch now gets its own
    `queue` (leaky=downstream). Their ADR 0020 records the queue as load-bearing
    rather than tidiness, so a later simplification does not reintroduce it.
  - **The silent one**: gstreamer 1.22 added a parsed payload type for
    `user_data_unregistered`. Before that it arrived unhandled, which is all
    their replacement check knew, so on 1.24 the source's SEIs survived beside
    ours — the duplication their ADR 0024 exists to prevent, failing quietly.
    Both forms are handled now, guarded by `GST_CHECK_VERSION`.
  - **Behaviorally a no-op here**, as the last two pin moves were: on gstreamer
    1.20 neither fault reproduces. Suites green, 9/9 `-L parrot` and 34/34 bare
    (25 of those libmisbklv's own, matching what upstream reports under 1.24.2).
    CI's 1.24 is the stack that judges this one — the transitive-include lesson
    again.
  - Upstream also gained `tools/gst-container`, which runs its suite against
    another gstreamer; building the CI environment in docker is what finally
    reproduced the hang locally. Nothing to adopt here, but it is where a
    version-specific failure gets diagnosed next time.

* **The `--klv` coverage report names items instead of numbering them.** It read
  `2=1538842372000000 … item 11 — format does not carry it`; it now reads
  `Item   2  Precision Time Stamp  = 1538842372000000 microseconds` and
  `Item  11  Image Source Sensor   — format does not carry it`. Names and units
  come from libmisbklv's ST 0601 registry, so the report stays in step with the
  standard rather than carrying a second copy of it.
  - **The number stays alongside the name.** "Item 21" is how ST 0601 indexes it
    and how this project's own docs refer to it, so replacing the number would
    have cost the cross-reference the report is usually read against.
  - Emitted items moved to one per line: with names attached the old single line
    ran past 400 characters. The name column is sized to the widest name present,
    so a short report stays narrow.
  - Units are appended only to a bare number — a value already qualified
    (`out-of-range (0x8000)`, `20.000 (clamped)`) would otherwise read
    "(clamped) deg", and "withheld deg" says nothing "withheld" did not. Asserted
    in `transform_test` rather than left to reasoning.
  - Two tests asserted the old wire format (`13=withheld`, `4=withheld`). They now
    find the item's line and check it says withheld, which is the actual
    requirement and does not break the next time a column moves.
  - Conformance reports do not embed this text, so nothing committed changed.

* **First `v0.1.0` tag failed to build; the 22.04 runner needed a package
  untangled.** `libgstreamer1.0-dev` depends on `libunwind-dev`, and the
  `ubuntu-22.04` image ships LLVM's `libunwind-*-dev`, which conflicts with it —
  apt reports "held broken packages" and names only the dependency, never the
  conflict. Removed the LLVM copy and named `libunwind-dev` explicitly. Specific
  to pinning the build to the oldest target: the 24.04 image the test workflow
  uses does not have it, so `Build and test` was green throughout.
  - The tag was moved rather than bumped. `publish` is gated behind `smoke` and
    both were skipped, so nothing had been distributed and `v0.1.0` still means
    what it will mean.
* **Fork 9 decided — ship a relocatable bundle.**
  [ADR 0009](./decisions/0009-release-bundle.md), accepted and implemented.
  `packaging/make-bundle.sh` + `.github/workflows/release.yml`.
  - The tool plus private copies of GStreamer and the eight LGPL plugins it uses:
    16 MB, ~7 MB compressed, extract and run, no root, nothing installed.
    Verified from a scrubbed environment — only `libc` and `libm` resolve outside
    the bundle.
  - Built on **22.04, deliberately not `ubuntu-latest`**: glibc compatibility
    runs forward only, so the oldest target is the only correct build host. CI
    checks the binary imports nothing above `GLIBC_2.35` rather than trusting the
    runner label. `-static-libstdc++` removes the host C++ runtime from the
    surface entirely, which is safe here because nothing else in the process is
    C++ — every bundled library and plugin is C.
  - **The smoke job is the point of the workflow**: it converts a fixture inside a
    bare `ubuntu:24.04` container and asserts the output carries KLV and ST 0604.
    It first checks the image really has no GStreamer, so the test cannot quietly
    stop testing anything.
  - Rejected: Docker at runtime (the constraint), static GStreamer (`gst-full` is
    a fragile build to own, and LGPL relinking gets hard), AppImage (type-2 needs
    FUSE2 and 22.04/24.04 ship FUSE3 — it would require the one `apt install` we
    were told not to assume).
  - **Packaging found a real defect**, recorded in full under the re-pin entry
    below: parsebin needed a decoder in the registry to expose a parsed stream,
    and the decoders that satisfied it were the encumbered ones. Fixed upstream
    rather than papered over. Restricting a dependency set is a good way to find
    out what you actually depend on.

* **Doc lint before a break in the work.** Four drifts, all from the last two
  changes rather than age:
  - **README never mentioned that releases exist.** `v0.1.0` has been published
    with downloadable deliverables and the front page still said "build it
    yourself". Added a *Getting it* section naming both, which is recommended,
    and pointing at the ADRs for why they are shaped that way.
  - README's structure block and CI section predated `packaging/` and
    `release.yml`; both now listed.
  - ROADMAP's Phase 4 named only the bundle. It now names both deliverables and
    says which is the default.
  - PROGRESS said "nothing mid-flight" while an untagged v0.1.1 sat on `main`
    with two unreleased changes, and its "Next" listed only fork 4. Both
    corrected — the immediate actions are the libmisbklv re-pin and the tag.
  - Checked every internal link resolves. The two hits are the illustrative
    `./concept.md` in CONVENTIONS § Linking, present since the initial commit.

* **Fork 10 decided — a container image as a second deliverable.**
  [ADR 0010](./decisions/0010-container-deliverable.md), accepted and
  implemented. `packaging/{Dockerfile,Dockerfile.debug,make-image.sh,parrot-to-klv}`.
  - Additive, not a replacement: the tarball stays the default.
    [ADR 0009](./decisions/0009-release-bundle.md) rejected Docker as the *only*
    deliverable and that still holds; the audience widened rather than the
    decision reversing, and 0009 is annotated to say so.
  - **Built FROM the bundle**, so the two deliverables cannot describe different
    binaries. One build, two packagings.
  - **distroless, measured rather than assumed**: `docker save | gzip -9` is
    13.8 MB against `ubuntu:22.04`'s 34.2 MB. Base size matters more here than
    usual — layer sharing normally amortizes it, and `docker save` defeats that,
    so every user downloads the full base every time. The cost is no shell in the
    image; `Dockerfile.debug` is the same contents on a base that has one.
  - **The launcher takes arguments first and prompts only as a fallback**, and
    only when stdin *and* stdout are terminals — so one interface serves a person
    and a cron job, and a missing argument produces usage and a non-zero exit
    rather than a prompt nobody will answer.
  - Three things easy to get wrong, all handled and all verified: `--user`
    (without it every output file lands owned by root and the user cannot delete
    their own conversion); mounting the file's *directory* read-only rather than
    a tree, collapsing to one read-write mount when input and output share a
    directory; and printing the **host** path on success, because the tool
    reports the container's view (`/work/flight.ts`), which means nothing to
    whoever typed a host path.
  - Two UX flaws found by running it rather than reading it: the output prompt
    offered a default in brackets *and* treated blank as "no output", which
    contradicts what brackets mean everywhere else — now two questions; and
    `set -e` would have swallowed the exit status before it could be reported.
  - CI gains `smoke-image`, which asserts something the bundle's smoke test
    cannot: **the output is owned by the invoking user**. That is the regression
    test for `--user`; without it the flag could be dropped and nothing would
    fail.
* **The bundle is down to six plugins from eight.** `playback` was there only to
  provide parsebin, and `audioparsers` only because parsebin insisted on parsing
  an audio pad the muxer then dropped. Both fell out when libmisbklv stopped
  using parsebin (its ADR 0025). Re-derived by removing each and re-converting
  every video fixture, not by reading the pipeline — 15 MB, 5.8 MB compressed.
  ADR 0009's counts and sizes corrected in place.
* **Both deliverables now ship a `USAGE.md`**, assembled from a shared body plus
  a per-deliverable invocation section, so the substantive half cannot drift
  between the tarball and the image.

* **Re-pinned to libmisbklv `8766e56` — video passthrough no longer needs a
  decoder installed.** Their fork 23 / ADR 0025, written and landed from here
  while packaging the release bundle (fork 9, in progress).
  - **What it fixes for us.** `parsebin` decided a stream was fully parsed by
    asking whether any *decoder* in the registry accepted its caps — never
    instantiated, merely present. Ordinary systems always have one, so nothing
    showed. A bundle carrying only the plugins we use has none, and conversion
    failed with "missing parser" for a stream `h264parse` had already parsed.
  - The decoders that satisfied it were exactly the ones we cannot ship:
    `openh264` (H.264 patents), `libde265` and `faad2` (GPL), in an Apache-2.0
    distribution, to sit in a registry and never run. So the fix went upstream:
    the chain is now `filesrc ! demuxer` with the container sniffed up front.
  - **Verified here**: all three video fixtures convert against **eight LGPL
    plugins with no decoder present**, from a scrubbed environment. Suites green,
    9/9 `-L parrot` and 34/34 bare.
  - Noted in passing: `mpegtsmux` output is **not byte-reproducible** — the same
    binary run twice produces files of identical length differing mid-stream. Our
    conformance reports cover metadata decode, not muxed bytes, so nothing rests
    on it; worth knowing before anyone writes a test that diffs a `.ts`.

* **CI had been red since the drive-by pin bump; fixed.** `Build and test` failed
  at *configure* on every push from `0ca3990` (2026-07-28 01:04) onward — the
  merge that carried `25199e4` and its unannounced pin to `b454415`. That pin
  brought libmisbklv's ST 0604 SEI generation, which parses H.264 NALs and so
  needs `gstreamer-codecparsers-1.0`; the workflow installed the *runtime*
  `gstreamer1.0-plugins-bad` but not `libgstreamer-plugins-bad1.0-dev`, which is
  what ships the `.pc` file. Added it.
  - **Nothing compiled for two days**, so no test ran in CI across the whole ST
    0604 / fork 7 / fork 8 sequence. Every "suite green" in the entries below is
    a local result only. `link-check` stayed green throughout, which is why the
    repo did not look broken at a glance.
  - **The diagnostic was the reason it took a log dig.** libmisbklv treats its
    gstreamer backend as optional: it prints one status line and carries on
    without defining `misbklv::gst`, so the failure surfaced as a bare
    `target "misbklv::gst" not found` against `parrot_mux`, naming neither
    gstreamer nor a package. `CMakeLists.txt` now checks for the target straight
    after `FetchContent_MakeAvailable` and fails with the apt line. Verified by
    configuring with `pkg-config` stubbed out to fail.
  - Fifth distinct problem traced to that one drive-by bump, after the wrong hash
    in the log, the silent end of video passthrough, the H.265 regression, and
    the ST 0604 posture shift. The rule it broke — a pin bump is its own commit —
    exists precisely so someone reads what the pin contains.
* **Fork 8 decided — `--force-emit-attitude` removed.**
  [ADR 0008](./decisions/0008-force-emit-attitude-cannot-work.md), accepted and
  implemented the same day. `encode_out_of_spec_angle`, the `force_emit`
  parameter through `to_0601()` and `mux_to_ts()`, the mutual-exclusion check and
  its CTest all go with it; `to_0601()` is back to one boolean.
  - **The flag errors rather than being ignored.** An unrecognized argument is
    silently dropped by this CLI, and this one used to change output, so passing
    it now explains what it actually did and points at Items 90/91. A script
    still carrying it gets told instead of quietly getting the default.
  - `put_narrow_angle` keeps a comment saying why there is no third branch, so
    the next person to think "we should just emit the real angle" finds the
    reason rather than the idea.
  - [[0005-attitude-range-policy]] revised, not rewritten: the reasoning that led
    to the flag was sound and the error was believing the encoding had room.
* **The attitude flags are tested — and the tests found that one of them cannot
  work.** Closes fork 5's standing gap ("no test covers either flag") and opens
  **fork 8**, [ADR 0008](./decisions/0008-force-emit-attitude-cannot-work.md)
  (`proposed`): remove `--force-emit-attitude`.
  - New in `transform_test`: `check_attitude_clamped` (both signs, both items,
    and Items 90/91 still true), `check_attitude_in_range_is_untouched` (neither
    flag may alter a value that was always emittable), and
    `check_attitude_force_emitted`. Mutual exclusion is argument parsing rather
    than transform, so it is a CTest — `attitude_flags_exclusive`, running the
    CLI on a fixture it would otherwise summarize cleanly, with `WILL_FAIL`.
  - **What the first run said:** `Item 6 carries the true pitch when
    force-emitted — expected 45 ±0.01, got 20`. Force-emit produces exactly the
    clamped value, byte for byte, and always: ST 0601 spends the entire int16
    range on ±limit, `encode_out_of_spec_angle` uses that same scale, and it only
    ever runs when the angle is already past the limit. There is no input it
    extrapolates.
  - Two claims in [[0005-attitude-range-policy]] corrected rather than quietly
    edited: saturation was said to begin "beyond roughly twice the spec limit",
    which is wrong in the flattering direction, and the assumption noting that
    ceiling was "derived from the encoding, not measured" — which is exactly how
    it went wrong. Deriving from a read of the code invented a working range that
    does not exist.
  - The failing expectation is now pinned to the real behavior with a pointer to
    fork 8, so the defect is asserted rather than merely known.

* **Fork 7 decided — ask for ST 0604 only on H.264, carry anything else through
  unstamped.** [ADR 0007](./decisions/0007-h265-vs-st0604-generation.md),
  accepted and implemented the same day.
  - **The regression it fixes was real and reproducible.** With `Generate`
    unconditional, the new HEVC fixture fails: `Sei0604::Generate needs H.264
    video; source carries video/x-h265` → `could not mux frame 0: media pipeline
    failed`. Note that lands on the *first emit* as `Error::Backend`, not at open
    as `Error::Unsupported` — the ADR's first reading of the upstream code was
    wrong on that detail and has been corrected from the observed behavior.
  - `mux_to_ts()` reads the video track's sample entry (`scan_mp4_tracks`, on the
    handle already open for session metadata) and asks for `Generate` only on
    `avc1`/`avc3`. Anything unrecognized takes the `Preserve` path too: the test
    is "do we know this is H.264", not "do we know it is not".
  - `MuxReport` gained `st0604_generated` and `video_sample_entry`, and the CLI
    prints which path ran. A conditional guarantee that announces itself is
    tolerable; a silent one is not.
  - **New fixture `parrot_v3_video_h265.mp4`** — `hvc1`/`hvcC`, same 64x64
    all-IDR shape as the H.264 pair, libx265 needed only to regenerate. Building
    `hvcC` surfaced a trap worth recording: the 12 profile_tier_level bytes must
    be copied from the *unescaped* SPS, since x265's constraint-flag zero run
    trips emulation prevention and a verbatim copy silently shifts every field
    after it. A guard caught it rather than it shipping as a subtly wrong config.
  - **`mux_test` now asserts both branches** — and property 4's video-PID guard
    was changed to fail rather than skip when no video stream is found. As
    written it would have passed silently on exactly this regression, which is
    the second time this week a vacuous guard has been the reason something
    stayed invisible.
  - Docs corrected: `mux-stage`, PROGRESS, `mux.hpp`. The claim that an ANAFI Ai
    records H.265 is gone — the airframes in scope record H.264, so the non-H.264
    path is defensive, not load-bearing.
  - Punted deliberately: H.265 ST 0604 generation upstream, which has its own ADR
    there. This decision stays correct if that ever lands.
* **CONVENTIONS § Linking now documents what the bundle actually does.** It had
  recommended root-absolute `[Title](/concept.md)` first — a form that resolves
  against the site root, so it works nowhere — and the only instance of it in the
  whole bundle was that recommendation. It also never mentioned `[[slug]]`
  wikilinks, which are the dominant form here (86 uses against 40 markdown
  links). Rewritten by audience: wikilinks for concept→concept inside the
  bundle, sibling-relative markdown where a person will click it on GitHub or
  the target is outside `context/`, root-absolute never.

* **Lint (drift sweep).** Two stale present-tense claims after the opt-in
  change: README described ST 0604 as something "libmisbklv generates" (it now
  generates it because we ask — its default leaves the video alone), in both the
  scope note and the output description; and [[0006-inherited-st0604-sei-injection]]'s
  frontmatter `description` still summarized the ADR as accepting an
  unconditional upstream rewrite, though its body carries the revision. The
  title stands — the decision is still to accept the injection, and the video
  is still not a pure passthrough.

* **Re-pinned to libmisbklv `f8cc8c5`** — their fork-22 follow-ons, both of which
  we get for free. `Sei0604::Generate` now *refuses* a non-H.264 source rather
  than silently producing timestamp-free video: irrelevant to us in practice
  (Parrot records H.264) but it turns a class of silent breakage into a startup
  error. And the ST 0603 Time Status we emit is now derived rather than asserted
  — bits 6/5 report a discontinuity when absolute time stops tracking the media
  timeline, which for us means a recording whose telemetry clock jumps will say
  so in the video stream instead of claiming linear time. Suite green on the new
  pin: 34/34 bare, 9/9 `-L parrot`.

* **ST 0604 SEI is now something we ask for, not something we inherit; pinned to
  libmisbklv `e356b0c`.** Upstream made SEI generation opt-in (their fork 22 /
  ADR 0024) — the exact opt-out [[0006-inherited-st0604-sei-injection]] had
  deferred as "the flag has to exist upstream first". `mux_to_ts()` now passes
  `misbklv::Sei0604::Generate` explicitly; without it their default leaves the
  video byte-identical and our output would carry no ST 0604 at all.
  Revision recorded on ADR 0006 — the decision is unchanged, but it is ours to
  state now rather than upstream's to impose.

  **`mux_test` gained property 4** and it caught a real hole immediately: the
  suite passed with SEI generation removed entirely. Nothing in this project
  checked the video elementary stream, so the one requirement that motivated
  ADR 0006 was untested — a dropped argument would have produced an output
  correct in every other respect and useless to the downstream client. The test
  now reads the output's video ES, decodes every `MISPmicrosectime` SEI, and
  requires each timestamp to be an ST 0601 item 2 value we emitted. Verified to
  fail without the argument. (It also caught the author's own first mistake:
  ST 0604 carries absolute item 2 time, not the presentation timestamps.)

  Full suite green on the new pin: 34/34 bare, 9/9 `-L parrot`.

## 2026-07-27

* **libmisbklv rewrote its history; re-pinned to `f8773d5`.** Upstream scrubbed a
  third-party product name from source comments and docs, which rewrote every
  commit from their fork-21 work onward. Our pin `8c296da` was a descendant of
  the scrubbed commit, so it stopped existing: a fresh clone would have failed at
  `FetchContent` even though this machine still resolved it from cached objects.
  Full suite green on the new pin: 9/9 `-L parrot`, 34/34 bare, and
  `TurboVideo`-equivalent content is gone from their history, not just their tip
  (`git log --all -S` finds nothing).
  - **The entries below keep their original hashes** — they record what we
    actually pinned at the time, and falsifying that would defeat the point of a
    chronological log. Anything at or before `c0d4844` is untouched by the
    rewrite and still resolvable. The orphaned ones map as:

    | Cited below (orphaned) | Now | What it is |
    |---|---|---|
    | `f6d52e4` | `032db10` | ST 0604 SEI generation, their fork 21 / ADR 0023 |
    | `b454415` | `5b71ce8` | the merge our drive-by bump landed on |
    | `c093202` | `38400a7` | docs reconciled with their ADR 0023 |
    | `8c296da` | `60d8e7e` | their doc-hygiene lint |

  - ADR 0006's citations were updated in place instead, since a citation is meant
    to be followed; `e3c7f4a` and `f9070fb` there survive the rewrite unchanged.
* **Sanitized a third-party product name out of ADRs 0005 and 0006, then out of
  the unpushed history.** A downstream consumer was named outright in four
  places; it is now described by what it does ("a downstream KLV decoder", "the
  consumer's decoder"). No argument changed — what mattered was the
  `.at()`-on-missing-key behavior, not whose decoder it was.
  - The five unpushed commits were rewritten (`filter-branch` over
    `origin/main..HEAD`), so the name never appears in that range at all. The
    rewrite was verified content-neutral: the tip tree hash is identical before
    and after, since the working copy was already clean.
  - **Then the published history too** (`filter-repo --replace-text
    --replace-message`, force-pushed): all 60 commits rewritten from the
    offending one onward, every SHA from that point changed. Verified before
    pushing — same commit count, tip tree hash byte-identical, zero residual
    occurrences in any blob or message. Suite green after re-syncing: 9/9
    `-L parrot`, 34/34 bare. Local rollback refs dropped and the reflog expired
    afterwards, so the pre-rewrite objects are gone from this machine.
  - **A third occurrence surfaced only during that pass** — `planning/PROGRESS.md`
    in the original commit, which the first inventory missed by checking file
    *counts* per commit but naming files only for some. It was caught because the
    replacement set ended in a catch-all regex. Lesson: when scrubbing, enumerate
    every path, and keep a catch-all that must never fire.
  - Caveat recorded honestly: a force-push does not retract what was published.
    GitHub keeps unreachable objects addressable by SHA until its own GC runs,
    so this was tidying, not revocation.
  - **Convention going forward: identify external consumers by behavior, never
    by product name.**
* **Fork 6 decided — the video is not a passthrough, and has not been since the
  `b454415` pin.** [ADR 0006](./decisions/0006-inherited-st0604-sei-injection.md):
  keep libmisbklv's unconditional ST 0604 SEI injection, retire the "unchanged"
  claim everywhere it appears.
  - **Found by lint**, checking whether our own ST 0604 posture had gone stale
    after the pin bump below. It had not — nothing claimed upstream defers 0604
    — but the adjacent vocabulary turned up a bigger miss: the tool had been
    rewriting the H.264 elementary stream for a full pin cycle while nine sites,
    including the CLI's success message, said it passed video through unchanged.
  - **Verified before believing it**: converting `parrot_v3_video_audio.mp4`
    yields eight `MISPmicrosectime` SEI messages in the output TS, one per
    metadata sample, against zero in the source MP4. Picture Timing SEI is
    stripped and SPS/PPS repeat per IDR.
  - Corrected in `src/main.cpp` (both user-facing strings), `src/mux.{hpp,cpp}`,
    `README.md`, `ROADMAP.md`, `PROGRESS.md`, `mux-stage.md`, `context/index.md`;
    [ADR 0001](./decisions/0001-mp4-extraction-approach.md)'s "re-muxed
    unchanged" annotated as superseded in that one detail rather than rewritten.
  - Scope moved with it: ROADMAP and README listed "KLV standards beyond ST
    0601" as out of scope while every conversion emitted ST 0604. Now stated as
    an inherited exception — carried, not implemented.
  - The claim that survives is **never decoded, never re-encoded**. Full suite
    green after the string changes: 9/9 `-L parrot`, 34/34 bare.
* **libmisbklv pinned to `8c296da`** (doc hygiene pass upstream). `b454415..8c296da`
  touches only `context/`, `planning/`, `README.md` and `CLAUDE.md` — **no source
  changes**, so this bump is behaviorally a no-op here. Full suite green: 9/9
  `-L parrot` (one skip, `conformance_real_data`, no footage present) and 34/34
  bare, which exercises libmisbklv's own tests as the early warning for a moved
  pin.
  - **This also settled the `b454415` provenance question** the earlier entry
    flagged as unverified, by reading the fetched clone directly. `e3c7f4a`
    ("Fix H.264 SEI/SPS association for ST 0604 passthrough") is real, and the
    `h264parse config-interval=-1` narrative was its description, not an
    invention. But the pin landed on `b454415`, a merge that also brings
    `f6d52e4` — **ST 0604 SEI *generation*, upstream fork 21 / ADR 0023** — a
    substantive feature the entry never mentioned. The entry named the wrong
    commit *and* undersold what the pin actually pulled in.
  - Worth knowing for our own ST 0604 posture: upstream no longer flatly defers
    0604 (their ADR 0009 is qualified by ADR 0023). Nothing here depends on that
    yet, but our mux stage inherits it.
* **Fork 5 decided — out-of-range policy for Items 6/7**: the `0x8000` sentinel
  stays the default; `--clamp-attitude` and `--force-emit-attitude` are opt-in
  overrides for consumers that cannot parse it.
  [ADR 0005](./decisions/0005-attitude-range-policy.md) — implemented the same
  day in `src/transform.{hpp,cpp}`, `src/mux.{hpp,cpp}`, `src/main.cpp`. **No
  test covers either flag**; the suite still exercises only the default path.
* **Add progress indicator to conversions** (`src/mux.cpp`): prints "Converting...
  N%" every 10% during `-o` conversions. Uses carriage return to update the same
  line, cleared before final output. Helps visibility for long files.
* **libmisbklv pin moved to `b454415`**, as a drive-by inside the attitude
  commit rather than on its own, contrary to the rule in `CLAUDE.md`. The entry
  here first recorded the hash as `e3c7f4a`; **resolved later the same day** by
  fetching the pin (see below). Both halves were partly right: `e3c7f4a` is a
  real upstream commit and the timing-fix description belonged to it, but the
  pin went to `b454415`, a merge carrying strictly more.
  - The suite was reported green (9/9) at the time of the bump.

## 2026-07-26

* **Optimized `parse_session_metadata` memory footprint.** `parse_session_metadata`
  previously read the entire MP4 file into a `std::vector<uint8_t>`, which could
  allocate multi-gigabytes of memory on large flight recordings. It now scans
  top-level MP4 box headers to locate the `moov` container payload and buffers
  only that box into memory (with a 100 MB ceiling guard) before parsing `moov/udta`
  session tags. Verified clean against `session_metadata_test` and full test suite.
* **Real footage found an audio-track bug no fixture could — so the fixtures
  now have audio.** A real Parrot MP4 carries video *and audio* alongside the
  metadata track; every fixture here carried only video, so the mux never had to
  link a pad it discards. Upstream fixed it (parsebin pad linking, plus an
  H.264 `avc` → `byte-stream` mismatch) and the pin moved to `f9070fb`.
  - `parrot_v3_video_audio.mp4` adds an AAC track in an `mp4a`/`esds` sample
    entry on its own timescale — the shape of a real recording. `mux_test`
    converts it like the others, where "the PMT lists exactly two streams" now
    also asserts the audio was *dropped* rather than forwarded.
  - **Honest limit on that guard: it does not reproduce the bug here.** On
    gstreamer 1.20 `h264parse` already emits `byte-stream` and the pads link
    regardless — the old pin converts the new fixture byte-identically to the
    new pin. CI's 1.24 is the stack where a recurrence would show, which is the
    transitive-include lesson again: green locally says nothing about a stack
    you did not run on.
  - The generator needs ffmpeg for this fixture too, and still only to
    regenerate.
* **Doc lint.** Four fixes, all of them the future going stale rather than the
  past being wrong:
  - **Two ADRs still deferred work to Phase 3, which has landed.**
    [[0001-mp4-extraction-approach]]'s "Video handling TBD" is settled: the
    GStreamer path won, but inside libmisbklv, so no video crosses our code and
    the ADR's boundary holds rather than bends. [[0002-unmappable-parrot-fields]]
    rejected a private Local Set on *scope* "until the primary path works end to
    end" — that objection is now spent, and the remaining gate is the other half
    of its condition, a concrete consumer.
  - **README's structure block predated two tools and the new CI files** — it
    listed `parrot-to-klv + parrot-scrub` (missing `parrot-conform`), only
    minimp4 under `external/`, and only `link-check.yml` under workflows.
  - **CLAUDE.md gained the two things a fresh agent would get wrong**: a bare
    `ctest` here runs libmisbklv's suite as well (`-L parrot` is ours, and a new
    test without the label is silently absent from CI), and regenerating the two
    video fixtures needs ffmpeg.
  - Checked and clean: every wiki link resolves (the lone `[[wiki]]` hit is
    still prose *about* wiki links), no live tallies in present-tense docs, and
    PROGRESS/ROADMAP carry no forward-looking mention of work now done.
* **Build CI added** (`.github/workflows/build.yml`): configure, build and
  `ctest` on Ubuntu, on push and PR.
  - **Our tests now carry a `parrot` label and CI runs `ctest -L parrot`.**
    libmisbklv is a subproject, so an unfiltered `ctest` runs its suite too —
    fine locally as early warning when the pin moves, but several of its tests
    need ~355 MB of git-lfs media. Labeling ours beat enumerating theirs to
    exclude: the list of ours is a thing this repo already knows.
  - `GIT_LFS_SKIP_SMUDGE=1` at configure time keeps that media as pointers
    (355 MB → 28 KB), since nothing here opens it.
  - **The runner needs its own credential**: `GITHUB_TOKEN` is scoped to this
    repository and cannot clone a *different* private one. A read-only deploy
    key on libmisbklv, held as `MISBKLV_DEPLOY_KEY`, is the narrowest thing that
    works; the workflow fails with a pointed message if the secret is missing
    rather than dying in a confusing clone error. Setup and the rejected
    alternatives: `.github/workflows/README-ci.md`.
  - Runtime plugin packages are part of the dependency list, not just the `-dev`
    ones: the mux needs playback/parsebin + app (base), isomp4 (good), and
    mpegtsmux/tsdemux/h264parse (bad). A build-only package set compiles and
    then fails in `mux`.
  - Verified locally by reproducing the recipe — clean clone at the pin with LFS
    smudging off, then `ctest -L parrot`: 9 tests, green.
* **libmisbklv pinned to an exact commit** (`f818fd9`) instead of tracking
  `main`. FetchContent does not re-fetch an already-populated dependency, so a
  branch tag lets two checkouts of identical source build against different
  upstream commits depending on when each was first configured — which had
  already happened here: Phase 3 was verified against a local checkout while
  `main` had moved on. Bumping the line is now the reviewable record of moving
  upstream. Verified from a clean clone at the pin: full suite green.
* **Both workarounds for libmisbklv gaps are gone — upstream fixed both**
  (its ADRs 0021 and 0022, raised from here after Phase 3 landed).
  - **Read-path timestamps exist now**, so `mux_test` reads presentation times
    through `KlvStream` instead of parsing PES headers itself, and asserts each
    packet comes back at its own sample's time — absolute placement, not just
    spacing. That is the stronger claim: an origin error passes an interval
    check. Measured on our own output, the round trip is exact to 90 kHz
    rounding, origin included.
  - **`mux.cpp` no longer removes the output on failure.** The "no file left
    behind" guarantee now covers the whole insert session rather than just the
    open, which is where our failing case sat (a declared-but-unparseable video
    track fails at EOS). `mux_test` keeps asserting the property from this side:
    it is this tool's promise to its user whoever implements it.
  - One correction from upstream worth recording: `data/Day Flight.mpg`, which
    was the evidence for "the timestamps are in the file and dropped on read",
    genuinely has no PES timestamps — `kNoPts` was right for that one sample.
    The gap was real on files that do carry them.
* **Phase 3 landed: `parrot-to-klv in.mp4 -o out.ts` writes an MPEG-TS carrying
  the recording's own video plus a synchronous ST 0601 KLV stream.** The
  pipeline is end to end for both supported formats. Shape and traps:
  [[mux-stage]].
  - **libmisbklv's `KlvSink` gained a `video_source`** (its ADR 0020, upstream
    `main`), which is what made this a loop rather than a container project:
    `filesrc ! parsebin` joins the KLV appsrc into one `mpegtsmux`, so the
    drone's H.264 (or an Ai's H.265) is re-muxed unchanged, never decoded.
  - **The timeline is the metadata track's own sample times.** libmisbklv
    requires both branches on one timeline — presentation time from the start of
    the source file — and `MetadataSample::timestamp_us` is already on it, since
    both tracks are timed from the start of the same MP4. Deliberately *not* the
    frame's monotonic clock and not Item 2's UTC: those say when telemetry was
    sampled, the PTS says where the packet sits in the file.
  - **A failed conversion now leaves no output file.** libmisbklv unlinks only
    for a failure while opening; a source whose video is declared but does not
    parse opens fine and fails at EOS, leaving the sink's empty `.ts`. `mux.cpp`
    removes it — but only a file this run created, so a pre-existing file at that
    path survives a failed run.
  - **Two new fixtures carry real H.264**, `parrot_v3_video.mp4` and
    `parrot_proto_video.mp4`: gstreamer will not accept the five dummy bytes per
    frame the extraction fixtures use. `make_fixture.py` encodes 8 all-IDR
    64x64 frames with ffmpeg and repackages them as AVCC samples with an `avcC`.
    ffmpeg is needed to regenerate, never to run the suite — the `.mp4` is
    committed, so the "no test needs `data/`" rule is untouched.
  - **`mux_test` reads the container, not our own encoder**: PMT lists video +
    KLV (0x06/KLVA), packets come back byte-identical in sample order, and PES
    timestamp intervals match the sample spacing. The timestamps have to be
    parsed off the TS because libmisbklv's *extract* path reports every packet as
    `kNoPts` — its reassembly drops the buffer timestamp. An upstream gap; what
    this stage writes carries the times correctly.
  - `parrot_mux` is a separate static library from `parrot_extract`, because it
    is the one part of the tool that needs a media backend.

## 2026-07-25

* **Doc lint.** Five items, one of them a rule violation rather than ordinary
  staleness:
  - **PROGRESS had become a changelog.** Its "Now" had grown one triumphal
    paragraph per landed feature — v3 parser, conform, scrub, proto, transform —
    which is precisely the "Done history" the hygiene rules put in this log
    instead. It also still opened with "Phase 1 (Parse) in progress", which the
    corrected phase statuses contradict. Rewritten as present state: what the
    tools do now, where the phases stand, what passes.
  - **A live tally in durable prose**, also against the rules: PROGRESS claimed
    "33 items, a v3 frame 29" and [[transform-stage]] "roughly 29 items". Both
    now describe the state — every Tier 1 item with a source — and note that
    proto yields more than v3 without counting either.
  - [[field-mapping-analysis]]'s open question on anchoring Item 2 was answered
    when session metadata landed; struck and pointed at the implementation.
  - **[[0002-unmappable-parrot-fields]] gained a follow-on note.** The coverage
    report it required now exists, which settles its open question about the
    report format (human-readable), and the implementation added two things the
    ADR did not anticipate: omissions need *reasons* rather than a bare list,
    and the report is itself sensitive enough to withhold coordinates and the
    serial by default.
  - [[0004-shared-telemetry-snapshot]]'s assumption that "nothing parses the MP4
    session metadata yet" stopped being true the same day; marked resolved.
  - Checked and clean: every source and test file appears in a Files table,
    every `ctest` target is documented, and all wiki links resolve (the lone
    `[[wiki]]` hit is prose *about* wiki links in an old entry).
* **Scope narrowed to v3 and proto; phase statuses corrected; fork 4 opened.**
  A planning pass, no code.
  - **Supported formats are now v3 and proto** — the ANAFI range and ANAFI Ai /
    UKR, every Parrot drone still in production, both implemented. **v2** (Bebop
    2, Disco, Bluegrass) becomes a deferred add-on rather than committed scope;
    ADR 0004's normalization boundary means it would add one normalizer and
    touch nothing else if it ever lands. **v1** moves to out of scope, being
    deprecated by Parrot. ROADMAP § Scope, README and CLAUDE.md all said "all
    Parrot formats" and now say what is actually supported.
  - **The open mapping questions and the integration API are deferred**, and
    said to be so rather than left looking like pending work: Items 134, 45/46,
    48 and Tier 3 VMTI stay enumerated in [[field-mapping-analysis]] § Open
    questions, and each is currently *reported* as an omission rather than
    guessed at, so deferring them costs correctness nothing.
  - **Phase statuses corrected.** Phase 0 had read "(in progress)" while Phases
    1 and 2 had largely landed. Now: 0 done, 1 done for the supported formats,
    2 Tier 1 done, 3 next.
  - **Fork 4 (Tier 2 geometry fidelity) promoted from candidate to `OPEN`** —
    its stated trigger was the transform stage reaching the derived items, which
    it now has.
* **`camera.spectrum` added to the proto reader; Item 11 emitted — Tier 1 is
  now complete.** Field 17 of `CameraMetadata`, transcribed into
  `test/vmeta_subset.proto` alongside the `CameraSpectrum` enum, normalized to a
  format-neutral `ImageSpectrum` by name (as with the piloting modes).
  - **Item 11 is free-form utf8**, not a two-valued enum — the standard calls it
    "name of currently active sensor", and §8.11.1's example configuration names
    sensors `EO Narrow`, `EO Wide`, `IR Sensor`. So `camera.subtype` (field 18)
    was added alongside and the name is composed: the spectrum gives the stem
    (`EO`, `IR`, or `EO/IR Blended` for a fused image rather than one of its
    halves), the subtype qualifies it — `EO Wide`, `IR Tele`.
  - An unknown **spectrum** emits nothing even with a known subtype: a bare
    `Wide` does not say whether the imagery is visible or thermal. An unknown
    **subtype** leaves the stem unqualified.
  - The omission reason now distinguishes the two cases it always should have:
    v3 reports Item 11 as `format does not carry it`, a proto frame that simply
    did not report a spectrum as `not present in this frame`.
  - A proto frame with session metadata now yields 33 items, a v3 frame 29.
    Everything still unemitted is unemittable: 45/46 and 134 are open mapping
    questions, Tier 2 awaits the geometry fork, and v3 genuinely lacks an
    ellipsoid height, air speed and spectrum.
* **MP4 session metadata parsed; Items 2, 4 and 10 unblocked.**
  `src/session_metadata.{hpp,cpp}` reads Parrot's untimed tags from `moov/udta`.
  See [[mp4-extraction]] § Session metadata.
  - Reads the **four-character key method** (`moov/udta/meta/ilst` — `©mod`
    model, `©too` serial, `©day` media date, `©ART`, `©mak`, `©swr`, `©nam`).
    The full reverse-DNS method under `moov/meta/ilst` is not implemented: it
    needs the `keys` box for index-to-name mapping, and the fields it would add
    beyond these have no 0601 item anyway.
  - **The geotag is not in the ilst** — Parrot puts `©xyz` directly in
    `moov/udta` for Android compatibility. A parser that only descends into the
    ilst misses the recording's position, which is also why the scrubber looks
    for it there.
  - **Item 2 for v3 now works.** `anchor_to_session()` treats the media date as
    the first sample's wall time and adds each frame's elapsed monotonic µs, so
    frame spacing stays exact and absolute time is good to the media date's
    *second* granularity — the limit the file imposes, recorded rather than
    papered over. A frame with a real UTC stamp is never overwritten by the
    anchor, and a session with no parsable date leaves the frame unanchored
    rather than dating it to 1970.
  - The timezone offset is the trap the tests target hardest: dropping it would
    put every frame two hours out, consistently and invisibly. `timegm`, never
    `mktime` — the converter's local timezone has nothing to do with when the
    flight happened — and the conversion was cross-checked against Python.
  - **Device identity got its own opt-in.** The serial reaches Item 4, so the
    coverage report could print it; that is identity, which the conformance
    reports exclude by construction. Rather than folding it into
    `--with-location`, `Disclosure` now has separate `location` and `identity`
    flags (CLI: `--with-location`, `--with-identity`) — they expose different
    things about different people.
  - Item 11 (EO/IR) remains the one Tier 1 item with no source: session tags do
    not carry the spectrum and the proto reader drops `camera.spectrum`.
    Omitted rather than assumed to be EO.
* **Fork 3 (transform input shape) resolved** —
  [[0004-shared-telemetry-snapshot]], `accepted`. Opened and decided as Phase 2
  began; [[0003-protobuf-dependency]] had explicitly deferred it.
* **Transform stage started: Tier 1 mapped end to end.** `src/telemetry.*`
  normalizes either frame type into a `TelemetrySnapshot`; `src/transform.*`
  maps that to a real ST 0601 packet through libmisbklv's registry codec. See
  [[transform-stage]] for the shape and the traps.
  - ~26 items from a typical frame, assembled with UL key, BER length and Item 1
    checksum. `transform_test` parses every emitted packet back through
    `misbklv::parse_packet` and decodes item by item, so values are checked as a
    consumer sees them rather than as we staged them.
  - **A privacy bug caught while testing the CLI**: the coverage report printed
    every emitted item's value, including Items 13/14 — latitude and longitude —
    regardless of `--with-location`. On real footage `--klv` would have leaked
    the flight's position into exactly the output someone pastes into an issue.
    `to_text()` now withholds Items 13/14/40/41 unless asked, and the test
    asserts both directions.
  - Sign and range traps handled and tested: Item 51 negated (0601 measures
    toward zenith, Parrot NED is positive down); Items 6/7 signaled with
    `0x8000` when out of their ±20°/±50° range rather than clamped, with 90/91
    carrying the true value; Item 112 omitted below 0.5 m/s because course over
    ground is undefined at rest; `asin` clamped so a barely-non-unit v3
    quaternion cannot yield NaN pitch.
  - **Item 2 is mandatory and still sometimes absent.** No v3 recording can
    supply it, and proto may declare its UTC stamp invalid. The packet is then
    assembled without mandatory enforcement and the omission is reported — a
    fabricated timestamp is indistinguishable from a real one downstream, a
    missing item is not.
  - The coverage report is ADR 0002's obligation made concrete, and is wired
    into `parrot-to-klv --klv`: emitted items with values, omitted items each
    with a reason, and the Parrot fields with no 0601 item at all. Tier 2
    geometry is reported as `needs terrain intersection` rather than
    approximated — its fidelity fork is still open.
* **Proto parser implemented** — `src/parrot_proto.{hpp,cpp}`, a hand-written
  proto3 wire reader per [[0003-protobuf-dependency]]. `parrot-to-klv` now
  summarizes v3 and proto both, and `test/vmeta_subset.proto` records the
  transcribed schema subset (documentation only — nothing compiles it).
  - **Schema traps the transcription surfaced**, each of which would have been a
    silent mis-decode: `Location.altitude_egm96amsl` is field **7**, not 4 (4 is
    `sv_count`); `battery_percentage` and wifi `rssi` are `sint32`, so **ZigZag**
    — reading them as plain varints turns −55 dBm into 109; the proto
    `PilotingMode` is offset by one from the binary enum (`PM_MANUAL` = 1, binary
    `Manual` = 0), so it gets its own C++ type rather than sharing `PilotingMode`;
    and proto carries the FOVs in **radians** where v3 uses degrees, named
    `picture_hfov_rad` so the difference cannot be missed at a call site.
  - **Bug caught by its own test:** message fields originally called
    `read_bytes()` without checking the wire type, so a group tag (wire 3) on
    e.g. field 1 read the next byte as a length — and a zero there decoded as a
    valid *empty* submessage. A mis-synced stream would have parsed "cleanly"
    into a default-valued frame. Now routed through `read_msg()`, which insists
    on wire type 2.
  - No depth guard, deliberately: no parser calls itself, so nesting is bounded
    by the schema (three deep) rather than by the input. An earlier draft
    declared a `DepthExceeded` error it never enforced; removed rather than
    shipped.
  - Tests cover the fixture field-by-field plus the wire edges — truncation,
    over-long varints, undefined wire types, field number 0, ZigZag at −1,
    unknown fields of every wire type, and a Starfish `oneof` that must not
    count as a Wi-Fi link. `parrot_proto.mp4` now holds real serialized
    `TimedMetadata`; it previously held v3 bytes under a proto MIME type, which
    only ever exercised format *detection*.
  - **Validation is weaker than for v3, and stays that way for now**: no proto
    recording exists to check against, and the fixture encoder in
    `make_fixture.py` is hand-written because this machine has no protobuf
    toolchain. Encoder and decoder are both ours, so a shared misreading of
    proto3 would pass. Tracked as an open question in the ADR.
* **Fork 2 (protobuf dependency) resolved** —
  [[0003-protobuf-dependency]], `accepted`. Opened from the ROADMAP candidate
  list and decided in the same session.
* **Verified on the footage machine: the updated tool reproduces both committed
  reports byte-for-byte.** Regenerating over the real recordings left the working
  tree clean, which settles the two things that could only be predicted from a
  machine without footage — the schema v3 reports edited in place match what the
  renderer emits, and the quaternion-seeding fix does not move either report (so
  those recordings' first samples carry uniform norms, consistent with the
  1.000/1.000 they already showed).
* **Consistency scan over the conformance work; two fixes in
  `run_conformance`.** Swept the sources and docs after the tooling landed
  across two sessions:
  - **Quaternion norm range was seeded once per quaternion instead of once per
    sample.** `first` is only cleared after the per-sample loop, so all three
    quaternions of the first decoded sample each re-seeded min/max and the two
    before the last were discarded. Narrow effect — it could hide an outlier
    occurring only in quats 1–2 of sample 0 — and it never weakened an
    assertion, since `quaternions_are_unit_norm` tests every quaternion
    unconditionally. Fixed with a per-sample `first_quat` flag.
  - `XXH3_createState()`'s result was dereferenced unchecked; now reported as an
    error like any other unreadable input.
  - The fixture report is byte-identical across the fix (its quaternions are all
    unit, so the discarded values equalled the kept one). That confirms the
    committed reports stay valid under `--check`; it does not exercise the bug,
    which needs a first sample with differing norms.
  - Doc drift found and fixed: `CLAUDE.md` § Repo layout still described
    `external/` as minimp4 alone after xxhash.h was vendored beside it. Nothing
    else was stale — no `input_sha256` survives outside this log's history.
* **Report schema v3: `input_sha256` renamed to `input_hash`.** The field had
  held an xxHash since v2 while still being named for SHA-256 — misleading in a
  committed artifact, and the "kept for compatibility" justification was empty
  since no v1 reports survived the v2 regeneration. The two committed reports
  were edited in place rather than regenerated (the footage is gone); the
  rename and the version bump are their only textual changes, and the renderer's
  output on a fixture was diffed against them to confirm `--check` will still
  match byte-for-byte when footage is present.
* **Conformance reports committed for two real ANAFI recordings.** Ran
  `parrot-conform --scan data --reports test/conformance` on the machine that
  held the footage; the recordings have since been removed, the reports remain.
  Snapshot at write time — 699 samples (92 MB) and 10,709 samples (1.2 GB),
  **100% decoded, zero errors, every structural check passing on both**:
  - **The sample shape does not vary.** 144 bytes in every sample of both
    recordings — no histogram spread at all — and the same three extensions in
    every frame at fixed payload lengths: `0x4531` (8 B), `0x4533` (28 B),
    `0x4536` (24 B). The decoder's fixed-layout assumption holds against a real
    encoder across two flights.
  - **0x4536 is confirmed real and universal**, at 24 payload bytes in all
    11,408 samples, despite reference docs stating it does not exist. Its
    length across every sample is now committed evidence — most of what would
    identify it if it is ever worth decoding.
  - Quaternion norms 1.000 min and max in both; frame deltas 33,323–33,411 µs
    (30 fps, monotonic and strictly increasing throughout).
  - The long recording exercised more of the state machine —
    `landed`/`taking-off`/`flying`/`hovering`/`emergency` — so the flying-state
    decode is covered beyond the steady-state cases, `emergency` included.
  - Both flights were `manual` piloting mode, so no automated-mode evidence yet;
    that remains unexercised by real footage.
  - `data/` on this machine is now empty, so `conformance_real_data` skips (77)
    as designed — verified after the deletion.
* **`parrot-conform` switched to xxHash and anonymizes report filenames.**
  Performance and security improvements:
  - Replaced SHA-256 with xxHash XXH3_128 for 20-100× faster file identification
    (1.2GB scan: 2s total vs 6+s, hash step <100ms vs 4+s). Hash is for
    identification only, not security, so collision resistance of 128 bits is
    plenty.
  - Report filenames now derived from hash prefix (`a0ff2d3b1fdb07ab.json`)
    instead of source filename, anonymizing sensitive video names. Label field
    defaults to same prefix. Console still shows original filename for context.
  - Added file-size display and progress dots (every 100MB) during hashing.
  - Schema version bumped to 2 (hash algorithm change). `input_sha256` field now
    holds xxHash XXH3_128, not SHA-256.
  - Vendored xxhash.h single-header library (BSD-2-Clause).
* **Doc lint after `parrot-conform`.** Four items, two of them rule violations
  rather than ordinary staleness:
  - `PROGRESS` "Next" had grown a *fork* — the protobuf-dependency choice for the
    proto parser — stated inline. Forks live in ROADMAP; PROGRESS points at them.
    Moved to ROADMAP candidates, PROGRESS now defers ("open work has one home").
  - `CLAUDE.md` § Repo layout said "tests must never require anything from
    `data/`", which reads as false now that `conformance_real_data` uses `data/`
    when present. The rule still holds in substance and is now stated precisely:
    no test may *require* footage, one test may *use* it and skip without.
  - [[mp4-extraction]]'s Files table predated the conformance tool, and its title
    and description still described a doc about extraction alone. Retitled to
    cover the Phase 1 tooling it has actually accumulated.
* **`parrot-conform` added: real-encoder evidence without the recording.** The
  test-data problem had two unsatisfying answers — commit a scrubbed video (still
  imagery nobody wants in a repo) or source a public one (attempts kept turning up
  files whose metadata track had been transcoded away). This commits the
  *evidence* instead: a JSON report of what a real encoder produced — sample-size
  histogram, extension ids with payload lengths, decode rate and error kinds,
  quaternion norm range, aircraft-state ranges, and structural assertions.
  - **No position, absolute time, or device identity, by construction** rather
    than by filtering: nothing that reads a coordinate writes to the report, and
    latitude/longitude appear only as range checks. `conformance_test` enforces it
    negatively — renders a report from a fixture with known coordinates and fails
    if any appear at any precision or in fixed-point form.
  - `ctest` target `conformance_real_data` regenerates and compares against the
    committed reports, so a decoder change that alters how a real recording reads
    is caught. Skips (`SKIP_RETURN_CODE 77`) where `data/` is absent, empty, or
    holds nothing applicable — a no-op in CI, a real regression test where the
    footage lives. A non-Parrot video in `data/` is skipped, not failed.
  - Reports are tied to recordings by hash (originally SHA-256, now xxHash), so a
    report whose input was replaced is detectable rather than silently stale.
  - Side benefit: the report records `0x4536`'s payload length across every
    sample, which is most of what would identify that undocumented extension.
* **Doc lint.** Six pieces of drift, all from the last two days' work outrunning
  the docs describing it:
  - [[mp4-extraction]] still said "these fixtures prove MP4 handling only… the
    sample payloads are invented", which stopped being true when the generator
    started emitting real v3 structs. Replaced with a fixture table and the
    sharper caveat that actually holds: fixtures generated *from* the docs cannot
    catch a parser wrong in the same way the docs are — only real footage can,
    and did.
  - Its Files table listed four files out of nine, predating the v3 decoder and
    the scrubber; its frontmatter description predated scrubbing too.
  - [[field-mapping-analysis]] still asserted flatly that v2/v3 have "no LFIC",
    contradicting the hedged 0x4536 status in [[parrot-metadata-formats]]. Items
    40/41/42 now carry the same open question rather than a claim the sibling doc
    withdraws.
  - [[parrot-video-metadata-docs]] listed LFIC among the reference's documented
    extension structures. It is not there.
  - `parrot-metadata-formats.md` frontmatter still carried its 2026-07-23
    timestamp and original description after being substantially rewritten.
  - README structure block didn't mention `test/fixtures/` or that `src/` now
    builds two tools.
* **Scrubber leak fixed: undecoded extension payloads passed straight through.**
  Found immediately after the 0x4536 discovery below, and demonstrated with a
  fixture whose 0x4536 payload held coordinates — `parrot-scrub` reported success,
  listed its usual caveats, and left all of them in the output. Since real ANAFI
  recordings carry 0x4536 in *every* sample, the tool's central promise was false
  for exactly the files it exists to handle.
  - Undecoded extension payloads are now blanked by default, id and length kept
    so the extension stays well-formed and is skipped identically. Known-safe
    extensions (timestamp, thermal) are preserved; automation is rewritten as
    before. `--keep-unknown-extensions` opts out and the report then states the
    output is **not** scrubbed.
  - The regression test asserts the coordinates hidden inside the extension are
    absent from the whole output — the same standard the other scrub tests use.
  - The general rule this encodes: an extension we cannot decode is one we cannot
    inspect, so it cannot be trusted. "Unknown" has to mean "cleared", not
    "assumed harmless".
* **v3 decoder verified against real Parrot ANAFI footage.** A real recording with
  hundreds of samples decoded successfully at 100% completion rate. All base
  structure fields parse correctly: state, mode, battery level, RSSI, satellite
  count, altitude AGL, NED velocities, field-of-view angles, quaternions (drone and
  frame), and microsecond timestamp. Automation extension detected and parsed in
  every sample. Quaternion magnitudes validate at exactly 1.0, confirming the
  fixed-point decoding is correct.
  - **0x4536 extension appears in every real sample.** The reference docs say "0x4536
    appears nowhere" and that LFIC exists only in protobuf format, but real v3
    recordings contain a 0x4536 extension in every frame. The decoder correctly skips
    it by `ext_length` (unknown extensions are logged but don't cause failure), so
    decoding succeeds. This suggests the reference documentation may be incomplete
    rather than the encoder being wrong. The extension's content is not needed for
    ST 0601 field mapping since target coordinates come from the automation
    extension's `framing_target_*` fields.
* **Metadata scrubber added** (`parrot-scrub`). Turns a real flight into a
  shareable fixture by replacing its location fields with synthetic values,
  patching **in place** over a byte-for-byte copy so sample sizes never change
  and every box, table and chunk offset stays valid — the output is a specimen of
  the real encoder in all respects but the coordinates.
  - Two separate location channels: the v3 telemetry track (latitude, longitude,
    altitude, and the automation extension's two coordinate triples) and the
    `moov/udta` `©xyz` ISO 6709 geotag, which no metadata-level scrub would
    otherwise touch. The geotag is neutralised by zeroing its digits, preserving
    string length so nothing needs resizing.
  - **Refuses what it cannot fully scrub** (proto format, non-Parrot files) and
    writes no output — a file that looked scrubbed while still carrying real
    coordinates would be worse than a failure.
  - Does **not** touch the imagery; `--zero-media` blanks video/audio payloads
    for output destined for a repository. The tool says so rather than leaving it
    to be discovered.
  - Tests assert the original coordinates' encoded byte pattern is absent from
    the whole output, not merely that the decoded fields changed.
* **Parrot v3 binary parser implemented** (`src/parrot_v3.{hpp,cpp}`). Decodes the
  72-byte base structure and the timestamp (0x4531), automation (0x4533) and
  thermal (0x4534) extensions, converting fixed-point to SI at parse time.
  Unknown extensions are skipped by `ext_length` and recorded rather than
  ignored; a sample that does not decode cleanly returns a typed error instead of
  a partial frame. `test/make_fixture.py` now emits structurally valid v3 payloads
  with known values, so `parrot_v3_test` asserts field-by-field against the
  generator's inputs. Clean under ASan+UBSan over 60,300 random buffers.
* **Two reference errors corrected while implementing it.**
  - **There is no binary LFIC extension.** `0x4536` appears nowhere in the
    reference; LFIC exists only in the protobuf schema. [[parrot-metadata-formats]]
    listed it as a standard v2/v3 extension and [[field-mapping-analysis]] cited
    it as the v2/v3 source for Items 40/41/42 — same class of fabrication as the
    ST 0601 tag numbers. Items 40/41/42 now cite the automation extension's
    `framing_target_*` for v3, and nothing for v2.
  - **v2 and v3 are not layout-compatible.** They share a prefix through
    `drone_z`, then diverge: v3 inserts a frame-base quaternion, drops v2's
    explicit `camera_pan`/`camera_tilt`, adds AWB gains / FOV / link quality, and
    reorders the trailing four bytes. [[parrot-metadata-formats]] had described a
    single shared "base structure"; it now carries both byte layouts in full.
    Also recorded: binary and protobuf number `piloting_mode` differently, so the
    enum maps must go through names, not values.
* **Extraction verified against real Parrot footage.** `./build/parrot-to-klv
  data/P0330036.MP4` detected 699 samples and extracted successfully — the box
  scan and format detection survive a real ANAFI file the synthetic fixtures
  don't represent.
* **CMakeLists.txt fixed to use SSH URLs.** FetchContent was using HTTPS for
  libmisbklv, prompting for GitHub credentials even with SSH keys configured.
  Changed `GIT_REPOSITORY` from `https://github.com/...` to `git@github.com:...`
  so it uses SSH authentication.
* **MP4 metadata extraction works end to end.** The extractor did not compile;
  fixing it turned up seven distinct defects, most of them fabricated API:
  - `MP4D_frame_size()` and `MP4D_frame_duration()` do not exist in minimp4.
    `MP4D_frame_offset()` takes six arguments, was called with four, and returns
    an *unsigned* `MP4D_file_offset_t`, so the `offset < 0` guard was dead. Its
    out-params supply size and timestamp together, which also deleted an O(n²)
    loop that re-summed durations from zero for every sample (~162M iterations
    on a 10-minute 30 fps track).
  - `MP4D_HANDLER_TYPE_META`/`_NONE` do not exist — minimp4 defines only `VIDE`
    and `SOUN`. `track.duration_units` does not exist either (`duration_hi`/`lo`).
  - The read callback returned the byte count, but minimp4 treats **any truthy
    return as failure** (see `minimp4_fgets`), so a successful read read as an
    error. `MP4D_open` returns 1 on success and the caller tested `!= 0`.
  - `MP4D_open(..., 0)` passed a zero file size; minimp4 treats
    `read_pos >= read_size` as EOF, so parsing aborted at the first skip.
  - `MP4D_close()` was never called — the per-track index arrays leaked.
* **Format detection is real, and no longer guesses.** `extract_mime_type()` was
  a stub returning `""`, and the caller then *defaulted to v3* — which would have
  fed a proto-format ANAFI Ai file to the v3 binary parser and produced confident
  garbage. minimp4 cannot supply it: `MP4D_track_t::SampleDescription` is a union
  covering `soun` and `vide` only, so a metadata track's sample entry is parsed
  past and dropped. New `src/mp4_box_scan.{hpp,cpp}` walks the box tree directly
  (consistent with [ADR 0001](./decisions/0001-mp4-extraction-approach.md)) and
  recovers `tkhd` track IDs, `hdlr` handler types, the `stsd` sample entry, and
  `tref`/`cdsc` links. The MIME type now yields a typed `ParrotFormat`, and an
  unrecognized one is a hard error listing what was found.
* **Test fixtures are synthetic and committed.** `test/make_fixture.py` builds
  MP4s with Parrot's documented track layout and invented payloads, so extraction
  is testable without committing footage that carries live location data. Covers
  v3, proto, a writer that fills in `content_encoding` (moving the MIME type to
  the second string), and a non-Parrot `meta` track that must be refused. Wired
  into `ctest` as `mp4_extraction`; the suite passes, and the box walker survives
  6701 malformed inputs (every truncation plus every single-byte corruption at
  three values) clean under ASan+UBSan.
* **Doc lint after the extraction work.** New [[mp4-extraction]] Component doc
  (first entry under `index.md` § Components) carries the durable knowledge the
  above turned up — the division of labour and the minimp4 traps — so it isn't
  re-derived from a log entry. ADR 0001 gained a follow-on note: implementation
  took *both* of its options (minimp4 for the sample index, a custom reader for
  the sample entry), which sharpens the decision rather than changing it. Also
  added `test/` to the README and `CLAUDE.md` structure listings, and converted
  two `[[wiki]]` links in `planning/` to relative Markdown — wiki links resolve
  within the bundle, and the link-check CI only validates Markdown ones.
* **Field mapping rewritten against the complete ST 0601.19 registry.** libmisbklv
  commit `a981a1a` registers §8.1–8.143 (all but deprecated Item 66), validated
  against the standard's worked examples, so the mapping could finally be settled
  item by item instead of guessed.
  - [[field-mapping-analysis]] was **substantially wrong above tag 65** and is
    replaced, not amended. Its "missing tags" tables carried tag numbers written
    from assumption: 3, 53, 54, 92, 93, 103, 106, 107, 108, 114, 134 and 135 are
    all different items than it claimed, and the "Sensor Lat/Lon/Alt Extended" and
    "Platform Groundspeed Extended" items it proposed adding do not exist in 0601
    at all. It also premised a precision problem that isn't real — Items 13/14 are
    int32 mapped over ±90/±180, ~4.7 mm.
  - Its four-phase plan is gone with it. The old Phase 2/3 existed to extend
    `uas0601.toml`; **no registry work is required for this project.** Replaced by
    three tiers: direct mappings, derived geometry, VMTI.
  - Real items the full registry unlocked: 90/91 Platform Pitch/Roll (Full), which
    matter because Items 6/7 cap at ±20°/±50° and a quadcopter exceeds that
    routinely; 79/80 Sensor North/East Velocity with 51 Platform Vertical Speed
    (negated — §8.51 is zenith-positive, Parrot NED is down-positive); 75 Sensor
    Ellipsoid Height; 123 NAVSATs in View ← `sv_count`; 124/125/126 positioning
    and status enums; 134 Zoom Percentage; 40/41/42 Target Location ← the LFIC
    extension, which the old doc had written off as unavailable.
  - Re-read Parrot's `vmeta.proto` and the v2/v3 structs verbatim from
    `references/`, correcting two things the format doc obscured: proto `hfov`/
    `vfov` are **radians** while v2/v3 `picture_[hv]fov` are **degrees**, and
    proto `Location` carries **both** `altitude_egm96amsl` and
    `altitude_wgs84ellipsoid` — so the geoid conversion the old doc called for is
    unnecessary on proto.
* **Fork 1 opened, deliberated, and accepted: Parrot fields with no ST 0601 item**
  — [ADR 0002](./decisions/0002-unmappable-parrot-fields.md).

* **README made human-facing; rule rationale moved into the bundle.** The
  top-level README carried two agent-facing sections inherited from the template
  ("The doc taxonomy", "Why these rules"), which cluttered what a human sees and
  put workflow directives outside the docs agents read.
  - New [[workflow-rationale]] (`context/workflow-rationale.md`) is now the single
    home for the per-rule reasoning; added to `index.md` § Bundle guide, and
    `CLAUDE.md` § Planning hygiene points there instead of at the README.
  - "The doc taxonomy" was **deleted, not moved** — it duplicated
    [`CONVENTIONS.md`](./CONVENTIONS.md) § Layout, which already carries the same
    four-directory table.
  - README now runs: what it is → build → status pointer → a short "Working on
    this project" pointer → structure → license. It states no rules itself.
  - Same change upstream in `okf-project-template`, where the README keeps a short
    pitch that links to the rationale rather than hosting it, and `SETUP.md` now
    tells setup to delete both sections rather than keep them.
* **Doc-workflow consistency pass.** Three fixes to the docs describing the
  workflow itself:
  - `CLAUDE.md` — promoted "Before every commit checklist" from `###` to `##`.
    As a subsection it structurally swallowed the doc-role bullets and the "So:"
    rules that belong to § Planning hygiene, orphaning them from their intro
    sentence. Now a sibling section, still ahead of the hygiene rules.
  - `README.md` § "Why these rules" — added the missing rationale for the
    same-commit rule. The rule landed without one, which § Planning hygiene
    itself warns against ("a rule without its reason gets deleted the first time
    it's inconvenient").
  - `README.md` — completed the setup step that was skipped when this project was
    created: the file was still `okf-project-template`'s README verbatim (template
    title/intro, Quick Start, Manual setup, a dead `SETUP.md` link, 0BSD license
    line, template Structure block). Replaced with this project's identity, build
    instructions, real structure, and the Apache 2.0 license line. The doc taxonomy
    and "Why these rules" sections were kept, per `SETUP.md` § 2.
* The checklist and the same-commit rationale are being ported back upstream to
  `okf-project-template`; the README fix is local (the template's own README is
  correct as-is).

## 2026-07-23

* **Project initialized** from `okf-project-template`. Defined scope: convert
  Parrot telemetry from MP4 to MPEG-TS with MISB 0601 KLV. C++ with CMake,
  FetchContent for libmisbklv. License: Apache 2.0.
* **Domain types added**: `DataFormat` and `Standard` to the type vocabulary in
  CONVENTIONS.md.
* **Reference sources identified**: Parrot video metadata documentation (HTML),
  libvideo-metadata repo (GitHub, reference only), libmisbklv MISB specs
  (ST 0601.19, ST 0903.6, ST 1201.5).
* **Phase plan established**: Phase 0 (Foundation), Phase 1 (Parse), Phase 2
  (Transform), Phase 3 (Mux). First tasks: CMake setup, Parrot format research,
  field mapping documentation.
* **Parrot metadata formats researched**: Analyzed v1, v2, v3, and proto formats
  from documentation and libvideo-metadata source. Documented binary structures,
  fixed-point encoding, extension mechanism, and field categories. Created
  [[parrot-metadata-formats]] concept, [[parrot-video-metadata-docs]] and
  [[libvideo-metadata]] references.
* **Field mapping analysis completed**: Compared Parrot telemetry fields against
  ST 0601.19 tags in libmisbklv's registry. Identified 27 currently implemented
  tags, mapped Parrot fields to existing tags (Phase 1 viable with current impl),
  and documented high-priority missing tags (IMAPB extended items: 92-93, 103,
  106-108, 112-114) for Phase 2+. Created [[field-mapping-analysis]] with 3-phase
  implementation plan. Phase 0 Foundation tasks 1-3 complete.
* **Decision (accepted)**: ADR 0001 — Use direct MP4 box parsing for metadata
  extraction rather than GStreamer qtdemux. Investigation showed qtdemux doesn't
  expose metadata tracks as pads. Simple sequential parsing clearer for Phase 1.
  Will use lightweight library (minimp4) or custom minimal reader.
* **minimp4 vendored**: Added single-header MP4 parser (commit 4575afb, ~3.5K LOC,
  Unlicense/public domain) to `external/minimp4/`. Updated CMakeLists.txt to
  include vendored header. Standard practice for single-header libs (no
  build-time fetch, clear provenance via README).
* **Phase 1 started — MP4 extractor WIP**: Created `Mp4MetadataExtractor` class
  with minimp4 integration (pimpl pattern). Implements track finding, sample
  extraction API. Build integration with local libmisbklv via CMAKE_PREFIX_PATH.
  Status: Compiles with API corrections needed (minimp4 API differs from initial
  assumptions). Next: Fix API usage, test extraction with P0330036.MP4.
