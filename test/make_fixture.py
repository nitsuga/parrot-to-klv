#!/usr/bin/env python3
"""Generate synthetic MP4 fixtures with a Parrot-style timed metadata track.

Real Parrot footage carries live location data and can never be committed (see
CLAUDE.md § Security), so the extractor is tested against files built here: same
box layout, invented payloads. Track layout follows Parrot's documented shape --
tkhd/tref(cdsc)/hdlr('meta')/nmhd/stsd(TextMetaDataSampleEntry).

Usage: make_fixture.py <output-dir>
"""

import struct
import sys
from pathlib import Path

MIME_PREFIX = "application/octet-stream;type="


def box(kind: bytes, payload: bytes) -> bytes:
    return struct.pack(">I", 8 + len(payload)) + kind + payload


def full_box(kind: bytes, version: int, flags: int, payload: bytes) -> bytes:
    return box(kind, struct.pack(">I", (version << 24) | flags) + payload)


UNITY_MATRIX = struct.pack(">9i", 0x10000, 0, 0, 0, 0x10000, 0, 0, 0, 0x40000000)


def mvhd(timescale: int, duration: int, next_track_id: int) -> bytes:
    return full_box(b"mvhd", 0, 0, struct.pack(">IIII", 0, 0, timescale, duration)
                    + struct.pack(">ihh", 0x10000, 0x100, 0)      # rate, volume, reserved
                    + b"\x00" * 8
                    + UNITY_MATRIX
                    + b"\x00" * 24
                    + struct.pack(">I", next_track_id))


def tkhd(track_id: int, duration: int, width: int, height: int, volume: int) -> bytes:
    return full_box(b"tkhd", 0, 7, struct.pack(">IIIII", 0, 0, track_id, 0, duration)
                    + b"\x00" * 8
                    + struct.pack(">hhhh", 0, 0, volume, 0)       # layer, group, volume
                    + UNITY_MATRIX
                    + struct.pack(">II", width << 16, height << 16))


def mdhd(timescale: int, duration: int) -> bytes:
    return full_box(b"mdhd", 0, 0, struct.pack(">IIII", 0, 0, timescale, duration)
                    + struct.pack(">HH", 0x55C4, 0))              # 'und', pre_defined


def hdlr(handler: bytes, name: str) -> bytes:
    return full_box(b"hdlr", 0, 0, struct.pack(">I", 0) + handler + b"\x00" * 12
                    + name.encode() + b"\x00")


def dinf() -> bytes:
    url = full_box(b"url ", 0, 1, b"")                            # flag 1: self-contained
    return box(b"dinf", full_box(b"dref", 0, 0, struct.pack(">I", 1) + url))


def stsd_mett(mime: str, content_encoding: str = "") -> bytes:
    entry = box(b"mett", b"\x00" * 6 + struct.pack(">H", 1)
                + content_encoding.encode() + b"\x00"
                + mime.encode() + b"\x00")
    return full_box(b"stsd", 0, 0, struct.pack(">I", 1) + entry)


def stsd_avc1() -> bytes:
    entry = box(b"avc1", b"\x00" * 6 + struct.pack(">H", 1)
                + b"\x00" * 16
                + struct.pack(">HH", 320, 240)
                + struct.pack(">II", 0x480000, 0x480000)
                + struct.pack(">IHH", 0, 1, 0)
                + b"\x00" * 30
                + struct.pack(">Hh", 24, -1))
    return full_box(b"stsd", 0, 0, struct.pack(">I", 1) + entry)


def stbl(sample_desc: bytes, sizes: list[int], delta: int, chunk_offset: int) -> bytes:
    stts = full_box(b"stts", 0, 0, struct.pack(">III", 1, len(sizes), delta))
    stsc = full_box(b"stsc", 0, 0, struct.pack(">IIII", 1, 1, len(sizes), 1))
    stsz = full_box(b"stsz", 0, 0, struct.pack(">II", 0, len(sizes))
                    + b"".join(struct.pack(">I", s) for s in sizes))
    stco = full_box(b"stco", 0, 0, struct.pack(">II", 1, chunk_offset))
    return box(b"stbl", sample_desc + stts + stsc + stsz + stco)


def meta_trak(track_id: int, video_track_id: int, timescale: int, sizes: list[int],
              delta: int, chunk_offset: int, mime: str, content_encoding: str = "") -> bytes:
    tref = box(b"tref", box(b"cdsc", struct.pack(">I", video_track_id)))
    minf = box(b"minf", full_box(b"nmhd", 0, 0, b"") + dinf()
               + stbl(stsd_mett(mime, content_encoding), sizes, delta, chunk_offset))
    mdia = box(b"mdia", mdhd(timescale, delta * len(sizes))
               + hdlr(b"meta", "ParrotMetadata") + minf)
    return box(b"trak", tkhd(track_id, delta * len(sizes), 0, 0, 0) + tref + mdia)


def video_trak(track_id: int, timescale: int, sizes: list[int], delta: int,
               chunk_offset: int) -> bytes:
    vmhd = full_box(b"vmhd", 0, 1, struct.pack(">HHHH", 0, 0, 0, 0))
    minf = box(b"minf", vmhd + dinf() + stbl(stsd_avc1(), sizes, delta, chunk_offset))
    mdia = box(b"mdia", mdhd(timescale, delta * len(sizes))
               + hdlr(b"vide", "VideoHandler") + minf)
    return box(b"trak", tkhd(track_id, delta * len(sizes), 320, 240, 0) + mdia)


def build(mime: str, samples: list[bytes], video_samples: list[bytes],
          timescale: int = 30000, delta: int = 1000,
          content_encoding: str = "") -> bytes:
    """Assemble ftyp + mdat + moov. mdat first so chunk offsets are known up front."""
    ftyp = box(b"ftyp", b"isom" + struct.pack(">I", 512) + b"isomiso2avc1mp41")

    video_blob = b"".join(video_samples)
    meta_blob = b"".join(samples)
    mdat_payload = video_blob + meta_blob
    video_chunk_offset = len(ftyp) + 8
    meta_chunk_offset = video_chunk_offset + len(video_blob)
    mdat = box(b"mdat", mdat_payload)

    duration = delta * max(len(samples), len(video_samples))
    moov = box(b"moov", mvhd(timescale, duration, 3)
               + video_trak(1, timescale, [len(s) for s in video_samples], delta,
                            video_chunk_offset)
               + meta_trak(2, 1, timescale, [len(s) for s in samples], delta,
                           meta_chunk_offset, mime, content_encoding))
    return ftyp + mdat + moov


def v3_sample(index: int) -> bytes:
    """A v3 base struct is 0x5033 + length in 32-bit words, then the payload.

    Contents are invented -- these fixtures exercise MP4 extraction, not the
    Parrot parser, which gets its own fixtures when it exists.
    """
    body = bytes((index + i) & 0xFF for i in range(60))
    return struct.pack(">HH", 0x5033, len(body) // 4) + body


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    out_dir = Path(sys.argv[1])
    out_dir.mkdir(parents=True, exist_ok=True)

    samples = [v3_sample(i) for i in range(8)]
    video = [bytes([0xDE, 0xAD, 0xBE, 0xEF, i]) for i in range(8)]

    fixtures = {
        "parrot_v3.mp4": build(MIME_PREFIX + "com.parrot.videometadata3", samples, video),
        "parrot_proto.mp4": build(MIME_PREFIX + "com.parrot.videometadataproto", samples, video),
        # Some writers fill in content_encoding; the MIME type is then the
        # *second* string in the sample entry.
        "parrot_v3_encoded.mp4": build(MIME_PREFIX + "com.parrot.videometadata3", samples,
                                       video, content_encoding="binary"),
        # A metadata track that is not Parrot's -- must be rejected, not guessed.
        "foreign_meta.mp4": build("application/octet-stream;type=com.example.telemetry",
                                  samples, video),
    }
    for name, data in fixtures.items():
        (out_dir / name).write_bytes(data)
        print(f"{name}: {len(data)} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
