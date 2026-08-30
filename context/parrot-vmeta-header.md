---
type: Reference
title: Parrot vmeta.h — location sentinel
description: Parrot's vmeta.h header defining VMETA_LOCATION_INVALID_SV_COUNT and vmeta_location, vendored under BSD-3-Clause
tags: [parrot, vmeta, sentinel, reference]
generated:
  by: opencode-go/muse-spark-1.2-contributor
  at: 2026-08-30T00:00:00Z
sources:
  - id: vmeta-h
    resource: ../references/vmeta.h
    title: Parrot-Developers/libvideo-metadata — vmeta.h at 22f715a (BSD-3-Clause)
---

# Parrot vmeta.h — location sentinel

Vendored header from Parrot's reference implementation at
[`22f715a`](https://github.com/Parrot-Developers/libvideo-metadata/tree/22f715adbb52ad14693b3b98fa537f3dbef53409)[^vmeta-h],
BSD-3-Clause (in-file notice). Local snapshot: `../references/vmeta.h`.

Stable anchor: `vmeta.h` § `VMETA_LOCATION_INVALID_SV_COUNT` and `struct vmeta_location`[^vmeta-h] —
`#define VMETA_LOCATION_INVALID_SV_COUNT ((uint8_t)-1)` (0xFF) and the location
struct that carries it.

Key fact for this project: the header **decouples count from valid** — `sv_count`
may be `VMETA_LOCATION_INVALID_SV_COUNT` even when `valid` is set, meaning
"count unavailable", not "no fix"[^vmeta-h]. v3 packs only altitude+count and
offers no validity signal other than `sv_count`, so 0xFF leaves nothing to trust
— fail closed (withhold position). Cost: a real fix whose count is unreported
loses position.

[^vmeta-h]: `vmeta.h` § `VMETA_LOCATION_INVALID_SV_COUNT` / `vmeta_location` — sentinel definition and the comment "set to VMETA_LOCATION_INVALID_SV_COUNT if not available even if valid is set" with "when a location is valid, sv_count may still be invalid".
