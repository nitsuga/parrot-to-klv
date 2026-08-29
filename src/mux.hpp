// Copyright 2026 Eric Schneider
// Licensed under the Apache License, Version 2.0

#pragma once

#include <cstdint>
#include <string>

#include "transform.hpp"

namespace parrot_to_klv {

/// What one conversion produced.
///
/// `error` empty means the output file was written; anything else is the reason
/// it was not, and the other counts describe how far the run got.
struct MuxReport {
    uint32_t samples_seen = 0;      ///< metadata samples the track declared
    uint32_t frames_emitted = 0;    ///< ST 0601 packets muxed into the output
    uint32_t frames_skipped = 0;    ///< samples that did not decode to a packet
    /// Coverage across every emitted frame, not just the first — the mapping
    /// is not guaranteed static across a recording (an out-of-range attitude
    /// sentinel, a thermal extension starting mid-flight, a field dropping
    /// in and out), and a single frame's snapshot would hide that drift.
    AggregatedCoverage coverage;
    /// The video track's sample entry format ('avc1', 'hvc1', …), 0 if none.
    uint32_t video_sample_entry = 0;
    /// Whether the output's video carries ST 0604 Precision Time Stamps.
    ///
    /// False for a non-H.264 source: generation is H.264-only upstream, so such
    /// a recording is carried through unstamped rather than refused (ADR 0007).
    /// A consumer that reads time from the elementary stream must check this.
    bool st0604_generated = false;
    std::string error;
};

/// Convert `input` (a Parrot MP4) into an MPEG-TS at `output`, carrying the
/// source's video elementary stream plus a synchronous ST 0601 KLV stream.
///
/// The video is **never decoded or re-encoded** — parsed only — so the output
/// holds the same H.264/H.265 the drone recorded, with no generation loss.
///
/// For an **H.264** source it is *not* bit-identical: libmisbklv rewrites the
/// timing NALs, stripping Picture Timing SEI and injecting ST 0604 from the KLV
/// sensor timestamp, and repeats parameter sets per IDR (ADR 0006). For any
/// **other codec** the elementary stream is carried unchanged and carries no ST
/// 0604 — generation is H.264-only upstream, and asking for it elsewhere would
/// refuse the conversion outright (ADR 0007). `MuxReport::st0604_generated`
/// says which happened. Each successfully decoded and mapped metadata sample
/// becomes one KLV packet, timestamped with that sample's own presentation time
/// from the start of the file, which is the timeline libmisbklv gives the video
/// branch.
///
/// Samples that fail to decode are skipped and counted rather than aborting the
/// conversion: one corrupt sample in a flight should cost that frame's
/// telemetry, not the file.
///
/// Refuses the operation if `input` and `output` identify the same file,
/// including through normalized paths, symbolic links, or hard links. The
/// check happens before the output is opened, so the source cannot be truncated
/// while the video branch still needs it.
///
/// `options` is passed through to `to_0601` for every frame — the mapping
/// relaxations are a property of the conversion, not of the muxing.
MuxReport mux_to_ts(const std::string& input, const std::string& output,
                    const TransformOptions& options = {});

/// Ask `mux_to_ts()` to stop at the next sample boundary and report the run as
/// interrupted rather than finishing it, so the output it created is discarded
/// like any other failed conversion instead of being left half-written.
///
/// Async-signal-safe — only sets a `sig_atomic_t` flag — so it is safe to call
/// from a signal handler. It does not itself install one; `main.cpp` does that
/// for SIGINT/SIGTERM. A signal that arrives while `mux_to_ts()` is blocked
/// inside a single sample's push (backpressure on the sink) is not seen until
/// that call returns — this is cooperative, not preemptive, cancellation.
void request_mux_interrupt();
bool is_mux_interrupted();
/// Clears the interrupt flag. Test-only: lets a test suite run more than one
/// interrupt scenario in one process. Not async-signal-safe; never call it
/// from production code or a handler.
void reset_mux_interrupt_for_test();

}  // namespace parrot_to_klv
