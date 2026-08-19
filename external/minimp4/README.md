# minimp4 - Vendored Dependency

**Version:** commit `5a212a18dba7dca09543bbc7d65619274fd2931a` (2026-07-13)  
**Source:** https://github.com/lieff/minimp4  
**License:** Public Domain (Unlicense) — see [LICENSE](./LICENSE)

## About

minimp4 is a single-header MP4 muxer/demuxer library (~2K LOC). We use it to:
- Parse MP4 box structure (`moov`, `trak`, `mdia`, `minf`, `stbl`)
- Locate the Parrot metadata track (handler type `meta`)
- Extract metadata samples for conversion to MISB 0601 KLV

## Why Vendored?

Single-header libraries are commonly vendored rather than fetched at build time.
This approach:
- Simplifies builds (no download step)
- Is standard practice for stb-style single-header libs
- Still tracks provenance (version documented here)

## Files

- `minimp4.h` — The complete library (header-only)
- `LICENSE` — Unlicense (Public Domain)
- `README.md` — This file (provenance documentation)

## Updates

To update minimp4:
1. Clone https://github.com/lieff/minimp4
2. Copy `minimp4.h` and `LICENSE` to this directory
3. Update the commit hash and date in this README
4. Test build and extraction with sample MP4
