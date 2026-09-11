# Parrot to KLV — Progress

Current status only — **present tense, volatile, rewrite freely**. The
chronological history (what landed when, decision detail) lives in
[`../context/log.md`](../context/log.md); the decided-fork register is
[`../context/decisions/index.md`](../context/decisions/index.md); the plan is
[ROADMAP.md](./ROADMAP.md). This file does not re-narrate the past.

## Now

**`v0.5.1` is the current published release.** It provides native x86-64 and
aarch64 bundles and container images; release notes are in
[`CHANGELOG.md`](../CHANGELOG.md). The detailed hardening history belongs in the
[log](../context/log.md), not here.

## In progress

- **Making the repository public** awaits GitHub Support purging stale pull
  refs and cached objects from before the documentation withdrawal; see
  [ADR 0016](../context/decisions/0016-references-licensing-withdrawal.md).

## Next

1. Fork 4, Tier 2 geometry fidelity, in [ROADMAP](./ROADMAP.md).

## Known gaps

- Non-H.264 output carries no ST 0604; see
  [ADR 0007](../context/decisions/0007-h265-vs-st0604-generation.md).
- Terrain intersection is still required for Items 21/22/23/24/25/57/78/82–89;
  that is Fork 4.
- The `IR` image-source inference lacks thermal-recording evidence; see
  [ADR 0011](../context/decisions/0011-inferred-image-source-sensor.md).
- Proto parsing still lacks real-recording and reference-encoder validation;
  see [ADR 0003](../context/decisions/0003-protobuf-dependency.md).
