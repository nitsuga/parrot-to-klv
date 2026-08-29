# Knowledge Bundle Log

## 2026-08-28

* **Bumped the libmisbklv pin for live terminal status:** moved the exact
  `GIT_TAG` from `b8ad143` to upstream `08f65de`, which adds the nonblocking
  `KlvSink::poll()` API consumed by issue #94. This pin change is kept separate
  from the parrot integration commit.

* **Returned live pipeline terminal failures for issue #94:** libmisbklv now
  exposes `Inserter::poll()`/`KlvSink::poll()`; the GStreamer backend observes
  EOS/ERROR without blocking and retains a consumed terminal result for a later
  `close()`. `live_ingest` polls that surface on its drive loop, returns
  `Error::Backend` before telemetry can arrive when the pipeline fails, stops
  cleanly on EOS, and preserves the timed dummy path plus explicit indefinite
  mode.
  The focused no-telemetry failure regression passed, as did the debug and
  sanitizer suites.

* **Dropped the live-ingest EOS stop path (issue #94):** upstream narrowed
  `poll()` to failure-only on review, because a clean EOS is unreachable before
  `close()` — `mpegtsmux` is a `GstAggregator` and forwards EOS only once every
  sink pad has it, and the KLV appsrc pad is EOS'd only by `finish()`
  (libmisbklv ADR 0035). `live_ingest` therefore drops `pipeline_ended` and the
  two branches that set it, keeping the failure check issue #94 needed. Done
  before the pin move so each commit builds: this change still compiles against
  the old `Result<bool>` pin, which the pin bump alone would not have.

* **Repinned to the accepted terminal-status contract (issue #94):** moved the
  exact `GIT_TAG` from `08f65de` to `2e1c232`, the merge of libmisbklv#59,
  where `poll()` returns `Result<std::monostate>` under the accepted ADR 0035.

* **Returned generated-KLV parse failures for issue #95:** `try_decode_and_emit`
  and file-to-live replay now return `Message::parse` errors for KLV produced by
  `to_0601`, instead of treating an internal generated-packet invariant failure
  as absent telemetry. Normal Parrot decode misses and truncated proto
  coalescing remain skips; the existing callers already propagate the returned
  error out of `live_ingest` and `replay_file_to_live`. The debug and sanitizer
  labeled suites pass, including the focused live RTP test.

* **Made MP4 track pairing fail closed for issue #87:** `Mp4MetadataExtractor::open()`
  now rejects disagreement between the first-`moov` box scan and minimp4's
  track count, then checks every indexed handler type before trusting the
  metadata index. A synthetic fixture with a foreign top-level `trak`
  reproduces the old selected-`meta`/`meta` false match and proves the file is
  refused before extraction or `--zero-media` can use shifted indices. Pairing
  by track ID is unavailable because minimp4's public `MP4D_track_t` does not
  expose `tkhd` IDs and the vendored parser is not patched for this purpose;
  count plus full handler order is the available fail-closed invariant. The
  resulting rejection of otherwise usable files with a divergent second
  `moov` is deliberate and documented as the security trade-off.

* **Returned live KLV emission failures for issue #86:** `try_decode_and_emit`
  now carries `KlvSink::emit()`'s Result to both continuous-drive call sites,
  so a runtime sink rejection exits `live_ingest` with the original error
  instead of being treated as a skipped packet and falling through to timed
  close. `sink.error()` remains open-time only; pipeline bus EOS/ERROR
  observation remains the documented gap. Debug and sanitizer builds pass,
  as does the focused live RTP test.

* **Scoped issue #86 after review:** PR #93 covers the runtime
  `KlvSink::emit()` failure path. No regression test was added because
  `live_ingest` constructs `KlvSink` directly and the available real GStreamer
  failure path (`file:/dev/full`) is reported by `close()`, so that harness
  passes on the unfixed code. The no-telemetry bus EOS/ERROR path is tracked in
  #94; generated-KLV parse failures are tracked in #95.

* **Made Item 2 session anchoring fail closed for issue #85:** pre-epoch media
  dates are rejected before unsigned timestamp arithmetic, and `©day` accepts
  ISO 8601 extended, basic, and hour-only UTC offsets. Synthetic coverage pins
  both the MP4 path and the resulting non-anchor.

* **Made absent v3 values explicit for issue #84:** normalization now leaves
  zero-sentinel automation targets and missing per-frame timestamps absent;
  session anchoring refuses to invent Item 2 for a frame without its clock,
  while an explicitly present zero timestamp remains valid. A synthetic MP4
  fixture exercises the parser, normalization, anchoring, and coverage path.

* **Corrected #84's coverage reason:** a v3 frame without its timestamp now
  reports Item 2 as `SourceAbsent`, while a timestamped frame that cannot be
  anchored is distinguished from a missing session date. A mixed synthetic
  recording pins the deliberate all-or-nothing behavior when the first frame
  has no clock.

* **Named the missing v3 anchor origin in #84 coverage:** when a timestamped
  frame has parsed session metadata but the recording's first frame had no
  clock, Item 2 now reports `NoAnchorOrigin` instead of blaming session data.
  The mixed fixture and coverage assertion pin the distinction.

* **Closed the oversized automation tail found in review of issue #83:** v3
  automation extensions still rewrite their known 28-byte prefix, but any
  surplus fields now go through the same unknown-extension reporting and
  clearing path. A synthetic 40-byte automation fixture covers the third
  pass-through shape; no real footage values were used.

* **Closed scrubber pass-through paths in issue #83:** `parrot-scrub` now
  records and clears short automation payloads and the present bytes of
  truncated extension tails, using one named automation payload-size constant
  in both parser and scrubber. Synthetic fixtures cover both malformed cases;
  no real footage values were used.

* **Applied the remaining mechanical over-engineering audit cleanup.** C++20 `std::string::starts_with` replaces three private prefix helpers; the live RTP capture queue has one bounded insertion path instead of three copies; coverage reports share their identical item-label formatter while keeping their simple, differently shaped width scans inline; skipped extension IDs use `std::snprintf` like the project's other hexadecimal diagnostics; the unused `default` configure/build preset aliases are removed; and the deprecated `gst_element_class_set_details_simple` call is removed because GStreamer's compatibility header aliases it to the identical `gst_element_class_set_metadata` call immediately following it. No behavior changes.

* **Collapsed four duplicated blocks found by an over-engineering audit (#81).** No behavior change; 112 net lines removed. `live_ingest` had written its indefinite and timed drive loops twice with byte-identical drain/dummy-emit/close bodies, differing only in the loop bound — one loop with an optional deadline now covers both. Merging exposed that the dummy-emit block in the indefinite branch was already unreachable: it was guarded by `cfg.run_duration.has_value()`, false by construction in that branch. The guard is preserved, so dummy emission stays gated to timed runs. `file_replay` had the same shape, finite (`test_repeat_limit`) and infinite repeat loops sharing a body; the merged loop increments its counter only on finite runs, since an indefinite run would otherwise overflow the signed counter after `INT_MAX` passes (review catch — the original `while` had no counter). `session_metadata` and `parrot_scrub` each carried a private in-memory MP4 `read_box`; both now call one `read_mp4_box` in `mp4_box_scan`, which keeps `session_metadata`'s wrap-safe `size > limit - pos` bound over `parrot_scrub`'s `pos + size > limit` — the latter can wrap for a hostile 64-bit largesize near `UINT64_MAX`, so the scrub path gains a tighter check than it had. In CI, the build and sanitizers jobs repeated their apt-install step verbatim apart from `pkg-config`; it is now a composite action under `.github/actions/install-build-deps` taking an `extra-packages` input, with the GStreamer plugin rationale moved into it rather than dropped. Release build clean and 38/38 green, sanitizer build clean and 36/36 green under ASan/UBSan.

## 2026-08-26

* **Released `v0.3.0`: native arm64 joins x86-64 as a first-class release target.** This is a minor bump because a new class of machines can now use the prebuilt deliverables, while the CLI and output formats remain unchanged. Each architecture gets its own relocatable bundle and exported container image, built and smoke-tested on a native runner; filenames, inner image archives, and image tags use the `uname -m` spellings `x86_64` / `aarch64`. Both builds share the digest-pinned Ubuntu 22.04 OCI index on Ubuntu 24.04 compute, preserving the GLIBC 2.35 floor without the retiring `ubuntu-22.04` runner labels. The release also carries the bundle SONAME scan's SIGPIPE fix. Pre-release evidence is the all-green native matrix [run 32940718745](https://github.com/nitsuga/parrot-to-klv/actions/runs/32940718745); the `v0.3.0` tag drives the same matrix and publishes all eight release assets — four archives and four checksums — atomically after both architectures pass.

* **release: make bundle SONAME discovery `pipefail`-safe.** The post-review native matrix run exposed a pre-existing x86-64 flake in `make-bundle.sh`: after a slow but successful dependency install, bundle assembly exited 141 immediately after scanner discovery while the arm64 leg completed. The closure loop used `objdump -p | awk '... exit'`; for a shared object with enough remaining program-header output, awk's early exit closes the pipe, objdump receives SIGPIPE, and `set -o pipefail` turns successful SONAME discovery into a failed release. The awk program now records the first SONAME while consuming the rest of objdump's output. This changes no copied filename; it removes an enumeration-order/output-size-dependent abort.

* **release review: make the multi-architecture contract explicit end to end.** The pinned Jammy reference in `release.yml` now identifies itself as an OCI index digest so a future refresh does not accidentally substitute a host-specific manifest; ADR 0017 records that one failed architecture deliberately blocks the whole publish while all matrix legs still report. The exported image archive now carries `x86_64` or `aarch64` inside its deliverable directory as well as in the outer release asset and loaded image tag, avoiding a collision when archives are copied side by side. Review also restored both amended ADRs' original `generated.by` attribution and moved implementation results out of ADR 0017's decision-time assumptions: the assumptions remain visible, with this dated log retaining the run links and evidence that resolved them.

* **release evidence: ADR 0017's full native x86-64/arm64 matrix is green** ([run 32932948339](https://github.com/nitsuga/parrot-to-klv/actions/runs/32932948339)). Both pinned Jammy build containers compiled natively, stayed at or below the GLIBC 2.35 symbol floor, assembled the bundle and exported image, and uploaded architecture-qualified artifacts; both native 24.04 smoke legs converted the fixture, resolved every live plugin closure and replayed to UDP, while both image legs loaded their own architecture-qualified tag, converted as the caller and exited non-interactively. The downloaded `bundle-x86_64` and `bundle-aarch64` artifact sets passed their SHA-256 manifests, and their generated launchers/usage name `parrot-to-klv:bbf0fe0-x86_64` and `parrot-to-klv:bbf0fe0-aarch64` respectively. This closes the two implementation-time unknowns in ADR 0017: the code has run on arm64, and hosted arm64 job containers can reach the runner's Docker daemon for this pipeline.

* **release: add the native arm64 deliverable matrix after the x86 container stage passed** ([ADR 0017](./decisions/0017-multi-architecture-release.md)). The isolated x86-64 container migration completed build, the GLIBC symbol check, bundle/image assembly and both smoke jobs in `workflow_dispatch` run [32932383100](https://github.com/nitsuga/parrot-to-klv/actions/runs/32932383100); only then did `release.yml` matrix `build`, `smoke` and `smoke-image` over `ubuntu-24.04`/`x86_64` and `ubuntu-24.04-arm`/`aarch64`. Artifact names, release filenames, generated usage and the exported image's internal tag all carry the `uname -m` architecture spelling; `publish` merges both bundle artifacts before attaching one release's assets. The architecture-qualified image tag closes the launcher-cache collision ADR 0017 identified. First native arm evidence is deliberately still pending the manual run of this commit.

* **release: move the x86-64 bundle build off the deprecating runner image and into the pinned Jammy job container — ADR 0017's first implementation stage.** `release.yml` now supplies compute with `ubuntu-24.04` while the build runs in the multi-architecture `ubuntu:22.04@sha256:2edbbc…c7dbc` image. A pre-checkout bootstrap installs Git and Git LFS because the stock image has neither and checkout's REST fallback cannot satisfy `lfs: true`; subsequent dependency installation runs as the container's root user, without `sudo`, and explicitly supplies `make` plus the Docker client that talks to the host daemon through the socket GitHub mounts into job containers. Inspection of that exact base found no installed `libunwind-*-dev`, so the runner-image-only removal workaround is deliberately gone rather than surviving as a silent `|| true` no-op. Manual artifact versions come directly from `GITHUB_SHA` rather than asking Git to trust the host-owned workspace. The x86-only shape is intentional and temporary: this commit must pass `workflow_dispatch` before the arm64 matrix is added, keeping a failed run attributable to one environmental change.

* **Fork 15 decided: arm64 ships beside x86-64, and the glibc floor moves off the runner label that carries it** ([ADR 0017](./decisions/0017-multi-architecture-release.md), #75). Two clocks resolved as one decision, because `ubuntu-22.04` and `ubuntu-22.04-arm` both begin deprecation on 2026-09-17 and the arm leg would otherwise have been written against a label already scheduled for removal. Not yet implemented.

## 2026-08-25

* **The withdrawn Parrot snapshot is out of this repository's git history; what remains is GitHub's to clear** ([ADR 0016](./decisions/0016-references-licensing-withdrawal.md)). The withdrawal in #71 removed the files at the tip, which a review round had already established was not the same as removing them from history. That remediation is now done. Sequence, in order: libmisbklv went public and its own `references/` provenance gap was closed first ([libmisbklv#44](https://github.com/nitsuga/libmisbklv/pull/44) — the six MISB standards there are U.S. Federal Government works under 17 U.S.C. § 105, so that repository needed a record rather than a withdrawal); this repository's `FetchContent` moved to anonymous HTTPS and the deploy key was retired (#73); every open PR was merged, leaving `main` the only branch after six fully-merged branches were deleted; and a full mirror backup was taken — 262 commits, 62 refs, 20 LFS objects, `git fsck --strict` clean — at `/home/eric/backups/parrot-to-klv-prerewrite-20260825`, with its `origin` remote removed afterward so it can never push. **The rewrite**: `git filter-repo --invert-paths` over the HTML file and its entire `_files/` directory, run on a fresh mirror with `refs/pull/*` dropped first, then force-pushed. `main` went `2493f62` → `3b23aab` and all eleven release tags took new commit identifiers (`v0.2.3` → `8a432a98`). All 232 commits survived — none became empty — and the rewritten tip tree is `6f44c5e9`, byte-identical to the tree it replaced, so working content did not move. Verification from a fresh clone: zero withdrawn objects reachable through any branch or tag; LFS pointers intact across history (the 20 `.mp4` fixtures unaffected); `git fsck` clean; and of the 46 SHA-like strings in commit messages, none resolved to an own-repo commit orphaned by the rewrite, confirming the earlier blast-radius finding. Local clones were then cleaned — stale branches deleted, reflogs expired, `gc --prune=now` — including a forgotten opencode worktree under `/tmp` that was pinning the old objects; the working clone now reaches none of them. **Still outstanding, and not fixable by pushing:** all 44 `refs/pull/*` refs still resolve to pre-rewrite commits and still reach all 16 withdrawn paths — `hud.jpg` reads back at its full 516,174 bytes through them. A support request is drafted at `/home/eric/work/rewrite/github-support-request.md` asking GitHub to purge those refs, the cached objects, and the generated source archives for the rewritten tags, while keeping the pull request conversations. Visibility does not change until that purge is confirmed and re-verified.

* **chore: libmisbklv is public, so CI fetches it over anonymous HTTPS and the deploy key is retired.** `FetchContent`'s `GIT_REPOSITORY` moves from `git@github.com:nitsuga/libmisbklv.git` to `https://github.com/nitsuga/libmisbklv.git`; the exact-commit pin is unchanged, and bumping it stays its own commit. The three "Authorize the private dependency" steps — two in `build.yml` (build, sanitizers), one in `release.yml` — are deleted along with the `MISBKLV_DEPLOY_KEY` secret they read and the read-only deploy key on libmisbklv they authenticated with. **Neither workflow now reads any secret**, which also means a fork's pull request builds with no setup, something the private dependency made impossible. Verified before pushing: an anonymous clone with `GIT_SSH_COMMAND=/bin/false` and an emptied credential helper reaches the repository and fetches the pinned commit `b8ad143`, so nothing was relying on ambient SSH credentials. `.github/workflows/README-ci.md` is rewritten around the current state and keeps the retired setup, and the reasons the PAT and submodule alternatives were rejected, on the record. [ADR 0009](./decisions/0009-release-bundle.md) § Context still says building requires "a deploy key for a private dependency": left as written, because an ADR records what was true when the decision was taken and is not restated as circumstances move. libmisbklv keeps a second deploy key for `dji-to-klv`, which is a different repository's business and untouched.

<!-- Durable chronological record, newest first, ISO YYYY-MM-DD day headings.
     One thin line per decision (chronology + link — the ADR owns the rationale);
     a fuller entry per significant implementation. Routine ingests/lint go here
     too. This is the ONLY home for history — PROGRESS.md is present-tense only. -->

## 2026-08-24

* **docs: one security rule across both repositories, scoped to the data it protects.** This repo's `AGENTS.md` carried a security bullet naming "embedded GPS coordinates"; libmisbklv carried **none** — two sibling repositories holding different standards for the same class of data, found during the pre-public audit. Both now carry the same rule, widened from "coordinates" to what actually identifies a flight (coordinates, serials, absolute times, operator or device identity) across code, tests, fixtures, commit messages, documentation and agent memory, with fixtures stated as synthesized rather than derived and `parrot-scrub`/`parrot-conform` named as the supported route from a real recording to shareable evidence. **The rule is scoped to recordings the project holds and anything derived from them.** A first draft was written broadly enough to cover published facts about third-party media the project does *not* hold, and then needed a carve-out to exempt libmisbklv's `context/data-samples.md`; that shape was rejected, because the carve-out only undid an overreach one step earlier and a conditioned rule invites argument at the margin. Correctly scoped, bibliography was never in scope and no exception is needed. Redacting `data-samples.md` was also considered and rejected: it describes third-party public sample files this project never held, whose coordinates resolve to landmarks and whose tail number is public registry data, and it is the artifact showing that media was identified and removed deliberately — stripping its evidence would leave an unsupported assertion in place of a defensible record. Reaching the copies behind it would have required a `filter-repo` rewrite invalidating every libmisbklv SHA, including this repo's `CMakeLists.txt` `GIT_TAG` pin. No ADR: this follows the agent-authorship alignment earlier today, where a cross-repo working-rule sync landed as a log entry rather than a decision.

* **Withdrew the Parrot documentation snapshot from `references/`; the schema is now vendored under BSD-3-Clause** ([ADR 0016](./decisions/0016-references-licensing-withdrawal.md), #70): a pre-public audit found `references/Embedded Video Metadata - 8.4.2.html` and its `_files/` tree (16 tracked files, 936 KB) to be a verbatim export of documentation the live page footers as "Copyright © 2018-2026, Parrot", with no redistribution grant — and the asset tree carried Parrot's two trademarked logo SVGs plus a 516 KB product screenshot, which was the sharper exposure. Replaced by `references/parrot-video-metadata.md` (a link file: URL, version 8.4.2 and date observed, terms, pinned replacements) and `references/vmeta.proto`, Parrot's schema vendored from `Parrot-Developers/libvideo-metadata` `proto/vmeta.proto` at `22f715a` with its BSD-3-Clause notice intact. A bare link was rejected as the replacement: the doc URL is unversioned, so it would have discarded the version pinning `references/` exists to provide, where a commit SHA does not move. Citations repointed in `parrot-video-metadata-docs`, `field-mapping-analysis`, `libvideo-metadata`, `index`, ADRs 0003/0007/0013, and `test/vmeta_subset.proto`. **The append-only rule was amended in the same commit** — `CONVENTIONS.md` § Source discipline, `references/README.md` and `AGENTS.md` (both mentions) now carry one narrow withdrawal exception conditioned on a link file, pinned replacements, and an ADR; deleting under a rule that forbids deletion, explained only in a commit message, is the contradiction the amendment avoids. **Verification, and a bonus:** the vendored schema was diffed against `test/vmeta_subset.proto` — all 19 transcribed messages/enums present upstream, every field number agreeing, and 53 messages+enums total against the 35+18 ADR 0003 counted in the 8.4.2 export, so the pin has not drifted. ADR 0003 had recorded that transcription as never diffed against an original; it now has been. Corrected on review (Codex, `openai/gpt-5`, PR #71): the ADR first claimed the withdrawal let the repository go public, which is false — deleting at the tip leaves the HTML, both logo SVGs and `hud.jpg` readable from every ancestral commit and all eleven release tags. ADR 0016, PROGRESS and ROADMAP now say the *source-policy* question is settled and the default tree is clean, while publication stays blocked pending history remediation — a full clone still receives the blobs through reachable ancestry, the eleven release tags and the 45 `refs/pull/*` refs, so redistribution is only partially remediated until a rewrite plus a GitHub Support purge lands. A second review pass (same reviewer) rejected the first correction's wording — "ongoing redistribution stops", "no future clone of `main` receives them" and "closed at the tip" all overstate what deleting at the tip achieves, since a clone takes the whole reachable ancestry; verified with `git rev-list --objects`. The same review also caught that arm64 hosted runners reached private repositories in January 2026, free-tier eligible, so the multi-architecture work never depended on going public at all; fork 15 is reframed as cost and resources rather than availability. Also removed a stale `/home/schneiderer/...` local-clone path from `libvideo-metadata.md` — the only such leak across this repo and libmisbklv. What is genuinely lost is the documentation's *prose*: fixed-point encodings, field semantics, the extension narrative, and the "Integration in the RTP stream" section ADR 0013 cites; this project's reading survives in `parrot-metadata-formats.md`, but re-checking it now means fetching a live URL at whatever version it serves.

* **Released `v0.2.3`:** patch bump (`0.2.2`→`0.2.3` in `CMakeLists.txt project(VERSION)`) for the libmisbklv live-insert teardown pin (`b8ad143`, #68) — the only user-facing change on `main` since `v0.2.2`; the other two commits are CI preset wiring and an authorship-wording doc sync. `CHANGELOG.md` gains the `## v0.2.3` section `release.yml` publishes, describing the residual crash in user terms (~1 in 60 live sessions under stress, gone) rather than in pad-lifecycle terms. Release evidence: 560 clean live runs against the fetched pin (interactive witness `live_rtp` 30/30, `live_video` 30/30, full suite 66/66; then a detached 250+250 `MALLOC_CHECK_=3`/`MALLOC_PERTURB_=165` campaign at `0/500` failures, 0 core-file delta, configured *without* `-DFETCHCONTENT_SOURCE_DIR_MISBKLV` and aborting unless the fetched HEAD matched `b8ad143`). 500 runs is what makes the sweep mean something: at the #57 baselines (5.2% `MALLOC_CHECK_`, 1.6% `MALLOC_PERTURB_`) it puts the upper 95% bound on any residual rate near 0.6%, where the 30-run witness alone was ~38% powered. Tag `v0.2.3` triggers the bundle+image build, smoke test, and GitHub Release publish. Output formats and CLI behavior are unchanged.

* **chore: pin libmisbklv's live-insert teardown fix; #57 closed** (libmisbklv#40, merged
  `c1ed8d3`): the residual that survived the SEI-probe fix is attributed and gone.
  `GstInserter::finish()` was calling `gst_element_release_request_pad()` on the `mpegtsmux` pad
  while the pipeline was still `PLAYING`, so the muxer's streaming thread could traverse a freed
  request-pad-list node — the `g_list_last → libgstmpegtsmux → GstInserter::finish` core
  (`751496`) this repo's D3 discriminator produced. Upstream now keeps the pad linked and ends an
  unbounded live branch by pushing EOS through it, draining normally (libmisbklv ADR 0034);
  upstream verification `0/500` against baseline `5/500`. Evidence the crash seen here was that
  residual and not something new: the last CI segfault (run 32588593672, `0fa752a`) ran *after*
  `d3d60dd` had already bumped to the #38 pin. #57 closed on root cause; its OpenH264
  encoder-init hypothesis was never confirmed by any core and is recorded as unproven rather than
  fixed. D2's finding — no KLV produced under `x264enc`, 250/250 deterministic — split out to #67
  so it is not closed by implication.

* **docs: one agent-authorship spelling; CONVENTIONS drops the single-actor claim**: the rule
  already required an agent to sign PR/issue/comment prose, but mandated a `<provider>/<model>`
  spelling that disagreed with the bundle's own `generated.by` actor form — the repo carried two
  spellings for one concept. Both are now the same pair: `<producer>/<model>`, the vendor *or*
  harness that generated the text plus the model, which `claude/opus-5` and
  `opencode-go/muse-spark-1.2-contributor` already satisfy without renaming an agent or splitting
  a model string. Considered and rejected exempting harness-distinct agents: it would let one
  agent be spelled two ways in the same commit, which is the failure being fixed. Applies going
  forward; the mixed formats already in PR history stay as they are. CONVENTIONS § Actors no
  longer claims every `generated.by` is `claude/opus-5` — ADR 0012 is `claude/sonnet-5`. Ported
  from the sibling libmisbklv `AGENTS.md`, which grew the same rule while reviewing libmisbklv#40.

## 2026-08-22

* **build: CMakePresets.json v6→v3; wire build.yml/release.yml to `cmake --preset`**: `release.yml`'s `build` job is pinned to `ubuntu-22.04` (glibc floor), whose apt `cmake` is 3.22.1 — too old for the v6 schema (needs ≥3.25). Downgraded `CMakePresets.json` to v3 (`cmakeMinimumRequired` 3.21, satisfied by 3.22.1) and switched all three CI jobs to `cmake --preset`: `build.yml`'s `build` job uses `release` (`build/release`), its `sanitizers` job uses `sanitize` (`build/sanitize`), and `release.yml`'s `build` job uses `release-bundle` (`build/bundle`) — updating the hardcoded `build-release` path references (the glibc-floor `objdump` check, `make-bundle.sh`'s argument) to match. README.md, AGENTS.md, `test/conformance/README.md`, and `context/mp4-extraction.md` updated to drop stale v6/"maps to"/"mirrors" language now that CI uses these presets directly rather than parallel ad hoc flags.

* **build: add CMakePresets.json (v6) with preset matrix under build/\***: `CMakePresets.json` v6 adds a hidden `base` configure preset (shared defaults via `inherits`) and four variant presets under `build/*` — `release`/`default` → `build/release` (Release), `debug` → `build/debug` (Debug), `sanitize` → `build/sanitize` (Debug + `PARROT_SANITIZE=ON`, maps to CI `build-sanitizers` / `ctest -L parrot-sanitize`), `release-bundle` → `build/bundle` (Release + `PARROT_BUNDLE=ON`, mirrors `release.yml` bundle). Build presets share a hidden `base` with `jobs=6` (`nproc 7 → 6`, `nproc-1` headroom) so every `cmake --build --preset <preset>` builds in parallel without per-preset duplication. Manual `cmake -S . -B build` remains as fallback.
* **release: v0.2.2 — libmisbklv SEI-probe teardown pin + corrected #57 root cause**: bumped the libmisbklv pin `2529627` → `11bc55a` (upstream #38), which closes the use-after-free during teardown of a live RTSP → KLV session under `Sei0604::Generate` — the SEI pad probes hold a raw `VideoCtx*` and could fire after it was freed (Valgrind memcheck: 50 invalid accesses pre-fix → 0 post-fix). This **corrects the earlier #57 conclusion in this log** (the 2026-08-22 `_Exit` entry, which held the abort was "a data race, not a heap bug — no invalid access anywhere in our code"): there *was* a real use-after-free in the media backend's SEI path, now fixed upstream; the harness `_Exit` only lowered the probability. A separate, still-unattributed `tcache_thread_shutdown` heap-corruption residual persists at ~baseline rate during Generate live teardown (captured cores carry no project/GStreamer/plugin frames at fault time; candidates include upstream encoder/GStreamer teardown races) and remains open on #57 — out of scope for the pinned fix. v0.2.2 also ships the two other user-facing changes already on `main` since v0.2.1: the release-bundle live-path plugin fix (#62) and the Ctrl-C replay-drain interrupt (#63). CHANGELOG `## v0.2.2` covers all three; version bumped `0.2.1` → `0.2.2`.
* **fix: file→live replay honors Ctrl-C during the video drain tail (#63)**: `replay_file_to_live` now owns a small `InterruptBridge` — a watcher thread forwarding the async-signal-safe SIGINT flag to a `std::stop_source` whose token every `KlvSink::close()` call receives (upstream libmisbklv ADR 0032 added the cancellable `close(stop)`; this repo's half of the cross-repo fix). Previously the emission loop polled the flag but the final `close()` drained the video branch at wall-clock speed, ignoring any interrupt for the remaining video duration. Regression coverage: new `parrot_v3_video_tail.mp4` fixture (6 s real H.264 vs ~0.27 s of metadata — the shape that makes realtime replay dangerous) generated by `make_fixture.py` (`encode_h264` gains an optional `crf` to keep its committed bytes at ~110 KB); `test_interrupt_during_drain_tail` replays it through the unmodified production path, interrupts 1 s in, and asserts prompt join — verified to fail without the fix (4971 ms vs 11 ms with it). A test-only `reset_mux_interrupt_for_test()` lets both interrupt scenarios run in one suite (the flag has no production reset). libmisbklv pin bumped `62ea5e1` → `2529627` (cancellable close + Generate encoder-shift fixes, their ADRs 0032–0033).
* **chore: track binary fixtures in Git LFS**: `*.mp4` now maps to LFS via `.gitattributes` (`filter=lfs diff=lfs merge=lfs -text`), and all existing fixtures were renormalized into it in one forward-only commit — no history rewrite, so open PRs and the `v0.2.x` tags keep their SHAs; the pre-LFS blobs (~130 KB total) simply stay in history. Forward-only was chosen over `git lfs migrate import --everything` for exactly that reason. All CI checkouts that read fixture contents (`build.yml` ×2, `release.yml` bundle job, `link-check.yml`) pass `lfs: true` to `actions/checkout`, since a bare checkout smudges pointer files instead of media. The CHANGELOG-only checkout in `release.yml` deliberately stays bare.

## 2026-08-21

* **Fixed the release bundle omitting every live-path GStreamer plugin:** `packaging/make-bundle.sh`'s `PLUGINS` list was derived only from the offline file→file conversion path (`filesrc→qtdemux→h264parse→mpegtsmux→filesink` + the `appsrc` KLV branch), so it shipped six plugins. The live/replay paths added in `v0.2.0`/`v0.2.1` (ADR 0013 live streaming, ADR 0015 file-replay to UDP/SRT) instantiate `udpsink`/`srtsink` (output) and `rtspsrc`/`rtph264depay`/`rtph265depay`/`rtpjitterbuffer` (RTSP input), none of which were packaged. Symptom on a real recording: the bundle **wrapper** (`./parrot-to-klv -i file.mp4 -o udp://…`) failed with `file replay failed: 7` (libmisbklv's honest "udpsink factory unavailable" — `gst_insert.cpp:70`), while the **direct** `./bin/parrot-to-klv` silently succeeded by borrowing the host's plugins (the wrapper *replaces* `GST_PLUGIN_SYSTEM_PATH` and unsets `GST_PLUGIN_PATH`, so only bundled plugins are visible). On a clean machine — the bundle's whole point (ADR 0009) — both would fail. Added `udp srt rtsp rtp rtpmanager` to `PLUGINS` (all LGPL, per the list's rule; element→`.so` set traced from `gst_element_factory_make` in libmisbklv's gst backend); the `ldd` closure sweep pulls their transitive libs automatically (bundle 6→11 plugins, 42 libs). Root cause was the release smoke test in `release.yml` only exercising file→file, so the gap passed every green check — the `smoke` job now asserts every live-path plugin loads and that a live UDP path runs end to end: an `ldd`-under-the-bundle-lib check per plugin (`udp srt rtsp rtp rtpmanager`) catches the failure modes that actually bite (omitted, mis-named, or shipped without a transitive lib — a "not found" line) without needing a peer for `srtsink`/`rtspsrc`, plus a `file→UDP` replay to unicast loopback (`udp://127.0.0.1:…`, not multicast — udpsink loads from the bundle identically but multicast needs an outgoing `239.0.0.0/8` route the job's container may lack, which would fail a release for an environmental reason) that needs no listener and does one play-through then exits. Also reworded the drift note in `make-bundle.sh` to require the smoke test cover a live path. This is entirely a parrot-to-klv packaging fix; libmisbklv is unchanged (it correctly checks for and reports the missing factory). Verified by rebuilding the bundle and running the wrapper's file→UDP replay against real footage (exit 0); file→file unregressed; `srtsink`/`udpsink` confirmed resolving to the bundle's own `.so` under a host-plugin-free `env -i`.

* **Released `v0.2.1`:** patch bump (`0.2.0`→`0.2.1` in `CMakeLists.txt project(VERSION)`) for the libmisbklv performance refresh. `CHANGELOG.md` documents the lower-copy Generate and high-level streaming paths; tag `v0.2.1` triggers the existing bundle+image build, smoke test, and GitHub Release publish workflow. Output formats and CLI behavior are unchanged.

* **Updated the libmisbklv performance pin:** moved `GIT_TAG` from `4400a2135f6fe7a8ea3826deb874edeef9c2e5cc` to `62ea5e13862dd53f00a1a58d9c2adb2260a727b8`, pulling in zero-copy ST 0604 timestamp reads, latched H.264 negotiation, consumer-side timestamp-map retirement that preserves lagging-video support, and zero-copy ownership transfer through the high-level `KlvStream`/`KlvSink` path. User-facing formats and CLI behavior are unchanged; this is the dependency commit for the `v0.2.1` patch release. A fresh Release build against the fetched pin passed all 64 runnable CTest cases; the optional real-data conformance case was skipped because its external corpus was absent.

* **Released `v0.2.0`:** minor bump (`0.1.6`→`0.2.0` in `CMakeLists.txt project(VERSION)`) for the live-streaming surface — RTSP ingest, the unified `-i`/`-o` type-inferring CLI, and `file→UDP/SRT` replay (fork 13 / ADRs 0013/0014/0015) — plus the post-`v0.1.6` hardening batch (v3 decode validity #8/#9, strict `stsd` MIME parse #5, output-write reliability #11/#19/#17, sanitizer/packaging CI #16/#20/#21). `CHANGELOG.md` gains the `## v0.2.0` section `release.yml` publishes; tag `v0.2.0` pushed to trigger the bundle+image build, smoke test, and publish. `--live-rtsp`/`--live-udp` were added and removed within the unreleased window so are not a user-facing change vs. `v0.1.6`.

* **Doc scrub (drift):** ROADMAP no longer calls the ADR 0015 file→live replay "queued for implementation" (it landed in PR #58), and fork 13 has left the Open-forks list for the register per the lifecycle rule (decided + implemented across ADRs 0013/0014/0015). PROGRESS drops the stale "PR #39 ready to merge" (merged 2026-08-20) and frames the unreleased-on-`main` batch as the post-`v0.1.6` hardening plus the live-streaming surface.

* **Fixed `live_rtp` CI teardown-race flake ([issue #57](https://github.com/nitsuga/parrot-to-klv/issues/57)):** the intermittent `tcache_thread_shutdown(): unaligned tcache chunk detected` abort was a data race, not a heap bug — Valgrind memcheck (which serializes threads) finds no invalid access anywhere in our code, only benign GStreamer/codec uninitialised-stack noise. The still-winding-down x264/x265/OpenH264 encoder threads (and GLib thread-pool workers), which the test does not own and cannot join, race the main thread's glibc per-thread tcache teardown at process exit. The three prior fixes (#48/#49/#51: RAII joins + growing grace-sleeps) only reduced the probability. `test/live_rtp_test.cpp` `main()` now, once every result is decided, flushes output and `std::_Exit(code)` instead of returning — skipping the atexit/static-destructor/tcache teardown entirely, so the kernel reclaims the residual encoder threads without running their racy shutdown and the abort categorically cannot occur. Replaces the probabilistic 500 ms final grace-sleep with a hard guarantee; test-harness only, production `main()` untouched. `ctest -L parrot` 38/38 green.

* **File-replay review fixes (PR #58, ADR 0015):** `src/live/file_replay.{hpp,cpp}` now caches `std::vector<MetadataSample>` once via `extract_all_samples()` (origin/max/anchor/emission reuse it, no re-extraction per pass or per loop); `replay_file_to_live` uses `cfg.session` directly (caller `src/main.cpp` already parses once — removed duplicate `fopen`/`parse_session_metadata` fallback); `--repeat-pts continue` step now uses `track_info().duration_us` (full mett duration including tail, falling back to max-origin only if unavailable) so loop 2 starts cleanly past loop 1's last PTS instead of overlapping it. Added test seam `test_repeat_limit`/`test_disable_pacing`/per-pass `file:.passN` disambiguation and two hermetic multi-loop PTS tests in `test/file_replay_test.cpp` (`test_repeat_pts_reset` asserts both loops in `[0,dur]` with repeating span; `test_repeat_pts_continue` asserts monotonic `[0,dur]` then `[dur,2dur]` with `≈dur` delta, each via `KlvStream` read-back on `parrot_v3_video.mp4` with pacing disabled and run before the interrupt latch test which remains last). `ctest -L parrot` 38/38 green.

* **File → live-sink replay landed (ADR 0015):** `src/live/file_replay.{hpp,cpp}` builds a live `KlvSink{ sink=udp:/srt:/file:, realtime=true, video_source=file:<path>, sei_0604=Generate, udp_ttl/iface/loop }` (libmisbklv replays file video on the pipeline clock, ADR 0031 item 3); KLV from the `mett` track's `timestamp_us`, anchored zero-based as the file's running time per ADR 0021 → `set_pts` → `emit`, paced emission via interruptible sleep of `timestamp_us` deltas chunked to 10 ms, one play-through then `close()` and `--repeat` recreates the sink per iteration until SIGINT with `--repeat-pts reset|continue` controlling whether PTS resets each loop or continues monotonic (default `reset` — matches the per-loop video re-mux from PTS 0; `continue` would desync KLV from per-loop video until a single-mux implementation, see PR #58 review). `src/main.cpp` replaces the "not yet supported" gate for `path→udp:/srt:` with a call to `replay_file_to_live`, adds `--repeat`/`--repeat-pts` (only valid with `-i file.mp4 -o udp:/srt:` and `--repeat-pts` only with `--repeat`), and documents it in `print_usage`; `mux_to_ts` and `live_ingest` untouched. `CMakeLists.txt` adds `file_replay.cpp` to `parrot_live`, `test/file_replay_test.cpp` (hermetic file→file replay, pacing, repeat-interrupt) and `cli_rejects_repeat_without_file_live`/`cli_rejects_repeat_with_file_to_file`/`cli_rejects_repeat_pts_without_repeat`/`cli_rejects_repeat_pts_invalid_value`, removes `cli_rejects_file_to_live_sink`; `README.md` documents `-i file.mp4 -o udp://…` examples. `ctest -L parrot` 38/38 green.

* **Accepted file → live-sink replay (fork 13 — [ADR 0015](./decisions/0015-file-replay-live-output.md)):** the deferred `-i file.mp4 -o udp:/srt:` cell from ADR 0014 is now decided. `file:` video replayed by libmisbklv on the pipeline clock at `realtime` (ADR 0031 item 3); KLV from the `mett` track's `timestamp_us`, anchored zero-based as the file's running time per ADR 0021, so it interleaves with the video. Paced emission to the recording's timeline (bounds the muxer KLV backlog). One play-through then close (EOS) by default, with a `--repeat` flag to loop until SIGINT — named `--repeat`, not `--loop`, because `--loop` is already the multicast loopback toggle (ADR 0014). `mux_to_ts` (deterministic) and `live_ingest` (RTSP) are untouched; a new replay function reuses `mp4_metadata_extractor` + decoders + `to_0601` + `KlvSink`. Non-H.264 replays unstamped (ADR 0007). ADR accepted; implementation queued. Hermetic file→udp loopback with a KLV↔video drift check will gate it.

* **Unified -i/-o CLI with type inference (ADR 0014):** `src/main.cpp` now infers type from `-i`/`--input` (path | `rtsp://` | `rtsps://` | `pipeline:` gated by `PARROT_TEST_ALLOW_PIPELINE`) and `-o`/`--output` (path | `udp://`/`udp:` → normalized `udp:…` | `srt:`). Bare positional input kept for backward compat; `-i` without value and `-i`+positional both error ("more than one input file given"). Output `path→path` stays through deterministic `mux_to_ts` byte-identical; `rtsp/pipeline→{path|udp:/srt:}` routes to `live_ingest({rtsp_uri=input, udp_sink=translated, run_duration=nullopt, transform_options, udp_ttl/iface/loop})`; `path→udp:/srt:` emits "not yet supported"; `rtsp` with no `-o` emits "live input needs an output". UDP sink flags `--iface`/`--ttl` (0..255, "must be 0..255")/`--loop`/`--no-loop` only valid with `udp:` output; `--live-rtsp`/`--live-udp` now loud-removed ("have been removed. Pass the input and output instead: -i <rtsp://…> -o <path|udp://…>."). `print_usage` updated with `-i`/`-o` inference, udp flags, and live examples. A `udp:` output is now validated at the CLI for a `host:port` remainder (bare `udp:`/`udp://` → "udp output needs host:port"). `test/cli_test.cpp` gains `-i` happy-path v3 conversion; `CMakeLists.txt` adds seven `cli_reject_test.sh` cases (removed flags, udp-flag-without-udp for `--iface`/`--ttl`/`--loop`, file→live-sink, live-without-output, ttl out of range) on both `parrot`/`parrot-sanitize` labels, `<stdexcept>` included explicitly for the `--ttl` parse. `ctest -L parrot` all green.

* **Make live run_duration default finite (explicit indefinite opt-in) (ADR 0013 fix):** `LiveIngestConfig::run_duration` default changed from `std::nullopt` (indefinite hang-by-default) to timed `std::chrono::milliseconds(2000)` (safe for tests that forget to set a duration); `nullopt` remains explicit opt-in for indefinite live-unbounded until SIGINT. `src/live/live_ingest.hpp` doc updated to "default timed 2s for safety; set to nullopt for indefinite (explicit opt-in)". `live_ingest.cpp` logic unchanged (`!has_value()` → indefinite `while(!is_mux_interrupted())`, `has_value()` → timed deadline `now + *run_duration`). `src/main.cpp` retains `cfg.run_duration = std::nullopt` as explicit production opt-in for `--live-rtsp`. Hermetic tests already explicitly set `2000ms` and `test_indefinite_live_until_sigint` keeps explicit `nullopt`. No other `LiveIngestConfig` default-construction expects indefinite. `ctest -L parrot` 27 tests green.

* **Drive-until-SIGINT for live ingest (ADR 0013):** `LiveIngestConfig::run_duration==nullopt` now means indefinite live-unbounded (drive until `is_mux_interrupted()`/SIGINT — finite live sources must be stopped manually (EOS/ERROR from the live pipeline is not observed here; it will be surfaced via next emit() or close() if the mux fails, but the loop itself only exits on SIGINT or run_duration deadline)), not one-shot. For live-unbounded `rtsp://` or `pipeline:` without `num-buffers`, this is the production mode; `run_duration` with value remains timed for tests/hermetic (e.g., 2s). `live_ingest()`'s one-shot drain + 600ms poll is removed as a whole run — the continuous loop's first 10ms iteration subsumes the header-extension startup grace — and an indefinite `while(!is_mux_interrupted())` loop with 10ms sleep and 50ms dummy grace now handles the live case (`sink.error()` is only `open_error_` — runtime bus EOS/ERROR not observed, see header doc). `src/main.cpp` CLI now sets `run_duration=nullopt` (explicit opt-in, indefinite until SIGINT) for `--live-rtsp/--live-udp` without duration, documenting that Ctrl-C (SIGINT via `request_mux_interrupt`) breaks the loop and `close()`s. Hermetic `test/live_rtp_test.cpp` cases (`pipeline_hermetic`, `real_decode` v3/proto/split) now use explicit `run_duration=2s` so they stay bounded; new `test_indefinite_live_until_sigint` uses `run_duration=nullopt`, verifies the indefinite path exits via SIGINT (~500ms thread + `request_mux_interrupt` + join), and is documented as must-remain-last (latches global interrupt, no reset). Pipeline dummy now only for timed runs. H.265 `Generate` timed to 4s. `ctest -L parrot` 27 tests green, no 600ms dummy delay for pipeline.

## 2026-08-20

* **Live ingest continuous loop + Generate for live (ADR 0013):** `live_ingest()` now has a continuous mode gated by `LiveIngestConfig::run_duration` (0 = one-shot for backward compat, >0 = drive-until-deadline/interrupt). Continuous loop drains the FIFO (`parrot_extension_pop_next` → `try_decode_and_emit` with coalesce) every 10 ms, checks `is_mux_interrupted()` (SIGINT via `request_mux_interrupt`) and `sink.error()`, and emits dummy 3 KLV at 0.3 s spacing for `pipeline:` hermetic when no real telemetry arrives (RTSP still emits no dummy). `KlvSink` is now opened with `Sei0604::Generate` for live H.264 — libmisbklv pin bumped to `4ac014a` which wires the `h264parse` SEI probe for RTSP depay and pipeline bin ghost src pad (was `Preserve` gap). `parrot_live` now links `parrot_mux` for the shared interrupt flag, and `mux.hpp` exposes `is_mux_interrupted()`. `test/live_rtp_test.cpp` gains `test_continuous_live` (unbounded `videotestsrc is-live` 2 s run verifying KLV+video and late-arrival real decode drained mid-run). `ctest -L parrot` 27/27 green (13 s live_rtp). Bumped `FETCHCONTENT_SOURCE_DIR_MISBKLV` pin from `4a28859` to `4ac014a`.

* **Live ingest real decode (ADR 0013):** `ParrotRtpHeaderExtension::read` now captures buffer PTS (`GST_BUFFER_PTS`) alongside payload into FIFO bounded queue (`CapturedExtension{ payload, pts_ns }`, 128, drop oldest). `live_ingest()` drains the queue via `parrot_v3::parse` / `parrot_proto::parse` (proto split across first packets coalesced until `parse_proto` succeeds, truncated keeps buffering, 1 MiB cap) → `from_v3`/`from_proto` → `to_0601` with `TransformOptions`/`SessionMetadata` from `LiveIngestConfig` → `Message::set_pts(running_time)` → `KlvSink::emit`; live PTS is `running_time` per ADR 0013. When queue holds real extensions the dummy fallback is skipped; `pipeline:` hermetic path still emits 3 dummy packets when no extensions are present. Test helper `parrot_extension_push_for_test(payload, pts_ns)` added. `test/live_rtp_test.cpp` now verifies `read` PTS capture, FIFO order with PTS, and `test_real_decode` that feeds synthetic P3/proto bytes from fixtures via push helper, exercises `live_ingest` pipeline hermetic with real KLV, and checks PTS anchoring and proto split coalesce. `ctest -L parrot` 27/27 green including updated live_rtp test (9.0 s). One-shot startup poll only — live_ingest drains queue for ~600ms (+200ms on activity) after sink open then closes; continuous drive-until-EOS loop is remaining work for a working live path.

* **Live ingest wired to KlvSink — pipeline hermetic path (fork 13 — [ADR 0013](./decisions/0013-live-streaming-ingest.md))**: `live_ingest()` now validates `rtsp_uri`/`udp_sink`, registers the Parrot RTP header-extension, builds `InsertConfig{ sink, realtime=true, video_source=rtsp_uri, sei_0604=Preserve, udp_ttl/mcast_iface/loop }`, opens `KlvSink`, and pushes 3 dummy KLV packets (via `to_0601` from a synthetic `TelemetrySnapshot`) at PTS 1 s/2 s/3 s before `close()`. `pipeline:` video_source is fully functional (hermetic `videotestsrc ! x264enc/openh264enc ! h264parse` proved via `live_rtp_test` read-back of KLV and PTS); `rtsp://` is forwarded as `video_source` and fails gracefully with `Unsupported` within 5 s when unreachable, proving the RTSP URI + header-extension capture path is exercised. `test/live_rtp_test.cpp` gained `pipeline_hermetic` and `rtsp_unreachable` coverage. `ctest -L parrot` 27/27 green including the 6 s live_rtp test.

* **Accepted live ingest + multicast output (fork 13 — [ADR 0013](./decisions/0013-live-streaming-ingest.md))**: libmisbklv live surface (fork 28/ADR 0031) now accepted (pin 4a28859), so placement is accepted and implementation unblocked. Dependency was proposed; now it is the pin.

* **Proposed live ingest + multicast output, split by ownership** (fork 13 —
  [ADR 0013](./decisions/0013-live-streaming-ingest.md)): libmisbklv owns the
  generic live mux/pacing/multicast surface (their fork 28 / ADR 0031,
  proposed); parrot-to-klv owns the RTSP pull, the Parrot RTP header-extension
  capture, and the existing v3/proto decode + transform. Placement only; the
  "real-time streaming out of scope" roadmap line changes once implemented.

* **PR #39: the mux and CLI now run under sanitizers in CI.** Fixes issue #16.
  `build.yml`'s sanitizer job installs the runtime GStreamer plugin packages
  (`gstreamer1.0-plugins-base/good/bad`) alongside the `-dev` headers — the
  previous subset excluded the mux path, so no runtime plugins were needed —
  and builds every target rather than a hand-picked test list. `CMakeLists.txt`
  moved `mux`, `cli_converts_fixture`, and all the CLI `--help`/`--version`/
  rejection tests onto the shared `parrot;parrot-sanitize` label, so ASan+UBSan
  now instrument the output-writing and argument-parsing paths too.

* **PR #39: the conformance report's decode-failure path is pinned, and fixture
  reproducibility is documented.** Fixes issue #20. `conformance_test` gained
  `test_report_records_decode_failures`: against the fully-undecodable
  `parrot_v3_video_undecodable_metadata.mp4` fixture the report must complete
  with `ok` true, 8 samples / 0 decoded / 8 failed, exactly one recorded
  `decode_errors` type, and both `failed` and `decode_errors` present in the
  `to_json()` output the CLI prints — the JSON is the contract, not the struct.
  `make_fixture.py`'s docstring now states that metadata-only fixtures are
  byte-reproducible across environments while generated-video/audio fixtures
  depend on the ffmpeg/AAC encoder version.

* **PR #39: packaging guard boundaries hardened.** Fixes issue #21.
  `packaging_output_guard_test.sh` gained two cases:
  `staging_output_guard_validate` now refuses the staging directory itself
  (the one pathological target still passing next to the repo-relative and
  symlink refusals), and `staging_output_guard_reset` refuses a target whose
  tree contains a protected input — the sentinels prove the refusal deletes
  neither the output nor the protected input beneath it.

* **`parrot-sanitize` tightened to compiled C++ tests only (24, down from
  26).** `packaging_output_guard` and `conformance_real_data` are shell
  scripts, not CMake-compiled binaries, so ASan/UBSan instrumentation buys them
  nothing; they keep the plain `parrot` label and the sanitizer suite now means
  what it says — every instrumentable test, and nothing that cannot be
  instrumented.

* **Audio fixture regenerated under ffmpeg 6.1.1 and pinned to it.**
  `parrot_v3_video_audio.mp4` now carries bytes produced by ffmpeg
  `6.1.1-3ubuntu5` (sha256 `f993d7d9…`), verified byte-reproducible across
  repeated runs with that build; `make_fixture.py` records that any other
  ffmpeg version emits different AAC bytes, so a rebuild on another host can no
  longer silently swap the committed fixture.

* **Output writes are now checked end-to-end, and the CLI gains a true
  end-to-end test.** Fixes issues #11, #19, and #17 in one batch:
  - `parrot-scrub` and `parrot-conform`'s report writer switched from
    `std::ofstream` to `fwrite` + `fflush` + checked `fclose`, unlinking the
    half-written output on any failure; scrub additionally `fsync`s on
    Unix-like hosts so a reported-clean output is actually on disk — a privacy
    tool's "success" cannot leave a truncated file behind.
  - `mux_to_ts` preflights its destination before constructing the GStreamer
    sink: an output path that is a directory, whose parent does not exist, or
    whose parent is not a directory now fails with a specific message instead
    of libmisbklv's opaque "media pipeline failed" (issue #11). `mux_test`
    pins the nonexistent-parent diagnostic.
  - New `test/cli_test.cpp` runs the real `parrot-to-klv` binary against the v3
    and proto video fixtures and asserts exit 0, a non-empty output, the `KLVA`
    registration descriptor, and — for the v3 conversion — the
    `MISPmicrosectime` ST 0604 SEI (issue #19); inspection mode with disclosure
    flags exits 0. New `test/cli_reject_test.sh` replaces the bare-`WILL_FAIL`
    CLI rejections: each case must exit non-zero *and* print the expected
    diagnostic, so a rejection failing for the wrong reason is caught.
  - `mp4_extraction_test` gained `test_malformed_and_truncated_inputs` (issue
    #17): truncation at 16 boundaries, a `moov` box size declared past EOF, a
    truncated `mdat` payload, and a single-byte corruption sweep — all green
    under the sanitizer build, so a hostile file must not so much as tickle UB.

* **MIME type parameter strictly parsed for format detection.** Fixes issue #5.
  `format_from_mime` used to substring-match `com.parrot.videometadata`
  anywhere in the `stsd` string, so the magic string inside an unrelated
  parameter (a `comment=` or `other=` value) was enough to pick a format. It
  now splits on `;`, matches the `type=` parameter name case-insensitively,
  trims whitespace, accepts single- and double-quoted values, and requires an
  exact identifier match — `videometadata3` yes, `videometadata3_extra` no.
  The function is now declared in `mp4_metadata_extractor.hpp` and covered by
  a positive/negative matrix in `mp4_extraction_test.cpp`.

* **Short v3 extensions are recorded as skipped, and declared length is
  enforced exactly.** Fixes issue #9. `parse_v3` previously dropped a truncated
  timestamp/automation/thermal extension silently; each now reports through the
  new `skipped_extension_ids` list, and a sample whose declared length is
  *shorter* than its buffer is rejected (`declared != sample.size()`) instead
  of parsing the base structure and ignoring the trailing bytes.
  `parrot_v3_test` covers the three truncated-extension shapes and the
  under-long boundary case.

* **v3 decode rejects quaternions that are not unit norm.** Fixes issue #8.
  `parse_v3` now validates all three quaternions (drone, frame base, frame)
  against `|norm − 1| ≤ 0.01` and returns the new
  `ParrotV3Error::InvalidQuaternion` otherwise — an all-zero quaternion (norm
  0) previously decoded into garbage attitude with no error at all. Tests pin
  the accepted identity values and both sides of the tolerance boundary.

* **Proto and session-metadata assertions hardened.** Fixes issue #18. The
  proto fixture check now requires the tracking target itself, not just the
  tracking block, and `session_metadata_test`'s ISO 8601 cases check the
  `optional` before dereferencing it — a parse regression fails as "does not
  parse" instead of dereferencing an empty `optional` or quietly skipping the
  value check.

* **CI dependency installation gained an APT mirror fallback and
  interrupted-transaction recovery.** `build.yml`'s install steps now retry
  with bounded timeouts and `Acquire::Retries`, fall back from the default
  mirror to `archive.ubuntu.com` when the first attempt fails, and — after a
  timed-out install — run `dpkg --configure -a` plus `apt --fix-broken
  install` before retrying, so a runner interrupted mid-APT no longer poisons
  the next run with an unlocked dpkg state.

## 2026-08-19

* **`v0.1.6` published.** Tagged at `a0fa67d` after PRs #29–#33 and the
  automated-review-round fixes (#22–#26) merged. This is the first release to
  carry the whole review-round batch at once: strict/fail-closed CLIs (#13),
  extraction hardening against hostile/malformed input (#4, plus issues
  #27/#28 via the re-vendored `minimp4.h`, ADR 0012), transform truthfulness
  fixes (#6, #7), mux output integrity (#10), coverage aggregation (#15),
  `--scan` filesystem-error reporting (#14), and scrub identity/date (#12).
  One user-facing call-out in the release notes: a misspelled CLI flag that
  used to run to completion now exits non-zero, and an interrupted or
  zero-frame conversion leaves no half-written output.

* **`parrot-conform --scan` reports filesystem errors instead of silently
  absorbing them.** Fixes issue #14. Three unchecked `std::error_code`s in
  `conform_main.cpp`'s scan-mode loop: `fs::directory_iterator`'s
  construction error was ignored (a permission-denied scan directory
  produced a silently-empty listing, reported as "No recordings" rather
  than the real cause); the range-for loop's implicit `operator++()` throws
  on a mid-scan filesystem change, which was uncaught (an exception during
  iteration would have crashed the whole scan rather than reporting one bad
  entry); and `fs::file_size(p, ec)`'s error was ignored, so a vanished or
  unreadable file printed `static_cast<uintmax_t>(-1) / (1024*1024)` — a
  multi-terabyte garbage figure — instead of the real "No such file or
  directory", then still called `run_conformance()` on a file already known
  to be inaccessible. All three now check their `error_code` and report by
  name; the directory-listing and per-entry-iteration failures are now hard
  errors (nothing downstream can proceed without a real listing), while a
  per-file stat failure is reported and counted as a failure without
  stopping the rest of the scan.

  New `test/conform_stat_failure_test.sh`: a message-content check (a bare
  `add_test` + `WILL_FAIL` cannot discriminate here — both old and new code
  exit non-zero for a scan with one bad file among good ones, only the
  *message* differs), using a dangling symlink as a privilege-independent
  way to reproduce a vanished-file stat failure deterministically.
  Confirmed genuinely discriminant: fails against the pre-fix binary
  (missing the "cannot stat" message), passes against the fix.

* **Coverage reporting now aggregates across every frame instead of keeping
  only the first.** Fixes issue #15. `mux.cpp`'s `MuxReport::coverage` used
  to hold the first emitted frame's `CoverageReport` only (`if
  (report.frames_emitted == 0) report.coverage = ...`), and
  `parrot-to-klv`'s inspection mode (`--klv`) only ever computed/printed
  coverage for the first three samples shown — both despite `to_0601()`
  producing a fresh `CoverageReport` every frame that was simply discarded.
  Mid-recording drift (an attitude out-of-range sentinel appearing later, a
  thermal extension starting mid-flight, a field dropping in and out) was
  invisible in both surfaces, contradicting the "coverage is per-frame"
  claim in `context/transform-stage.md`.

  New `AggregatedCoverage` in `transform.{hpp,cpp}`: folds each frame's
  `CoverageReport` into per-tag emitted/omitted counts (the omitted side
  broken down by `OmissionReason`, since the reason can itself vary frame to
  frame) plus a union of dropped Parrot fields. `to_text()` calls out every
  tag that was sometimes emitted and sometimes omitted across the counted
  frames — the direct answer to "did the mapping change partway through this
  flight?" No `Disclosure` policy on its `to_text()`: a count carries no
  sensitive value the way a per-frame rendered value does.

  `mux_to_ts()` now accumulates every emitted frame's coverage.
  `decode_all()` in `main.cpp` now computes `to_0601()` for every decoded
  sample when `--klv` is passed (not just the first three shown in full) and
  prints the resulting aggregate once at the end. See
  [transform-stage](transform-stage.md) § Aggregated across a recording.

* **`parrot-scrub` and `parrot-conform` adopt `parrot-to-klv`'s option-parsing
  discipline: unknown flag, missing value, and extra positional are all
  errors now, never silently ignored.** Fixes issue #13. Scrub's `--lat`/
  `--lon`/`--alt` also gained strict parsing (`strtod` with a full-consumption
  check, rejecting `12abc`) and a finite-value requirement, closing a real
  UB path: `std::atof("nan")` reached `std::lround()` unchecked, and casting
  its NaN-domain-error result to `int32_t` is undefined behavior — for a
  privacy tool, a mistyped `--lat` used to either write garbage coordinate
  bits into every sample or (for a misspelled flag like `--zero_media`) exit
  0 having silently left the real location in place. `--lat`/`--lon` are now
  also range-checked ([-90, 90] / [-180, 180]). Conform additionally refuses
  `--label`/`--out` combined with `--scan` (they name one report; scan mode
  processes a directory and named each report from its own file hash while
  silently discarding `--label` under the old code). `parrot-to-klv`'s own
  `-o ""` no longer silently degrades to inspection mode. Nine new CLI-level
  regression tests (`scrub_rejects_*`, `conform_rejects_*` in
  `CMakeLists.txt`), each confirmed genuinely discriminant by rebuilding
  against the pre-fix source and watching every one wrongly exit 0 — three
  needed a specific argument *ordering* to actually exercise the bug rather
  than coincidentally erroring for an unrelated reason (an unknown flag
  given first was taken as the input path pre-fix, which then failed to
  open as a file either way).

* **Lint pass.** Found two items the ADR 0012 re-vendor left behind:
  `context/mp4-extraction.md`'s "minimp4 traps" section still described the
  `stsz`/`stts` sample-count mismatch as "a residual, unclosed risk" — a
  forward-looking claim the re-vendor had since resolved (issue #28, closed
  alongside #27) — and ADR 0012 itself was missing from `context/index.md`'s
  Decisions list (present in the decided register, just not the bundle
  catalog). Both fixed. Also caught and fixed a stray "division of labour"
  (heading and frontmatter) in the same file — the one American-English
  holdout found in a repo-wide re-check against the 2026-07-28 spelling pass;
  its own `references/`/`external/` exemptions accounted for, and `log.md`'s
  own dated history left as written rather than rewritten.

* **Extraction stage no longer silently mis-parses four classes of malformed
  input.** Fixes issue #4. `parse_iso8601_to_epoch`
  (`session_metadata.cpp`) now rejects a malformed or out-of-range UTC offset
  (`+02` with no minutes, `+99:99`) instead of leaving it at 0 and still
  succeeding — the exact silently-wrong-anchor failure the function's own
  header comment says it avoids — and rejects a calendar-impossible date
  (`2018-02-31`) by comparing against what `timegm` actually normalized it to
  (glibc silently turns it into `2018-03-03` rather than erroring).
  `mp4_metadata_extractor.cpp`'s `duration_us` (informational only; nothing
  else reads it) no longer wraps for a hostile `mdhd` duration near
  `UINT64_MAX` — `duration_units * 1'000'000` is checked against its own
  overflow threshold before the multiply, clamped to a new
  `kMaxSaneDurationUs` (100 years) ceiling rather than computed in a
  non-standard 128-bit type. `session_metadata.cpp`'s `read_box` bounds check
  changed from `pos + size > limit` (size_t addition that can wrap for a
  hostile 64-bit largesize) to `size > limit - pos` (cannot wrap, since
  `pos <= limit` is already guaranteed) — empirically this exact box shape
  turns out to be absorbed downstream by `entry_text`'s own
  `body_begin < body_end` guard either way, so the fix is correctness
  hygiene rather than a provable pre/post divergence; see
  `check_hostile_largesize_is_rejected`'s comment in
  `session_metadata_test.cpp`. `mp4_box_scan.cpp`'s `parse_tkhd` now rejects
  (rather than mis-parses as version 0's 8-byte time layout) a `tkhd`
  declaring a version past 1, which ISO/IEC 14496-12 does not define; Parrot
  never writes one, so this only affects diagnostics. New regression tests
  and fixtures for all four (`check_iso8601`'s new cases,
  `test_hostile_mdhd_duration_is_bounded`,
  `test_tkhd_version_2_track_id_not_misread`,
  `check_hostile_largesize_is_rejected`), each confirmed genuinely
  discriminant against the pre-fix code under ASan/UBSan except the
  `read_box` one (documented above).

* **`anchor_to_session` no longer stamps a proto frame that declared its own
  UTC invalid, and no longer treats a failed origin sample as clock zero.**
  Fixes issue #7. The anchor's early return only checked
  `snapshot.utc_timestamp_us` being already set; a proto frame with
  `utc_timestamp_accuracy == 0` reaches `from_proto` with that field
  deliberately left empty (the schema's own "invalid" signal), and both
  pipelines called `anchor_to_session` unconditionally — so it got stamped
  from `media_date + camera-monotonic-elapsed` anyway, an anchor whose
  assumptions (monotonic clock zeroed at recording start, media date == first
  frame's wall time) are validated for v3 only. Now gated on
  `snapshot.source_format == ParrotFormat::V3`. Separately,
  `first_monotonic_us` changed from `uint64_t` (silently 0 when the origin
  sample's extraction or parse failed) to `std::optional<uint64_t>`
  (`std::nullopt` on that failure), so an unknown clock origin now disables
  anchoring instead of being treated as zero. New tests in
  `transform_test.cpp`: `check_anchor_skips_proto_declared_invalid`,
  `check_anchor_skips_unknown_origin`, `check_anchor_v3_with_known_origin`
  (the last of which had no prior coverage at all — `anchor_to_session` was
  untested on its happy path before this).

* **Re-vendored `minimp4.h` onto upstream's overflow fix, closing issues #27
  and #28.** [ADR 0012](decisions/0012-revendor-minimp4-overflow-fix.md).

* **Doc-drift close-out and triage housekeeping.** README's `parrot-scrub`
  blurb now names what scrub removes beyond location — identity and the
  flight's absolute start time (serial, friendly name, title, copyright,
  `©day`) — not just "location fields". Issue #3 closed as resolved by PRs
  #23/#26; the two vendored-`minimp4.h` remainders each now have their own
  issue (#27 the in-`MP4D_open` `stsz` heap-overflow write, #28 the
  stts/stsz-mismatch OOB read), both pending the vendored-patch/ADR decision.

* **Extractor refuses tracks with missing or implausible sample-index
  tables, and caps unbounded allocation.** Fixes the two remaining findings
  of issue #3 (the wrapped-bounds-check finding shipped 2026-08-18).
  `MP4D_open()` validates only `track_count > 0`; a track missing `stco`/
  `co64` or `stsc` leaves `chunk_offset`/`sample_to_chunk` null, and
  `MP4D_frame_offset()`'s `sample_to_chunk()` helper dereferences
  `sample_to_chunk[0]` unconditionally on its first loop iteration whenever
  `chunk_count` is at least two (one chunk hits a safe early-return path
  inside `sample_to_chunk()` — cost a fixture rewrite to discover: the first
  attempt, with a single chunk, opened and extracted cleanly with no crash
  at all). New `track_sample_tables_valid()` in `mp4_metadata_extractor.cpp`
  checks every mandatory table is non-null, non-empty, and `sample_count`
  under a 10M-sample sanity ceiling, called right after `MP4D_open()`
  succeeds and before anything that would dereference them.
  `extract_sample()` separately caps a single declared sample size at 1 MiB
  before `resize()` — real Parrot samples run to a few hundred bytes; a
  hostile `stsz` entry can declare up to 4 GiB. Two new hostile fixtures
  (`parrot_missing_stsc.mp4`, `parrot_oversized_sample_size.mp4`) and their
  regression tests in `mp4_extraction_test.cpp`. Verified the missing-`stsc`
  crash is real before fixing it: a standalone repro against the pre-fix
  code segfaulted (confirmed via a two-chunk fixture — the first, one-chunk
  attempt did not reproduce it, which is what led to finding minimp4's
  `chunk_count <= 1` fast path); the same repro against the fix returns a
  clean rejection instead. `ctest -L parrot`: 15/15 green in both the
  ordinary and ASan build directories.

  **Deliberately not closed:** a residual OOB-read risk where `stsz`'s
  `sample_count` and `stts`'s own declared total disagree — minimp4 discards
  the true `stts` total once the box is parsed, so nothing outside
  `MP4D_open()` can detect the mismatch without an independent re-parse.
  Documented in [mp4-extraction](./mp4-extraction.md) § minimp4 traps rather
  than attempted here; closing it needs either a minimp4.h patch or a
  parallel `stts` scan, both bigger moves than a wrapper-boundary check.

  **Also found, not fixed here — filed separately:** `MP4D_open()`'s `stsz`
  parser multiplies `sample_count * 4` in 32-bit `unsigned` arithmetic before
  passing it to `malloc()`; a `sample_count` near `0x40000000` overflows that
  product to a small number, so the allocation undersizes while the
  following fill loop still iterates the full (unoverflowed) `sample_count`
  — a heap buffer overflow *write*, inside `MP4D_open()` itself, before any
  wrapper-boundary check added here ever gets a chance to run. Patching
  vendored `external/minimp4/minimp4.h` for a security fix is a real
  decision (this project's stated policy is read it, never restyle it) that
  deserves its own issue and likely an ADR, not a quiet fix folded into this
  one.

## 2026-08-18

* **Mux stage stops leaving a complete-looking output behind for a signal
  interruption or a zero-frame conversion.** Fixes issue #10:
  - `mux_to_ts()` called `sink.close()` unconditionally, then only *afterward*
    decided whether zero converted frames (or an error the sample loop had
    already raised) should fail the run. Closing succeeds whenever the video
    branch alone drains cleanly — libmisbklv has no way to know the KLV side
    produced nothing — and a successful close clears the sink's "discard on
    destruction" state, so the file survived next to the error message. Now
    `report.error` is decided before `close()` is ever called, and `close()`
    is skipped entirely once an error is set (whichever raised it), letting
    `KlvSink`'s destructor discard the file exactly the way an in-loop push
    failure already did.
  - No SIGINT/SIGTERM handling existed anywhere in `src/`. Default
    disposition terminates the process immediately, runs no destructors, and
    leaves whatever the sink had written so far — Ctrl-C on a long flight
    left a truncated `.ts` indistinguishable from a finished one. `mux.cpp`
    gained `request_mux_interrupt()`, an async-signal-safe flag setter
    (`sig_atomic_t`, nothing else) that `convert_all()`'s per-sample loop
    checks once per iteration; `main.cpp` installs it via `std::signal` for
    both signals. This is cooperative, not preemptive — a signal arriving
    while a single sample is blocked inside the sink's push is not observed
    until that call returns, which is the best available without threading
    or async cancellation at this layer.

  `test/mux_test.cpp` gained two regression tests: a new fixture
  (`parrot_v3_video_undecodable_metadata.mp4` — real H.264 video paired with
  every metadata sample too short to parse) proves the zero-frame path
  leaves no file, and a direct call to `request_mux_interrupt()` exercises
  the interruption path deterministically (real signal delivery races a
  fixture that converts in single-digit milliseconds, so the flag is set
  directly rather than by sending an actual SIGINT — the process-wide flag
  has no reset, so that test runs last). Both were verified to fail against
  the pre-fix code before being confirmed green against the fix.
  `context/mux-stage.md` § "An error must leave no output file" updated to
  describe both mechanisms. `ctest -L parrot`: 15/15 green in both the
  ordinary and ASan build directories.

* **Transform stops fabricating position, AGL, and Item 124 for no-fix and
  invalid frames; a NaN attitude is now reported invalid rather than
  out-of-range.** Fixes issue #6:
  - `from_proto()` treated proto3's zero-lat/lon absent-value sentinel as a
    real fix only for the camera-vs-drone choice, then assigned whichever
    location it picked unconditionally — so a genuinely no-fix frame (both
    locations zero) reported a confident 0°N 0°E. `TelemetrySnapshot::
    latitude_deg`/`longitude_deg` now stay empty when neither location has a
    fix, and `transform.cpp`'s existing `if (s.latitude_deg) ...` guards on
    Items 13/14 needed no change — they were already correct once given an
    honest optional.
  - `ground_distance_m` was a bare `double`, so Item 113 was unconditionally
    emitted even at the same zero-sentinel value; it is now
    `std::optional<double>`, empty under proto's zero-is-absent rule
    (matching altitude/zoom/FOV, and the same known tradeoff: a landed
    drone's true 0 m AGL is indistinguishable from no data). v3's AGL keeps
    its prior always-present behavior — v3 has no documented no-fix
    sentinel, and guessing one wasn't in scope for this fix.
  - Item 124 was encoded as `0x00` whenever `sv_count` was present but zero,
    which ST 0601.19 §8.124.1 forbids outright ("a value with all bits set
    to zero ... is not allowed"). Now omitted (`UndefinedForFrame`) in that
    case; Item 123 still reports the true zero count.
  - `put_narrow_angle()` (Items 6/7) compared `std::abs(degrees) <= limit`
    without a finiteness check first; a NaN pitch/roll (reachable from an
    unchecked proto wire float, or asin's clamp guard not catching every
    path) is neither true nor false under that comparison and fell into the
    out-of-range `0x8000` sentinel branch. Now checked with `std::isfinite`
    first and handed to `put()`, which already treats non-finite as
    `InvalidSourceValue` — matching how Items 5/90/91 already behaved.

  **Deliberately not touched:** the v3 path has the same shape as a
  no-fix-sentinel question (`from_v3` assigns `latitude_deg`/`longitude_deg`
  unconditionally too), but v3 is a fixed binary format with no documented
  "0 means no fix" convention anywhere in the reference docs or this
  project's own knowledge base — unlike proto's explicit no-presence-bit
  design. Guessing one risks turning a legitimate v3 reading of exactly
  0°N 0°E into a fabricated omission, which is the same class of error this
  fix closes, just inverted. Left open rather than guessed at.

  `transform_test` gained three regression tests
  (`check_no_fix_proto_frame_omits_position_and_agl`,
  `check_zero_sv_count_omits_item_124`,
  `check_nan_attitude_is_invalid_not_out_of_range`); each was verified to
  fail against the pre-fix code before being confirmed green against the
  fix. `context/transform-stage.md` § "Zero is not a value in proto" and the
  invalid-values section, and `field-mapping-analysis.md`'s Item 124 row,
  updated to match. `ctest -L parrot`: 15/15 green in both the ordinary and
  ASan build directories.

* **Bounds checks on hostile sample-table offsets rewritten as subtraction, not
  addition.** Fixes the cheap half of issue #3:
  `mp4_metadata_extractor.cpp`'s `zero_track_samples()` and
  `parrot_scrub.cpp`'s per-sample rewrite loop both checked
  `offset + size > file.size()`, which can wrap for an `offset` near
  UINT64_MAX before the following `static_cast<long>(offset)` — a hostile
  `co64` entry could turn that into a negative-offset OOB write. Investigating
  a regression fixture for this surfaced that the wrap is not reachable in
  this build today: minimp4 refuses to open any file with a `co64` entry
  whose upper 32 bits are nonzero at all (`MP4D_64BIT_SUPPORTED` is off by
  default and undefined anywhere in this project's CMake), so a real parsed
  `offset` never exceeds 32 bits, and `offset + size` in 64-bit arithmetic
  can't overflow at that scale. The subtraction-form check goes in anyway —
  correct regardless, and it stops relying on a guard this project doesn't
  control — verified against `test/fixtures/parrot_out_of_bounds_chunk_offset.mp4`
  (an ordinary out-of-file 32-bit `stco` offset, the case that must still be
  rejected). `mp4_extraction_test` gained the regression test; both source
  comments say plainly that this is defense in depth, not a demonstrated
  exploit closed. `ctest -L parrot`: 15/15 green in both the ordinary and
  ASan build directories. The other half of #3 — minimp4 accepting tracks
  with missing/mismatched sample tables — is unaddressed; better paired with
  #17's malformed-MP4 fixture work.

* **`parrot-scrub` now scrubs session identity and the absolute flight date, not
  just the geotag.** Fixes issue #12: `©day` (the flight's absolute wall-clock
  start time), `©too` (serial), `©ART` (friendly name), `©nam` (title, which
  restates the date in prose) survived every prior scrub untouched, while the
  printed warning claimed only serial/friendly-name/software-version were left
  and justified that as "identifies the aircraft, not the location" — silently
  omitting the timestamp, which the project already treats as excluded by
  construction in the conformance report. `©day` is zero-digited the same way
  the `©xyz` geotag already was; the other four have no format worth
  preserving, so their text is overwritten outright (same length, so no box
  resizes — `scrub_geotag` is renamed `scrub_session_boxes` to match). `©mak`,
  `©mod` (feeds ST 0601 Item 4/10) and `©swr` are not identity-bearing and stay
  untouched; the warning now says so instead of overclaiming. New
  `ScrubReport::identity_boxes_rewritten` counter, printed by the CLI.
  `test/parrot_scrub_test.cpp` gained a fixture-string check that none of the
  original identity/date values survive. `context/mp4-extraction.md` §
  Scrubbing updated to match; the ADR register is untouched since this is a bug
  fix closing a gap the docs described as intentional, not a design fork.

* **PR #22 follow-up: the identity/date scrub now handles both ilst entry
  shapes, and `blank_text` stopped over-counting.** deepseek-v4-flash's PR
  review caught that `session_metadata.cpp`'s own `entry_text()` accepts a
  second ilst shape — the string straight in the `<key>` box, no nested `data`
  box — that the new scrub code never checked; a file written that way kept
  every identity/date value byte-for-byte while the report claimed a clean
  scrub. Fixed by falling back to the key box's own body exactly the way
  `entry_text()` does, and added `test/fixtures/parrot_v3_direct_ilst.mp4`
  (`make_fixture.py` gained `direct_ilst`/`ilst_entry_direct()`) plus a test
  asserting the fallback scrubs it. Also fixed the flagged low-severity
  cosmetic issue: `blank_text` counted every byte written rather than only
  changed ones, so re-scrubbing an already-blanked file kept inflating
  `identity_boxes_rewritten`; it now gates on content change like
  `zero_digits` already did.

* **Moved the libmisbklv pin to `84c7e3663672168d931a36296b864766f654f312`.**
  This adopts upstream's 2026-08-17 review-issue wave (upstream issues #4–#8,
  each landed via a reviewed PR with the full CTest and ASan/UBSan suites
  green). What touches this project's paths: the gstreamer backend error-path
  hygiene sweep (`gst_insert`/`gst_video` — internal only, no API change) and
  the core codec/API polish — `Message::set(1, …)` now returns `Error::ReadOnly`
  and `Message::encode()` enforces mandatory items on the `create()` path, both
  inert here since the mux parses existing packets and the transform authors
  through `LocalSetBuilder`. The rest is out of this project's paths: the
  `gst_extract` PtsMarks growth bound and malformed-input robustness (TS read
  side — this project only writes TS), the ST 0903 variable-uint default widths
  (ADR 0029), and the now-decided non-minimal BER length leniency on read
  (ADR 0030). Adapting to the new `ReadOnly` enumerator required one source
  edit: `describe_misbklv_error` gained its switch case, and `mux_test` pins
  the string. Tool output is unchanged; full CTest green (39/39, both suites).

## 2026-08-12

* **`v0.1.5` published.** Tagged at `552573f` after the pin-bump PR (#2) merged
  and its ordinary CI passed. The tag workflow rebuilt on the 22.04 ABI floor,
  repeated the bare-24.04 bundle and container-image smoke tests, and published
  both deliverables with checksums and the hand-written changelog section. This
  release distributes the video-first PMT stream order (above); it is the first
  release whose output stream ordering differs from all earlier ones.

* **Moved the libmisbklv pin to `f630d51c17826e2a64d26937a42c88ffd7511a5d`.**
  This adopts upstream's video-first PMT stream order: the video muxer pad is
  reserved while the pipeline is NULL so it takes the lower ES PID, making
  `ffmpeg -map 0:0` select the video where it previously selected the KLV. The
  mechanism, and the two earlier attempts that failed with `NOFORMAT`, are
  owned by libmisbklv ADR 0020 § Stream order (revised 2026-08-12) — a thin
  pointer here, not a restatement. Our `mux_test` pinned the old order as a
  known limitation and now pins the new one; README, the packaging usage doc,
  and the mux-stage concept describe video-first, and PROGRESS's known-gap
  bullet is gone. The mux stage's public-API usage (`KlvSink` / `Message`) was
  untouched by the change; the only source edit is the `--help` text, which
  described the old order. `v0.1.5` distributes this.

## 2026-08-10

* **README now lists the full build-time and runtime package requirements.**
  The build section's one-line "GStreamer development packages are needed" note
  didn't capture that the mux stage also needs the `gstreamer1.0-plugins-base`
  (`appsrc`, `appsink`) and `gstreamer1.0-plugins-bad` (`mpegtsmux`,
  `h264parse`, `h265parse`) runtime packages, which the matching `*-dev`
  packages do not pull in — on a bare install the mux test fails with "media
  pipeline failed" while everything else passes. Added the complete apt command
  and why the runtime half is easy to miss.

## 2026-08-08

* **`v0.1.4` published.** Tagged at `3b8d608` only after ordinary CI and the
  tagless release workflow passed. The tag workflow rebuilt on the 22.04 ABI
  floor, repeated the bare-24.04 bundle and container-image smoke tests, and
  published both deliverables with checksums and the hand-written changelog
  section. The dependency-only release changes no runtime behavior; clean local
  and CI builds benefit from libmisbklv's purged historical data corpus.

* **Declared `0.1.4` and wrote its human-facing release notes.** This patch
  release distributes the latest libmisbklv pin after upstream's historical
  data purge. It calls out faster clean dependency fetches and the absence of a
  runtime behavior change; `project(VERSION)` agrees with the intended tag.

* **Moved the libmisbklv pin to `2533e235d97036f597918ae0268ec4db9d8f2e65`.**
  This takes the latest upstream revision after its historical data corpus was
  purged, so fresh local and CI dependency fetches no longer carry those large,
  unused files. The only source-tree change since the previous pin corrects
  documentation attribution; production behavior is unchanged.

* **Moved the libmisbklv pin to `a25ae2e94a5f45fbef1891b3cda90c5078a00375`.**
  The upstream change replaces external test media and derived fixtures with a
  committed, project-owned synthetic corpus; its production headers and sources
  are unchanged apart from comments. Normal configure, build, and test remain
  Python-free; Python 3.11 is needed only for optional upstream regeneration
  targets. A fresh build with Python discovery explicitly disabled passed the
  complete combined suite (37 tests, with the optional real-data consumer test
  skipped as designed). The obsolete LFS skip left over from upstream's external
  corpus was removed. This is build/test maintenance with no runtime or
  user-facing behavior change, so `v0.1.3` remains the current release rather
  than publishing a pin-only patch.

## 2026-08-01

* **Migrated the knowledge bundle to OKF v0.2.** `timestamp:` became
  `generated: {by, at}` across every concept and ADR, carrying each doc's
  existing timestamp through unchanged; the actor is `claude/opus-5` throughout,
  never the `human:` prefix, since trust tooling keys off that prefix and these
  are agent-written docs. ADRs now carry `decision_status:` rather than
  `status:`, because v0.2 claims `status` for *document* lifecycle
  (draft/stable/deprecated) — a different axis from decision state, as a
  superseded ADR is still a stable document. CONVENTIONS gained the v0.2
  frontmatter shape, the actor convention, and the optional-families table, and
  its existing `# Citations` section became `# Citations and sources` by merge
  rather than replacement, keeping this repo's own cite-by-stable-anchor rule.
  ROADMAP's status legend and fork-lifecycle prose were updated in the same
  pass — they instructed the next agent to write a field that no longer exists.

* **Made the agent entrypoint vendor-neutral.** `AGENTS.md` is canonical;
  `CLAUDE.md` is a one-line `@AGENTS.md` import, since Claude Code auto-loads
  that filename. A pointer, not a fork — duplicating the rules into two files is
  the exact duplication those rules exist to prevent. Live cross-references were
  repointed; this log's earlier entries were left alone as a frozen record of
  what `CLAUDE.md` said at the time.

* **Resolved two contradictions the docs had carried.** `references/` was
  described as immutable and never to be modified, which contradicts ingesting
  into it and makes an agent hesitate before a directed ingest; it is now stated
  as append-only, with immutability governing each snapshot rather than the
  directory's file count, plus the case nothing covered — a source believed
  wrong is never edited in place, it gets a `context/` concept citing it and
  saying where it departs. `references/README.md` was rewritten to describe what
  is actually here (an HTML documentation export and its asset tree, not PDFs),
  and its stale "this README is a placeholder" line was dropped now that a real
  source has landed. Separately, CONVENTIONS claimed broken links "are not
  errors" — OKF *consumer*-tolerance semantics pasted into this repo's own
  policy. Consumers must tolerate a dangling link; this bundle must not ship
  one, which is what lets the two-form linking boundary stay structural: the CI
  checks both `[[slug]]` and relative forms, so neither is the unchecked one.

* **Fixed a blind spot that had been silently disabling the link check.** The
  workflow stripped HTML comments *before* code, so a bare `<!--` inside a
  documented code block paired with a `-->` far below and swallowed everything
  between — every check skipped that span, invisibly. Code spans are now removed
  first, comments last. Verified against a constructed case: under the old
  ordering a broken link below such a fence produced no hit at all; under the
  new one it is caught. The workflow also gained a dangling-footnote check
  (`[^label]` matching no `sources[].id` in the same file). That check and the
  reordering are coupled here: this log's `grep -q '[^[:space:]]'` example is a
  POSIX character class the footnote regex would misread, and it is safe only
  because the corrected order strips its inline-code span first.

* Backported from `okf-project-template` (commits `6301ae4`, `5643ecb`,
  `a1e0a1c`, `cabc297`), applied in dependency order rather than template order
  so each file was written once into its final home. The template's
  cite-by-stable-anchor commit needed no backport — this repo already had it.
  The libmisbklv pin was deliberately not bumped: the sister repo's matching
  batch changed no header, source, or build input, so a bump would carry no
  reviewable upstream change.

## 2026-07-31

* **`v0.1.3` published.** Tagged at `67c25d2` after ordinary CI and a tagless
  release workflow both passed. The tag workflow rebuilt on the 22.04 ABI floor,
  repeated the bare-24.04 bundle and container-image smoke tests, and attached
  both deliverables with checksums. The release page carries the hand-written
  `## v0.1.3` changelog section.

* **Declared `0.1.3` and wrote its human-facing release notes.** The patch
  release leads with input-preservation and truthful-telemetry fixes, names the
  improved media failure reporting, and documents the stricter manual packaging
  destination contract. `project(VERSION)` now agrees with the intended tag.

* **Removed tracked Python bytecode and aligned runtime-plugin rationale.**
  Python cache artifacts are ignored, and CI and fixture comments now match the
  explicit-demuxer plugin set packaged by the release bundle.

* **Bundle and image staging now refuse unsafe reset targets before deletion.**
  Both paths share one canonical-path guard: broad paths, the current directory,
  the home directory, the repository outside its normal staging area, and paths
  overlapping the input are refused. Normal repository staging destinations and
  nested directories under `/tmp` remain valid. Disposable tests verify that a
  refused target preserves its contents.

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
