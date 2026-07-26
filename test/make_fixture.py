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


def geotag(iso6709: str) -> bytes:
    """moov/udta '\xa9xyz' -- Parrot records the flight's position here too.

    A location leak entirely separate from the telemetry track, which is why the
    scrubber has to handle it.
    """
    payload = struct.pack(">HH", len(iso6709), 0x15C7) + iso6709.encode()
    return box(b"udta", box(b"\xa9xyz", payload))


def ilst_entry(key: bytes, text: str) -> bytes:
    """One moov/udta/meta/ilst tag: <key> containing a 'data' box.

    'data' payload is 4 bytes of type indicator (1 = UTF-8) then 4 of locale,
    then the string.
    """
    data = box(b"data", struct.pack(">II", 1, 0) + text.encode())
    return box(key, data)


def session_udta(iso6709: str) -> bytes:
    """moov/udta carrying the geotag plus Parrot's four-character session tags.

    Parrot writes the session metadata two ways -- these iTunes-style keys under
    udta/meta/ilst, and full reverse-DNS keys under moov/meta/ilst. Only the
    first is generated here, matching what the parser reads.

    Note the geotag sits in 'udta' directly, NOT in the ilst -- Parrot does that
    for Android compatibility, and a parser looking only inside ilst misses it.
    """
    geo = box(b"\xa9xyz", struct.pack(">HH", len(iso6709), 0x15C7) + iso6709.encode())
    ilst = box(b"ilst",
               ilst_entry(b"\xa9ART", "ANAFI-G059745")      # friendly name
               + ilst_entry(b"\xa9mak", "Parrot")            # maker
               + ilst_entry(b"\xa9mod", "Anafi")             # model -> Item 10
               + ilst_entry(b"\xa9too", "PI040416BA8G059745")  # serial -> Item 4
               + ilst_entry(b"\xa9swr", "1.3.0")             # software version
               + ilst_entry(b"\xa9nam", "Sat, 06 Oct 2018 18:12:52 +0200")
               + ilst_entry(b"\xa9day", "2018-10-06T18:12:52+02:00"))  # media date
    # 'meta' is a FullBox: version/flags precede its children.
    meta = box(b"meta", struct.pack(">I", 0) + ilst)
    return box(b"udta", geo + meta)


def build(mime: str, samples: list[bytes], video_samples: list[bytes],
          timescale: int = 30000, delta: int = 1000,
          content_encoding: str = "", session: bool = True) -> bytes:
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
               + (session_udta("+48.85837+002.29448+135.250/") if session
                  else geotag("+48.85837+002.29448+135.250/"))
               + video_trak(1, timescale, [len(s) for s in video_samples], delta,
                            video_chunk_offset)
               + meta_trak(2, 1, timescale, [len(s) for s in samples], delta,
                           meta_chunk_offset, mime, content_encoding))
    return ftyp + mdat + moov


# --- Parrot v3 metadata samples -------------------------------------------
#
# Field order and fixed-point scales come from the reference's metadata_v3_base
# (72 bytes) and its extension structs. Values are invented but structurally
# valid, so a decoder can be checked against exact expected output.

Q16_16, Q10_22, Q16_8, Q8_8, Q2_14, Q11_5 = 1 << 16, 1 << 22, 1 << 8, 1 << 8, 1 << 14, 1 << 5


def v3_base(index: int) -> bytes:
    """The 72-byte v3 base structure. `length` is filled in by v3_sample()."""
    # A quaternion that is genuinely unit-norm, so a decoder's norm check is
    # meaningful: 60 degrees about Z.
    qw, qz = 0.86602540, 0.5

    body = struct.pack(
        ">iiii",
        round(12.5 * Q16_16),                       # ground_distance (m)
        round(48.858370 * Q10_22),                  # latitude (deg)
        round(2.294481 * Q10_22),                   # longitude (deg)
        (round(135.25 * Q16_8) << 8) | 11,          # altitude Q16.8 << 8 | sv_count
    )
    body += struct.pack(">hhhh",
                        round(3.5 * Q8_8),          # north_speed
                        round(-1.25 * Q8_8),        # east_speed
                        round(0.75 * Q8_8),         # down_speed
                        round(4.0 * Q8_8))          # air_speed
    for q in (qw, 0.0, 0.0, qz):                    # drone_quat
        body += struct.pack(">h", round(q * Q2_14))
    for q in (1.0, 0.0, 0.0, 0.0):                  # frame_base_quat
        body += struct.pack(">h", round(q * Q2_14))
    for q in (qw, 0.0, 0.0, qz):                    # frame_quat
        body += struct.pack(">h", round(q * Q2_14))
    body += struct.pack(">HH", round(8.25 * Q8_8), 200)          # exposure_time, gain
    body += struct.pack(">HH", round(1.5 * Q2_14), round(1.25 * Q2_14))  # awb r/b
    body += struct.pack(">HH", round(69.5 * Q8_8), round(43.75 * Q8_8))  # hfov, vfov
    body += struct.pack(">I", (25000 << 8) | 4)     # goodput kbit/s << 8 | quality
    body += struct.pack(">bBBB",
                        -47,                        # wifi_rssi (dBm)
                        77,                         # battery_percentage
                        0x80 | 3,                   # binning | FLYING_STATE_FLYING
                        0x80 | 2)                   # animation | PILOTING_MODE_FLIGHT_PLAN
    assert len(body) == 68, len(body)               # 72 less the id/length pair
    return body


def ext(ext_id: int, payload: bytes) -> bytes:
    assert len(payload) % 4 == 0, "extensions are 4-byte aligned"
    return struct.pack(">HH", ext_id, len(payload) // 4) + payload


def timestamp_ext(timestamp_us: int) -> bytes:
    return ext(0x4531, struct.pack(">II", timestamp_us >> 32, timestamp_us & 0xFFFFFFFF))


def automation_ext() -> bytes:
    payload = struct.pack(">iii", round(48.86 * Q10_22), round(2.30 * Q10_22),
                          round(140.0 * Q16_16))            # framing target
    payload += struct.pack(">iii", round(48.87 * Q10_22), round(2.31 * Q10_22),
                           round(150.0 * Q16_16))           # flight destination
    payload += struct.pack(">BBH", 5, 0x01 | 0x04, 0)       # animation, flags, reserved
    return ext(0x4533, payload)


def thermal_ext() -> bytes:
    payload = struct.pack(">hhh", round(0.25 * Q11_5), round(0.5 * Q11_5),
                          round(283.15 * Q11_5))            # min spot
    payload += struct.pack(">hhh", round(0.75 * Q11_5), round(0.25 * Q11_5),
                           round(312.65 * Q11_5))           # max spot
    payload += struct.pack(">hhh", round(0.5 * Q11_5), round(0.5 * Q11_5),
                           round(295.15 * Q11_5))           # probe spot
    payload += struct.pack(">BB", 2, 0x01 | 0x02)           # calib_state, validity flags
    return ext(0x4534, payload)                             # 9*2 + 2 = 20 bytes, aligned


# --- proto3 encoder -------------------------------------------------------
#
# Hand-written because this machine has no protobuf toolchain (ADR 0003). The
# intended check is the reference Python protobuf library encoding
# test/vmeta_subset.proto, which would make encoder and decoder independent;
# until then both sides are ours, so this catches an asymmetric mistake (a field
# number or wire type wrong on one side) but not a shared misreading.


def varint(n: int) -> bytes:
    out = b""
    while True:
        byte = n & 0x7F
        n >>= 7
        out += bytes([byte | (0x80 if n else 0)])
        if not n:
            return out


def tag(field: int, wire: int) -> bytes:
    return varint((field << 3) | wire)


def pb_varint(field: int, value: int) -> bytes:
    return tag(field, 0) + varint(value)


def pb_sint32(field: int, value: int) -> bytes:
    """sint32 is ZigZag-encoded -- battery_percentage and wifi rssi are sint32."""
    return tag(field, 0) + varint((value << 1) ^ (value >> 31))


def pb_float(field: int, value: float) -> bytes:
    return tag(field, 5) + struct.pack("<f", value)


def pb_double(field: int, value: float) -> bytes:
    return tag(field, 1) + struct.pack("<d", value)


def pb_msg(field: int, payload: bytes) -> bytes:
    return tag(field, 2) + varint(len(payload)) + payload


def pb_quat(w: float, x: float, y: float, z: float) -> bytes:
    return pb_float(1, w) + pb_float(2, x) + pb_float(3, y) + pb_float(4, z)


def pb_location(lat: float, lon: float, wgs84: float, amsl: float, sv: int) -> bytes:
    # altitude_egm96amsl is field 7, NOT 4 -- 4 is sv_count.
    return (pb_double(1, lat) + pb_double(2, lon) + pb_double(3, wgs84)
            + pb_varint(4, sv) + pb_float(5, 1.5) + pb_float(6, 2.5)
            + pb_double(7, amsl))


def pb_ned(north: float, east: float, down: float) -> bytes:
    return pb_float(1, north) + pb_float(2, east) + pb_float(3, down)


def proto_sample(index: int) -> bytes:
    """One serialized vmeta.TimedMetadata.

    Values are invented but structurally faithful: negative battery and RSSI
    exercise ZigZag, and an unknown top-level field exercises skip-by-wire-type.
    """
    drone = (pb_msg(1, pb_quat(1.0, 0.0, 0.0, 0.0))
             + pb_msg(2, pb_location(45.5, -73.5, 120.0, 100.0, 12))
             + pb_double(3, 25.0)                 # ground_distance
             + pb_msg(4, pb_ned(1.5, -2.5, 0.5))  # speed
             + pb_sint32(5, 77)                   # battery_percentage
             + pb_varint(7, 3)                    # FS_FLYING
             + pb_msg(9, pb_ned(10.0, 20.0, -5.0))
             + pb_varint(11, 1)                   # animation_in_progress
             + pb_varint(12, 1)                   # PM_MANUAL (binary's Manual is 0)
             + pb_double(13, 30.0))               # altitude_ato

    camera = (pb_varint(1, 33333 * index)
              + pb_msg(2, pb_quat(1.0, 0.0, 0.0, 0.0))
              + pb_msg(3, pb_quat(0.7071, 0.0, 0.7071, 0.0))
              + pb_float(4, 2.5)                  # exposure_time
              + pb_varint(5, 100)                 # iso_gain
              + pb_float(6, 1.75) + pb_float(7, 1.25)
              + pb_float(8, 1.2043)               # hfov RADIANS (~69 deg)
              + pb_float(9, 0.7376)               # vfov RADIANS (~42 deg)
              + pb_varint(10, 1770000000000000 + 33333 * index)  # utc_timestamp
              + pb_varint(11, 50)                 # utc_timestamp_accuracy (non-zero: valid)
              + pb_msg(13, pb_location(45.5, -73.5, 120.0, 100.0, 12))
              + pb_msg(14, pb_float(1, 0.5) + pb_float(2, 0.5))
              + pb_msg(15, pb_quat(1.0, 0.0, 0.0, 0.0))
              + pb_float(16, 2.0)                 # zoom_level
              + pb_varint(17, 1)                  # CS_VISIBLE
              + pb_varint(18, 3))                 # CST_WIDE -> Item 11 "EO Wide"

    wifi = pb_msg(1, pb_varint(1, 15000) + pb_varint(2, 4) + pb_sint32(3, -55))
    lfic = (pb_float(1, 0.5) + pb_float(2, 0.5)
            + pb_msg(3, pb_location(45.6, -73.6, 130.0, 110.0, 12))
            + pb_double(4, 1.0)
            + pb_varint(5, 0)                     # LFIC_TYPE_COT
            + pb_float(6, 3.0) + pb_float(7, 4.0))
    tracking = (pb_msg(1, pb_float(1, 0.25) + pb_float(2, 0.25) + pb_float(3, 0.5)
                       + pb_float(4, 0.5) + pb_varint(5, 1) + pb_float(6, 0.9)
                       + pb_varint(7, 42))
                + pb_varint(2, 33333 * index) + pb_varint(3, 5) + pb_varint(4, 0))

    return (pb_msg(1, drone) + pb_msg(2, camera) + pb_msg(3, wifi)
            + pb_msg(4, tracking) + pb_msg(8, lfic)
            # Field 7 is thermal, which the reader drops by design; it must be
            # skipped and recorded, not treated as an error.
            + pb_msg(7, pb_varint(1, 2)))


def v3_sample(index: int, extensions: bytes = b"") -> bytes:
    """0x5033 + length (32-bit words after the pair, extensions included)."""
    body = v3_base(index) + extensions
    return struct.pack(">HH", 0x5033, len(body) // 4) + body


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    out_dir = Path(sys.argv[1])
    out_dir.mkdir(parents=True, exist_ok=True)

    # A recorded sample always carries the timestamp extension; 33333 µs apart.
    samples = [v3_sample(i, timestamp_ext(33333 * i)) for i in range(8)]
    video = [bytes([0xDE, 0xAD, 0xBE, 0xEF, i]) for i in range(8)]

    fixtures = {
        "parrot_v3.mp4": build(MIME_PREFIX + "com.parrot.videometadata3", samples, video),
        # Real serialized TimedMetadata, not v3 bytes under a proto MIME type.
        "parrot_proto.mp4": build(MIME_PREFIX + "com.parrot.videometadataproto",
                                  [proto_sample(i) for i in range(8)], video),
        # Some writers fill in content_encoding; the MIME type is then the
        # *second* string in the sample entry.
        "parrot_v3_encoded.mp4": build(MIME_PREFIX + "com.parrot.videometadata3", samples,
                                       video, content_encoding="binary"),
        # Every extension the reference defines for v3, plus one unknown id that
        # a parser must skip rather than choke on.
        "parrot_v3_extensions.mp4": build(
            MIME_PREFIX + "com.parrot.videometadata3",
            [v3_sample(i, timestamp_ext(33333 * i) + automation_ext() + thermal_ext()
                       + ext(0x4599, b"\x00\x00\x00\x00")) for i in range(8)],
            video),
        # An undecoded extension holding coordinates. Real ANAFI recordings carry
        # a 0x4536 extension in every sample that the reference does not describe;
        # if it holds position, passing it through would leak location out of a
        # file reported as scrubbed.
        "parrot_v3_unknown_ext.mp4": build(
            MIME_PREFIX + "com.parrot.videometadata3",
            [v3_sample(i, timestamp_ext(33333 * i)
                       + ext(0x4536, struct.pack(">ii", round(48.858370 * Q10_22),
                                                 round(2.294481 * Q10_22))))
             for i in range(8)],
            video),
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
