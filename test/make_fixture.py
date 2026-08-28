#!/usr/bin/env python3
"""Generate synthetic MP4 fixtures with a Parrot-style timed metadata track.

Real Parrot footage carries live location data and can never be committed (see
CLAUDE.md § Security), so the extractor is tested against files built here: same
box layout, invented payloads. Track layout follows Parrot's documented shape --
tkhd/tref(cdsc)/hdlr('meta')/nmhd/stsd(TextMetaDataSampleEntry).

All metadata-only fixtures are byte-reproducible across environments. Fixtures
with generated video/audio (such as parrot_v3_video_audio.mp4) depend on the
ffmpeg/AAC encoder version. The committed bytes match ffmpeg 6.1.1
(6.1.1-3ubuntu5), confirmed byte-reproducible across repeated runs with that
build; any other ffmpeg version emits different AAC bytes.

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


def mdhd_v1(timescale: int, duration: int) -> bytes:
    """Version 1 'mdhd': 64-bit creation/modification time and duration,
    the only shape that can carry a duration minimp4 splits into
    duration_hi/duration_lo -- version 0's duration is a bare 32-bit field.
    """
    return full_box(b"mdhd", 1, 0, struct.pack(">QQIQ", 0, 0, timescale, duration)
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


def stsd_avc1(width: int = 320, height: int = 240, avcc: bytes = b"") -> bytes:
    entry = box(b"avc1", b"\x00" * 6 + struct.pack(">H", 1)
                + b"\x00" * 16
                + struct.pack(">HH", width, height)
                + struct.pack(">II", 0x480000, 0x480000)
                + struct.pack(">IHH", 0, 1, 0)
                + b"\x00" * 30
                + struct.pack(">Hh", 24, -1)
                + avcc)
    return full_box(b"stsd", 0, 0, struct.pack(">I", 1) + entry)


def stsd_hvc1(width: int = 320, height: int = 240, hvcc: bytes = b"") -> bytes:
    """An HEVC sample entry. Identical to 'avc1' but for the type and config box.

    The VisualSampleEntry layout is codec-independent, so only the four-character
    code and the trailing configuration box differ.
    """
    entry = box(b"hvc1", b"\x00" * 6 + struct.pack(">H", 1)
                + b"\x00" * 16
                + struct.pack(">HH", width, height)
                + struct.pack(">II", 0x480000, 0x480000)
                + struct.pack(">IHH", 0, 1, 0)
                + b"\x00" * 30
                + struct.pack(">Hh", 24, -1)
                + hvcc)
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
               chunk_offset: int, width: int = 320, height: int = 240,
               config: bytes = b"", codec: str = "avc1") -> bytes:
    vmhd = full_box(b"vmhd", 0, 1, struct.pack(">HHHH", 0, 0, 0, 0))
    sample_desc = (stsd_hvc1(width, height, config) if codec == "hvc1"
                   else stsd_avc1(width, height, config))
    minf = box(b"minf", vmhd + dinf() + stbl(sample_desc, sizes, delta, chunk_offset))
    mdia = box(b"mdia", mdhd(timescale, delta * len(sizes))
               + hdlr(b"vide", "VideoHandler") + minf)
    return box(b"trak", tkhd(track_id, delta * len(sizes), width, height, 0) + mdia)


def stsd_mp4a(asc: bytes, sample_rate: int, channels: int) -> bytes:
    """An AAC sample entry: 'mp4a' carrying the ES descriptor tree in 'esds'."""
    # DecoderSpecificInfo (tag 5) holds the AudioSpecificConfig; the tree above it
    # is fixed boilerplate. Single-byte descriptor lengths, which suffice here.
    def desc(tag: int, payload: bytes) -> bytes:
        return bytes([tag, len(payload)]) + payload

    dsi = desc(0x05, asc)
    dcd = desc(0x04, struct.pack(">BB", 0x40, 0x15)  # MPEG-4 audio, AudioStream
               + b"\x00\x00\x00"                     # bufferSizeDB (24-bit)
               + struct.pack(">II", 0, 0)            # max / avg bitrate
               + dsi)
    es = desc(0x03, struct.pack(">HB", 1, 0) + dcd + desc(0x06, b"\x02"))
    esds = full_box(b"esds", 0, 0, es)

    entry = box(b"mp4a", b"\x00" * 6 + struct.pack(">H", 1)
                + b"\x00" * 8
                + struct.pack(">HH", channels, 16)
                + struct.pack(">HH", 0, 0)
                + struct.pack(">I", sample_rate << 16)
                + esds)
    return full_box(b"stsd", 0, 0, struct.pack(">I", 1) + entry)


def audio_trak(track_id: int, timescale: int, sizes: list[int], delta: int,
               chunk_offset: int, asc: bytes, channels: int) -> bytes:
    smhd = full_box(b"smhd", 0, 0, struct.pack(">hH", 0, 0))
    sample_desc = stsd_mp4a(asc, timescale, channels)
    minf = box(b"minf", smhd + dinf() + stbl(sample_desc, sizes, delta, chunk_offset))
    mdia = box(b"mdia", mdhd(timescale, delta * len(sizes))
               + hdlr(b"soun", "SoundHandler") + minf)
    return box(b"trak", tkhd(track_id, delta * len(sizes), 0, 0, 0x0100) + mdia)


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


def ilst_entry_direct(key: bytes, text: str) -> bytes:
    """One moov/udta/meta/ilst tag using the direct-string shape some writers
    use instead: the string sits straight in the <key> box, no nested 'data'
    box. session_metadata's entry_text() accepts this as a fallback, so the
    scrubber must too.
    """
    return box(key, text.encode())


def session_udta(iso6709: str, direct_ilst: bool = False) -> bytes:
    """moov/udta carrying the geotag plus Parrot's four-character session tags.

    Parrot writes the session metadata two ways -- these iTunes-style keys under
    udta/meta/ilst, and full reverse-DNS keys under moov/meta/ilst. Only the
    first is generated here, matching what the parser reads.

    Note the geotag sits in 'udta' directly, NOT in the ilst -- Parrot does that
    for Android compatibility, and a parser looking only inside ilst misses it.

    `direct_ilst` swaps every entry to the shape `ilst_entry_direct()` builds,
    for fixtures exercising that fallback.
    """
    entry = ilst_entry_direct if direct_ilst else ilst_entry
    geo = box(b"\xa9xyz", struct.pack(">HH", len(iso6709), 0x15C7) + iso6709.encode())
    ilst = box(b"ilst",
               entry(b"\xa9ART", "ANAFI-G059745")      # friendly name
               + entry(b"\xa9mak", "Parrot")            # maker
               + entry(b"\xa9mod", "Anafi")             # model -> Item 10
               + entry(b"\xa9too", "PI040416BA8G059745")  # serial -> Item 4
               + entry(b"\xa9swr", "1.3.0")             # software version
               + entry(b"\xa9nam", "Sat, 06 Oct 2018 18:12:52 +0200")
               + entry(b"\xa9day", "2018-10-06T18:12:52+02:00"))  # media date
    # 'meta' is a FullBox: version/flags precede its children.
    meta = box(b"meta", struct.pack(">I", 0) + ilst)
    return box(b"udta", geo + meta)


def build(mime: str, samples: list[bytes], video_samples: list[bytes],
          timescale: int = 30000, delta: int = 1000,
          content_encoding: str = "", session: bool = True,
          direct_ilst: bool = False,
          video: "H264Track | None" = None,
          audio: "AacTrack | None" = None) -> bytes:
    """Assemble ftyp + mdat + moov. mdat first so chunk offsets are known up front.

    An audio track is optional and, when present, runs on its own timescale: AAC
    frames are 1024 samples at the audio rate, which has nothing to do with the
    video's. That is also what a real recording looks like.
    """
    ftyp = box(b"ftyp", b"isom" + struct.pack(">I", 512) + b"isomiso2avc1mp41")

    video_blob = b"".join(video_samples)
    meta_blob = b"".join(samples)
    audio_blob = b"".join(audio.samples) if audio else b""
    mdat_payload = video_blob + meta_blob + audio_blob
    video_chunk_offset = len(ftyp) + 8
    meta_chunk_offset = video_chunk_offset + len(video_blob)
    audio_chunk_offset = meta_chunk_offset + len(meta_blob)
    mdat = box(b"mdat", mdat_payload)

    duration = delta * max(len(samples), len(video_samples))
    traks = (video_trak(1, timescale, [len(s) for s in video_samples], delta,
                        video_chunk_offset,
                        *( (video.width, video.height, video.config, video.codec)
                           if video else (320, 240, b"", "avc1") ))
             + meta_trak(2, 1, timescale, [len(s) for s in samples], delta,
                         meta_chunk_offset, mime, content_encoding))
    if audio:
        traks += audio_trak(3, audio.sample_rate, [len(s) for s in audio.samples],
                            1024, audio_chunk_offset, audio.asc, audio.channels)
    moov = box(b"moov", mvhd(timescale, duration, 4 if audio else 3)
               + (session_udta("+48.85837+002.29448+135.250/", direct_ilst=direct_ilst)
                  if session else geotag("+48.85837+002.29448+135.250/"))
               + traks)
    return ftyp + mdat + moov


# --- a real H.264 video track ---------------------------------------------
#
# The other fixtures carry five bytes of 0xDEADBEEF per "frame", which is all the
# metadata extractor ever needs -- it reads the *other* track. The mux stage does
# not have that luxury: libmisbklv hands the video elementary stream to a
# GStreamer codec parser, which rejects anything that is not really H.264. So
# this fixture carries a genuine encode.
#
# ffmpeg is needed to *regenerate* the fixture, never to use it: the committed
# .mp4 is what the suite reads, so a machine with no encoder still runs every
# test. Kept tiny (64x64, all-IDR, 8 frames) so the committed bytes stay small
# and no stss box is needed -- with every sample a sync sample, its absence is
# already correct.


class H264Track:
    """AVCC samples plus the avcC decoder config, as an MP4 needs them."""

    codec = "avc1"

    def __init__(self, samples: list[bytes], avcc: bytes, width: int, height: int):
        self.samples = samples
        self.config = avcc
        self.width = width
        self.height = height


class H265Track:
    """Length-prefixed HEVC samples plus the hvcC decoder config."""

    codec = "hvc1"

    def __init__(self, samples: list[bytes], hvcc: bytes, width: int, height: int):
        self.samples = samples
        self.config = hvcc
        self.width = width
        self.height = height


def split_nals(annexb: bytes) -> list[bytes]:
    """Annex-B byte stream -> NAL units, start codes stripped."""
    nals = []
    positions = []
    i = 0
    while i < len(annexb) - 3:
        if annexb[i:i + 3] == b"\x00\x00\x01":
            positions.append((i, 3) if i == 0 or annexb[i - 1] != 0 else (i - 1, 4))
            i += 3
        else:
            i += 1
    for n, (start, code_len) in enumerate(positions):
        end = positions[n + 1][0] if n + 1 < len(positions) else len(annexb)
        nals.append(annexb[start + code_len:end])
    return [n for n in nals if n]


def encode_h264(frames: int, width: int, height: int, fps: int,
                crf: int | None = None) -> H264Track:
    """Encode a test pattern with ffmpeg and repackage it as AVCC samples.

    Access units are cut at each VCL NAL (types 1 and 5): x264 emits one slice
    per picture at this size, so a VCL NAL *is* the end of a frame. Parameter
    sets go into avcC, not into the samples, and AUD/SEI are dropped -- an MP4
    sample is the picture, and h264parse re-inserts what a decoder needs.
    `crf` (when given) trades quality for committed bytes; the default keeps
    the original fixtures' bytes identical.
    """
    import subprocess
    import tempfile

    x264_opts = ["-crf", str(crf)] if crf is not None else []
    with tempfile.NamedTemporaryFile(suffix=".h264") as out:
        subprocess.run(
            ["ffmpeg", "-y", "-loglevel", "error",
             "-f", "lavfi", "-i", f"testsrc=size={width}x{height}:rate={fps}",
             "-frames:v", str(frames), "-c:v", "libx264", "-g", "1",
             *x264_opts,
             "-pix_fmt", "yuv420p", "-f", "h264", out.name],
            check=True)
        annexb = Path(out.name).read_bytes()

    sps, pps, samples, current = None, None, [], b""
    for nal in split_nals(annexb):
        kind = nal[0] & 0x1F
        if kind == 7:
            sps = nal
        elif kind == 8:
            pps = nal
        elif kind in (1, 5):
            current += struct.pack(">I", len(nal)) + nal
            samples.append(current)
            current = b""
    if sps is None or pps is None:
        raise RuntimeError("encoder produced no parameter sets")
    if len(samples) != frames:
        raise RuntimeError(f"expected {frames} access units, cut {len(samples)}")

    # avcC: configurationVersion, profile/compat/level from the SPS, then
    # 0xFF (4-byte NAL length prefixes) and 0xE1 (one SPS).
    avcc_payload = (b"\x01" + sps[1:4] + b"\xff\xe1"
                    + struct.pack(">H", len(sps)) + sps
                    + b"\x01" + struct.pack(">H", len(pps)) + pps)
    return H264Track(samples, box(b"avcC", avcc_payload), width, height)


def rbsp(nal: bytes) -> bytes:
    """Strip emulation-prevention bytes: 00 00 03 -> 00 00.

    A 0x03 is inserted after any 00 00 that would otherwise look like a start
    code. Reading fixed-width fields out of a NAL means undoing that first.
    """
    out = bytearray()
    zeros = 0
    for byte in nal:
        if zeros == 2 and byte == 0x03:
            zeros = 0
            continue
        out.append(byte)
        zeros = zeros + 1 if byte == 0 else 0
    return bytes(out)


def encode_h265(frames: int, width: int, height: int, fps: int) -> H265Track:
    """Encode a test pattern as HEVC and repackage it the way an MP4 wants it.

    Same shape as `encode_h264`: cut an access unit at each VCL NAL, put the
    parameter sets in the config record rather than the samples. HEVC differs in
    three ways that matter here -- a two-byte NAL header, a VPS alongside SPS and
    PPS, and VCL types being 0..31 rather than two specific values.

    This fixture exists to prove an H.265 source still converts (ADR 0007): the
    ST 0604 SEI libmisbklv generates is H.264-only, so asking for it here would
    refuse the session outright rather than degrade.
    """
    import subprocess
    import tempfile

    with tempfile.NamedTemporaryFile(suffix=".h265") as out:
        subprocess.run(
            ["ffmpeg", "-y", "-loglevel", "error",
             "-f", "lavfi", "-i", f"testsrc=size={width}x{height}:rate={fps}",
             "-frames:v", str(frames), "-c:v", "libx265", "-g", "1",
             # One temporal layer keeps the SPS's profile_tier_level exactly 12
             # bytes, which is what hvcC copies verbatim below.
             "-x265-params", "log-level=none:bframes=0",
             "-pix_fmt", "yuv420p", "-f", "hevc", out.name],
            check=True)
        annexb = Path(out.name).read_bytes()

    vps = sps = pps = None
    samples, current = [], b""
    for nal in split_nals(annexb):
        kind = (nal[0] >> 1) & 0x3F
        if kind == 32:
            vps = nal
        elif kind == 33:
            sps = nal
        elif kind == 34:
            pps = nal
        elif kind <= 31:  # VCL: one slice per picture at this size
            current += struct.pack(">I", len(nal)) + nal
            samples.append(current)
            current = b""
    if vps is None or sps is None or pps is None:
        raise RuntimeError("encoder produced no parameter sets")
    if len(samples) != frames:
        raise RuntimeError(f"expected {frames} access units, cut {len(samples)}")

    # hvcC. The 12 bytes of profile_tier_level are copied out of the SPS: they
    # sit after the 2-byte NAL header and one byte holding
    # sps_video_parameter_set_id / max_sub_layers_minus1 / temporal_id_nesting,
    # and with a single temporal layer they are exactly the 12 bytes hvcC wants,
    # in the same order.
    #
    # The copy must come from the *unescaped* payload. x265 emits a constraint
    # field run of zeroes that trips emulation prevention, so the escaped NAL has
    # a 0x03 sitting inside the range -- copying those bytes verbatim silently
    # shifts every field after it.
    ptl = rbsp(sps)[3:15]
    if len(ptl) != 12:
        raise RuntimeError("SPS too short for profile_tier_level")

    def nal_array(nal_type: int, nal: bytes) -> bytes:
        return (struct.pack(">BH", 0x80 | nal_type, 1)
                + struct.pack(">H", len(nal)) + nal)

    hvcc_payload = (b"\x01" + ptl
                    + struct.pack(">H", 0xF000)   # min_spatial_segmentation_idc
                    + b"\xfc"                      # parallelismType
                    + b"\xfd"                      # chromaFormat 4:2:0
                    + b"\xf8" + b"\xf8"            # bit depths, 8-bit
                    + struct.pack(">H", 0)         # avgFrameRate: unspecified
                    + b"\x0f"                      # 4-byte NAL length prefixes
                    + struct.pack(">B", 3)
                    + nal_array(32, vps) + nal_array(33, sps) + nal_array(34, pps))
    return H265Track(samples, box(b"hvcC", hvcc_payload), width, height)


class AacTrack:
    """Raw AAC access units plus the AudioSpecificConfig an 'esds' needs."""

    def __init__(self, samples: list[bytes], asc: bytes, sample_rate: int, channels: int):
        self.samples = samples
        self.asc = asc
        self.sample_rate = sample_rate
        self.channels = channels


def encode_aac(frames: int, sample_rate: int = 44100, channels: int = 1) -> AacTrack:
    """Encode a tone with ffmpeg and strip the ADTS framing MP4 does not use.

    A real Parrot recording carries audio, and until this existed no fixture did
    — which is how an audio-pad bug in the mux reached real footage with the
    suite green. The AudioSpecificConfig is derived from the first ADTS header
    rather than assumed, so it stays right if the encoder picks another profile.
    """
    import subprocess
    import tempfile

    seconds = frames * 1024 / sample_rate
    with tempfile.NamedTemporaryFile(suffix=".aac") as out:
        subprocess.run(
            ["ffmpeg", "-y", "-loglevel", "error",
             "-f", "lavfi", "-i", f"sine=frequency=440:sample_rate={sample_rate}",
             "-t", f"{seconds:.4f}", "-ac", str(channels), "-c:a", "aac",
             "-f", "adts", out.name],
            check=True)
        adts = Path(out.name).read_bytes()

    samples, asc, i = [], None, 0
    while i + 7 <= len(adts):
        if adts[i] != 0xFF or (adts[i + 1] & 0xF0) != 0xF0:
            raise RuntimeError("lost ADTS sync")
        length = ((adts[i + 3] & 0x03) << 11) | (adts[i + 4] << 3) | (adts[i + 5] >> 5)
        header = 7 if (adts[i + 1] & 0x01) else 9  # protection_absent -> no CRC
        if asc is None:
            profile = (adts[i + 2] >> 6) & 0x3          # 0 = AAC-LC, i.e. object type 2
            freq_index = (adts[i + 2] >> 2) & 0xF
            chan = ((adts[i + 2] & 0x1) << 2) | ((adts[i + 3] >> 6) & 0x3)
            asc = struct.pack(">H", ((profile + 1) << 11) | (freq_index << 7) | (chan << 3))
        samples.append(adts[i + header:i + length])
        i += length
    if not samples:
        raise RuntimeError("encoder produced no AAC frames")
    return AacTrack(samples, asc, sample_rate, channels)


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


def automation_without_target_ext() -> bytes:
    # The all-zero framing target is v3's no-target sentinel. Keep the flight
    # destination populated so the extension itself is present and decodable.
    payload = struct.pack(">iii", 0, 0, 0)
    payload += struct.pack(">iii", round(48.87 * Q10_22), round(2.31 * Q10_22),
                           round(150.0 * Q16_16))
    payload += struct.pack(">BBH", 5, 0x01 | 0x04, 0)
    return ext(0x4533, payload)


def short_automation_ext() -> bytes:
    # A v3 automation id with only the first coordinate triple. The parser
    # cannot decode it as AutomationExt, so the scrubber must treat it as
    # unknown rather than letting its location-bearing bytes pass through.
    payload = struct.pack(">iii", round(48.858370 * Q10_22),
                          round(2.294481 * Q10_22), round(135.25 * Q16_16))
    return ext(0x4533, payload)


def oversized_automation_ext() -> bytes:
    # A valid automation payload followed by a third coordinate triple. The
    # parser understands the prefix but the scrubber must clear the tail.
    payload = struct.pack(">iii", round(48.86 * Q10_22), round(2.30 * Q10_22),
                          round(140.0 * Q16_16))
    payload += struct.pack(">iii", round(48.87 * Q10_22), round(2.31 * Q10_22),
                           round(150.0 * Q16_16))
    payload += struct.pack(">BBH", 5, 0x01 | 0x04, 0)
    payload += struct.pack(">iii", round(48.858370 * Q10_22),
                           round(2.294481 * Q10_22), round(135.25 * Q16_16))
    return ext(0x4533, payload)


def truncated_unknown_ext() -> bytes:
    # Declares 40 payload bytes but provides only 8. The parser records the id
    # and stops; the scrubber must still clear the bytes that are present.
    payload = struct.pack(">ii", round(48.858370 * Q10_22), round(2.294481 * Q10_22))
    return struct.pack(">HH", 0x4536, 10) + payload


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


def build_out_of_bounds_chunk_offset() -> bytes:
    """A valid Parrot metadata track, plus a second track whose 'stco' chunk
    offset (0xFFFFFFF0) points nowhere in this ~1 KB file.

    This is the regression test for the bounds-check hardening in issue #3:
    `offset + frame_bytes > file.size()` was written as a single comparison
    that a much larger offset (a 'co64' entry, near UINT64_MAX) could wrap
    past, then casting the wrapped offset to `long` walks the write out of
    bounds. That exact wrap turns out not to be reachable in this build --
    minimp4 refuses to open any file with a 'co64' entry whose upper 32 bits
    are set at all (MP4D_64BIT_SUPPORTED is off by default here), so a real
    offset never exceeds 32 bits and the 64-bit addition below cannot
    overflow. The subtraction-form check lands anyway, as defense in depth
    against a future minimp4 config or version where that guard is gone; this
    fixture pins the ordinary case the fix must not regress: an offset
    genuinely past the end of the file is rejected, and nothing is written.
    """
    ftyp = box(b"ftyp", b"isom" + struct.pack(">I", 512) + b"isomiso2avc1mp41")
    meta_sample = v3_sample(0, timestamp_ext(0))
    mdat = box(b"mdat", meta_sample)
    meta_chunk_offset = len(ftyp) + 8
    traks = meta_trak(1, 2, 30000, [len(meta_sample)], 1000, meta_chunk_offset,
                      MIME_PREFIX + "com.parrot.videometadata3")
    traks += video_trak(2, 30000, [32], 1000, 0xFFFFFFF0)
    moov = box(b"moov", mvhd(30000, 1000, 3) + traks)
    return ftyp + mdat + moov


def build_missing_stsc() -> bytes:
    """A Parrot metadata track whose 'stsc' (sample-to-chunk) box is missing
    entirely, while 'stsz' declares two samples and 'stco' declares two
    chunks -- the NULL-deref regression test for issue #3.

    minimp4's sample_to_chunk() short-circuits `chunk_count <= 1` to a safe
    early return, so *one* chunk never reaches the bug -- it takes two to
    reach the loop body, which dereferences tr->sample_to_chunk[0]
    unconditionally on its very first iteration, with no null check.
    MP4D_open() itself does not validate that a 'stsc' box was even present.
    A hostile file that pairs a populated multi-entry 'stco' with a missing
    'stsc' used to crash inside MP4D_frame_offset(); the wrapper now
    validates every mandatory sample-index table is non-null (and non-empty)
    right after MP4D_open() succeeds, and refuses the track instead.
    """
    ftyp = box(b"ftyp", b"isom" + struct.pack(">I", 512) + b"isomiso2avc1mp41")
    sample0 = v3_sample(0, timestamp_ext(0))
    sample1 = v3_sample(1, timestamp_ext(33333))
    mdat = box(b"mdat", sample0 + sample1)
    chunk0_offset = len(ftyp) + 8
    chunk1_offset = chunk0_offset + len(sample0)

    stts = full_box(b"stts", 0, 0, struct.pack(">III", 1, 2, 1000))
    stsz = full_box(b"stsz", 0, 0, struct.pack(">II", 0, 2)
                    + struct.pack(">I", len(sample0)) + struct.pack(">I", len(sample1)))
    stco = full_box(b"stco", 0, 0, struct.pack(">III", 2, chunk0_offset, chunk1_offset))
    # No stsc box at all, despite two chunks -- nothing says how the two
    # samples map onto them.
    stbl_box = box(b"stbl", stsd_mett(MIME_PREFIX + "com.parrot.videometadata3")
                   + stts + stsz + stco)
    minf = box(b"minf", full_box(b"nmhd", 0, 0, b"") + dinf() + stbl_box)
    mdia = box(b"mdia", mdhd(30000, 2000) + hdlr(b"meta", "ParrotMetadata") + minf)
    trak = box(b"trak", tkhd(1, 2000, 0, 0, 0) + mdia)
    moov = box(b"moov", mvhd(30000, 2000, 2) + trak)
    return ftyp + mdat + moov


def build_oversized_sample_size() -> bytes:
    """A Parrot metadata sample whose 'stsz' entry declares 2 MiB, far past
    extract_sample()'s 1 MiB sanity cap -- the unbounded-allocation
    regression test for issue #3.

    The real mdat payload is a normal, tiny sample; only the size *field* in
    'stsz' is hostile. That is exactly the shape a crafted file would use to
    drive std::vector::resize() into an oversized allocation before a single
    byte of the (much smaller) real payload is ever read.
    """
    ftyp = box(b"ftyp", b"isom" + struct.pack(">I", 512) + b"isomiso2avc1mp41")
    sample = v3_sample(0, timestamp_ext(0))
    mdat = box(b"mdat", sample)
    chunk_offset = len(ftyp) + 8

    lied_size = 2 * 1024 * 1024  # 2 MiB declared; not actually present
    stts = full_box(b"stts", 0, 0, struct.pack(">III", 1, 1, 1000))
    stsc = full_box(b"stsc", 0, 0, struct.pack(">IIII", 1, 1, 1, 1))
    stsz = full_box(b"stsz", 0, 0, struct.pack(">II", 0, 1) + struct.pack(">I", lied_size))
    stco = full_box(b"stco", 0, 0, struct.pack(">II", 1, chunk_offset))
    stbl_box = box(b"stbl", stsd_mett(MIME_PREFIX + "com.parrot.videometadata3")
                   + stts + stsc + stsz + stco)
    minf = box(b"minf", full_box(b"nmhd", 0, 0, b"") + dinf() + stbl_box)
    mdia = box(b"mdia", mdhd(30000, 1000) + hdlr(b"meta", "ParrotMetadata") + minf)
    trak = box(b"trak", tkhd(1, 1000, 0, 0, 0) + mdia)
    moov = box(b"moov", mvhd(30000, 1000, 2) + trak)
    return ftyp + mdat + moov


def build_stsz_count_overflow() -> bytes:
    """A Parrot metadata track whose 'stsz' declares a sample_count near
    0x40000000 -- the integer-overflow regression test for issue #27.

    minimp4 computed `sample_count * 4` in native 32-bit arithmetic before
    the fix landed upstream (commit 5a212a1..df109a3): a count this large
    wraps that product to a few bytes, so the allocation it drives is far
    smaller than the (un-overflowed) loop that then fills it -- a heap
    buffer overflow write, reachable from a few hundred bytes on disk with
    no real per-sample payload behind the declared count at all (`READ()`
    clamps to zero-padding once the box's real bytes run out). Fixed by
    re-vendoring minimp4.h onto that upstream commit, which widens the
    computation to uint64_t before the size reaches minimp4_bounded_malloc's
    ceiling. open() must reject this track, not allocate into it.
    """
    ftyp = box(b"ftyp", b"isom" + struct.pack(">I", 512) + b"isomiso2avc1mp41")
    sample = v3_sample(0, timestamp_ext(0))
    mdat = box(b"mdat", sample)
    chunk_offset = len(ftyp) + 8

    evil_count = 0x40000000  # sample_count*4 wraps to 0 in 32-bit unsigned math
    stts = full_box(b"stts", 0, 0, struct.pack(">III", 1, 1, 1000))
    stsc = full_box(b"stsc", 0, 0, struct.pack(">IIII", 1, 1, 1, 1))
    stsz = full_box(b"stsz", 0, 0, struct.pack(">II", 0, evil_count))
    stco = full_box(b"stco", 0, 0, struct.pack(">II", 1, chunk_offset))
    stbl_box = box(b"stbl", stsd_mett(MIME_PREFIX + "com.parrot.videometadata3")
                   + stts + stsc + stsz + stco)
    minf = box(b"minf", full_box(b"nmhd", 0, 0, b"") + dinf() + stbl_box)
    mdia = box(b"mdia", mdhd(30000, 1000) + hdlr(b"meta", "ParrotMetadata") + minf)
    trak = box(b"trak", tkhd(1, 1000, 0, 0, 0) + mdia)
    moov = box(b"moov", mvhd(30000, 1000, 2) + trak)
    return ftyp + mdat + moov


def build_hostile_mdhd_duration() -> bytes:
    """A Parrot metadata track whose 'mdhd' (version 1, 64-bit duration)
    declares a duration near UINT64_MAX -- the integer-overflow regression
    test for issue #4.

    `duration_hi<<32|duration_lo, times 1'000'000` used to be computed in
    native uint64_t arithmetic before dividing by timescale, wrapping to
    garbage for any declared duration past ~1.8e13 units -- reachable from a
    hostile 'mdhd' with no real payload elsewhere. `duration_us` must come out
    bounded to a sane ceiling instead of wrapped.
    """
    ftyp = box(b"ftyp", b"isom" + struct.pack(">I", 512) + b"isomiso2avc1mp41")
    sample = v3_sample(0, timestamp_ext(0))
    mdat = box(b"mdat", sample)
    chunk_offset = len(ftyp) + 8

    stbl_box = box(b"stbl", stsd_mett(MIME_PREFIX + "com.parrot.videometadata3")
                   + full_box(b"stts", 0, 0, struct.pack(">III", 1, 1, 1000))
                   + full_box(b"stsc", 0, 0, struct.pack(">IIII", 1, 1, 1, 1))
                   + full_box(b"stsz", 0, 0, struct.pack(">II", 0, 1) + struct.pack(">I", len(sample)))
                   + full_box(b"stco", 0, 0, struct.pack(">II", 1, chunk_offset)))
    minf = box(b"minf", full_box(b"nmhd", 0, 0, b"") + dinf() + stbl_box)
    mdia = box(b"mdia", mdhd_v1(1, 0xFFFFFFFFFFFFFFFF)
               + hdlr(b"meta", "ParrotMetadata") + minf)
    trak = box(b"trak", tkhd(1, 1000, 0, 0, 0) + mdia)
    moov = box(b"moov", mvhd(30000, 1000, 2) + trak)
    return ftyp + mdat + moov


def build_tkhd_version_2() -> bytes:
    """A Parrot metadata track whose 'tkhd' declares version 2, with 64-bit
    creation/modification times (like version 1) rather than version 0's
    32-bit ones -- the unknown-layout regression test for issue #4.

    ISO/IEC 14496-12 defines no tkhd version past 1; the old code treated any
    version other than 1 as version 0's 8-byte time layout, so a version-2
    box with wider (16-byte) times had its track_id read 8 bytes into the
    wrong field. This fixture plants a sentinel (0xDEADBEEF) in the first
    half of 'modification_time' (body offset 12, reached by seeking
    body_begin+4 -- past version_and_flags -- then 8 more bytes), exactly
    where the old 8-byte-times assumption would misread it as track_id,
    while the real track_id (1) sits 8 bytes further out at body offset 20,
    where only the correct 16-byte-times offset reaches it. open() must
    still succeed (version only affects box-scan's supplementary track_id,
    not minimp4's own independent parse), but box-scan must not report *any*
    track_id for this track rather than the sentinel -- the whole point of
    rejecting a version it does not know the layout of.
    """
    ftyp = box(b"ftyp", b"isom" + struct.pack(">I", 512) + b"isomiso2avc1mp41")
    samples = [v3_sample(i, timestamp_ext(33333 * i)) for i in range(2)]
    mdat = box(b"mdat", b"".join(samples))
    chunk_offset = len(ftyp) + 8

    stbl_box = box(b"stbl", stsd_mett(MIME_PREFIX + "com.parrot.videometadata3")
                   + full_box(b"stts", 0, 0, struct.pack(">III", 1, 2, 1000))
                   + full_box(b"stsc", 0, 0, struct.pack(">IIII", 1, 1, 1, 1))
                   + full_box(b"stsz", 0, 0, struct.pack(">II", 0, 2)
                              + struct.pack(">II", len(samples[0]), len(samples[1])))
                   + full_box(b"stco", 0, 0, struct.pack(">II", 1, chunk_offset)))
    minf = box(b"minf", full_box(b"nmhd", 0, 0, b"") + dinf() + stbl_box)
    mdia = box(b"mdia", mdhd(30000, 2000) + hdlr(b"meta", "ParrotMetadata") + minf)
    tkhd_v2 = full_box(
        b"tkhd", 2, 7,
        struct.pack(">Q", 0)                       # creation_time (8 bytes, like v1)
        + struct.pack(">I", 0xDEADBEEF) + struct.pack(">I", 0)  # modification_time,
                                                     # sentinel in its high 4 bytes
                                                     # (body offset 12 -- where the
                                                     # old 8-byte-times bug reads)
        + struct.pack(">I", 1)                      # track_id -- only correct at the
                                                     # 16-byte-times offset (offset 20)
        + struct.pack(">I", 0)                      # reserved
        + struct.pack(">I", 2000)                   # duration
        + b"\x00" * 8
        + struct.pack(">hhhh", 0, 0, 0, 0)
        + UNITY_MATRIX
        + struct.pack(">II", 0, 0))
    trak = box(b"trak", tkhd_v2 + mdia)
    moov = box(b"moov", mvhd(30000, 2000, 2) + trak)
    return ftyp + mdat + moov


def build_hostile_read_box_largesize() -> bytes:
    """moov/udta/meta/ilst carrying one real tag ('©day'), followed by a
    hostile '©ART' box whose 64-bit largesize is set near UINT64_MAX -- a
    hardening/defense-in-depth fixture for issue #4's `read_box` finding.

    session_metadata's own `read_box` used to bounds-check a child against
    the in-memory 'moov' buffer with `pos + size > limit`, computed in
    size_t, which can wrap for a size this large. The fixed check
    ('size > limit - pos') cannot wrap. For this box's specific shape (a
    64-bit largesize always has a fixed 16-byte header), the wrapped sum can
    only ever land at or below body_begin, so a downstream guard
    (`entry_text`'s `body_begin < body_end`) already happens to absorb this
    exact case even pre-fix -- see the discussion in session_metadata_test's
    `check_hostile_largesize_is_rejected`. The fix is still correct
    (a bounds check should not rely on a downstream guard it does not
    control); this fixture exists to pin that the malformed box does not
    corrupt the real '©day' tag immediately before it.
    """
    ftyp = box(b"ftyp", b"isom" + struct.pack(">I", 512) + b"isomiso2avc1mp41")
    day = ilst_entry(b"\xa9day", "2018-10-06T18:12:52+02:00")
    # A largesize box: 32-bit size field = 1 (the "read the next 8 bytes"
    # marker), type '\xa9ART', then an 8-byte size near UINT64_MAX. No real
    # payload follows -- the box claims to run far past the end of the file.
    hostile_artist = (struct.pack(">I", 1) + b"\xa9ART" +
                      struct.pack(">Q", 0xFFFFFFFFFFFFFFF0))
    ilst = box(b"ilst", day + hostile_artist)
    meta = box(b"meta", struct.pack(">I", 0) + ilst)
    udta = box(b"udta", meta)
    moov = box(b"moov", mvhd(30000, 1000, 1) + udta)
    return ftyp + moov


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
        # A valid v3 automation extension can carry no framing target, and a
        # frame can omit the optional monotonic timestamp extension entirely.
        "parrot_v3_absent_data.mp4": build(
            MIME_PREFIX + "com.parrot.videometadata3",
            [v3_sample(i, automation_without_target_ext()) for i in range(8)],
            video),
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
        # A short automation extension must be treated as unknown, not as a
        # valid automation payload whose location fields can be left intact.
        "parrot_v3_short_automation.mp4": build(
            MIME_PREFIX + "com.parrot.videometadata3",
            [v3_sample(i, timestamp_ext(33333 * i) + short_automation_ext())
             for i in range(8)],
            video),
        # A valid automation prefix followed by an undecoded coordinate triple
        # must not let the extension's surplus bytes pass through.
        "parrot_v3_oversized_automation.mp4": build(
            MIME_PREFIX + "com.parrot.videometadata3",
            [v3_sample(i, timestamp_ext(33333 * i) + oversized_automation_ext())
             for i in range(8)],
            video),
        # A trailing extension can declare more bytes than remain in the
        # sample. Its present bytes are still sensitive and must be cleared.
        "parrot_v3_truncated_ext.mp4": build(
            MIME_PREFIX + "com.parrot.videometadata3",
            [v3_sample(i, timestamp_ext(33333 * i) + truncated_unknown_ext())
             for i in range(8)],
            video),
        # A metadata track that is not Parrot's -- must be rejected, not guessed.
        "foreign_meta.mp4": build("application/octet-stream;type=com.example.telemetry",
                                  samples, video),
        # Session metadata written with the direct-string ilst shape (no nested
        # 'data' box) instead of the usual one -- exercises entry_text()'s and
        # the scrubber's fallback for that shape.
        "parrot_v3_direct_ilst.mp4": build(MIME_PREFIX + "com.parrot.videometadata3",
                                           samples, video, direct_ilst=True),
        # A second track's chunk offset points past the end of the file --
        # the bounds-check regression test for issue #3.
        "parrot_out_of_bounds_chunk_offset.mp4": build_out_of_bounds_chunk_offset(),
        # 'stsc' missing entirely -- the NULL-deref regression test for
        # issue #3.
        "parrot_missing_stsc.mp4": build_missing_stsc(),
        # 'stsz' declares a 2 MiB sample -- the unbounded-allocation
        # regression test for issue #3.
        "parrot_oversized_sample_size.mp4": build_oversized_sample_size(),
        # 'stsz' declares a sample_count near 0x40000000, overflowing the
        # 32-bit sample_count*4 allocation size -- the regression test for
        # issue #27.
        "parrot_stsz_count_overflow.mp4": build_stsz_count_overflow(),
        # 'mdhd' (v1) declares a duration near UINT64_MAX -- the
        # integer-overflow regression test for issue #4.
        "parrot_hostile_mdhd_duration.mp4": build_hostile_mdhd_duration(),
        # 'tkhd' declares version 2, which this project's box scan does not
        # know the layout of -- the regression test for issue #4.
        "parrot_tkhd_version_2.mp4": build_tkhd_version_2(),
        # A '©ART' box declares a 64-bit largesize near UINT64_MAX -- the
        # size_t-wrap regression test for issue #4.
        "hostile_read_box_largesize.mp4": build_hostile_read_box_largesize(),
    }
    # The only fixture whose video track is real H.264 -- the mux stage feeds
    # that stream to gstreamer, which will not accept the dummy bytes above.
    try:
        h264 = encode_h264(frames=len(samples), width=64, height=64, fps=30)
    except (FileNotFoundError, RuntimeError) as err:
        print(f"parrot_v3_video.mp4: SKIPPED ({err}) -- needs ffmpeg with libx264",
              file=sys.stderr)
        h264 = None
    else:
        fixtures["parrot_v3_video.mp4"] = build(
            MIME_PREFIX + "com.parrot.videometadata3", samples, h264.samples,
            video=h264)
        fixtures["parrot_proto_video.mp4"] = build(
            MIME_PREFIX + "com.parrot.videometadataproto",
            [proto_sample(i) for i in range(8)], h264.samples, video=h264)
        # The shape of a real recording: video + audio + metadata. A source with
        # an audio track exercises a path the other fixtures cannot — the muxer
        # has to *drop* that track while still linking its pad.
        aac = encode_aac(frames=12)
        fixtures["parrot_v3_video_audio.mp4"] = build(
            MIME_PREFIX + "com.parrot.videometadata3", samples, h264.samples,
            video=h264, audio=aac)
        # Every metadata sample undecodable (too short for even the base
        # struct), paired with a video track that mux/decode cleanly -- the
        # "converted nothing" regression fixture for issue #10: the video
        # branch alone is enough for GStreamer's EOS to look like success.
        fixtures["parrot_v3_video_undecodable_metadata.mp4"] = build(
            MIME_PREFIX + "com.parrot.videometadata3",
            [b"\xff\xff\xff\xff" for _ in samples], h264.samples, video=h264)

        # The shape that makes a realtime replay dangerous: video far outliving
        # its metadata, so after the last KLV sample the sink's close() drains
        # the remaining video at wall-clock speed. The file_replay drain-tail
        # interrupt regression (issue #63) replays this fixture and asserts an
        # interrupt cuts that tail short instead of playing it out.
        try:
            h264_tail = encode_h264(frames=180, width=64, height=64, fps=30,
                                    crf=40)
        except (FileNotFoundError, RuntimeError) as err:
            print(f"parrot_v3_video_tail.mp4: SKIPPED ({err}) -- needs ffmpeg "
                  "with libx264", file=sys.stderr)
        else:
            fixtures["parrot_v3_video_tail.mp4"] = build(
                MIME_PREFIX + "com.parrot.videometadata3", samples,
                h264_tail.samples, video=h264_tail)

    # An H.265 source takes the other branch of fork 7 (ADR 0007): it converts,
    # but carries no ST 0604, because generation is H.264-only upstream and
    # asking for it on HEVC refuses the session rather than degrading.
    try:
        h265 = encode_h265(frames=len(samples), width=64, height=64, fps=30)
    except (FileNotFoundError, RuntimeError) as err:
        print(f"parrot_v3_video_h265.mp4: SKIPPED ({err}) -- needs ffmpeg with libx265",
              file=sys.stderr)
        h265 = None
    else:
        fixtures["parrot_v3_video_h265.mp4"] = build(
            MIME_PREFIX + "com.parrot.videometadata3", samples, h265.samples,
            video=h265)

    for name, data in fixtures.items():
        (out_dir / name).write_bytes(data)
        print(f"{name}: {len(data)} bytes")
    return 0 if h264 else 1


if __name__ == "__main__":
    sys.exit(main())
