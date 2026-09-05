// Copyright 2026 Eric Schneider
// Licensed under the Apache License, Version 2.0
#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "live/parrot_rtp_header_extension.hpp"
#include "misbklv/types.hpp"
#include "session_metadata.hpp"
#include "transform.hpp"

namespace parrot_to_klv {

struct LiveIngestConfig {
    // Video source URI. For live ingest this is typically an RTSP URL
    // (rtsp:// or rtsps://) but hermetic tests may use a GStreamer pipeline
    // description prefixed with "pipeline:" which is forwarded verbatim as
    // the KlvSink video_source. Keeping the field name rtsp_uri for backward
    // compatibility; treat it as the generic live video_source.
    std::string rtsp_uri;  // e.g., rtsp://192.168.0.1/live or pipeline:...
    std::string udp_sink;  // e.g., udp:239.255.0.1:5000 or file:/tmp/out.ts
    std::string udp_mcast_iface;
    int udp_ttl = 1;
    bool udp_loop = true;

    // Optional session metadata and transform options for live KLV mapping.
    // When absent, defaults are used (no session, default TransformOptions).
    std::optional<SessionMetadata> session;
    TransformOptions transform_options;

    // Test-only: when non-empty, injected deterministically after the entry
    // queue clear. This makes hermetic real-decode tests deterministic
    // without an async thread racing the poll window, and lets the normal
    // pipeline: dummy path remain immediate (no 600 ms poll) when no
    // extensions are present.
    std::vector<CapturedExtension> test_injected_extensions;

    // Continuous mode: how long live_ingest should drive before closing.
    // run_duration == nullopt -> no deadline (indefinite live-unbounded). The
    // loop ends on is_mux_interrupted()/SIGINT, idle_timeout when enabled, or
    // a failure from emitting a decoded KLV or from the live pipeline. A finite
    // live source otherwise has to be stopped manually: clean EOS is not
    // observable before close() sends it.
    // Default is timed 2000 ms for safety (safe for tests that forget to set
    // a duration); run_duration has value -> timed drive until deadline or
    // SIGINT (e.g., 2s for hermetic tests). If caller wants one-shot startup
    // poll (previous behavior), use a small run_duration like 600ms.
    std::optional<std::chrono::milliseconds> run_duration{std::chrono::milliseconds(2000)};

    // End the session when no video buffer has been delivered for this
    // long. The primary end-of-source signal (ADR 0018): a clean RTSP teardown
    // is the polite ending, but power loss and hard link failures send no
    // TEARDOWN at all, so idleness is the only signal guaranteed to fire.
    // The timer arms at drive-loop entry, not at the first delivery, so a source
    // that connects and never delivers ends the same way as one that stops.
    // Zero disables it, restoring a run that ends only on its deadline or
    // SIGINT. The signal is video delivery (KlvSink::last_video_delivery),
    // so a video-only stream stays up as long as its video flows.
    std::chrono::seconds idle_timeout{10};

    // Reconnect mode (ADR 0018 mode 2): when a session ends, finalize it and
    // then poll the source on `reconnect_interval` until it returns. Runs until
    // SIGINT/SIGTERM, or until `max_empty_sessions` consecutive sessions
    // connect and carry no telemetry — see that field. Each reconnect builds a
    // fresh pipeline rather than repairing the old one. A file sink rolls
    // through a _000n counter, one finalized capture per sortie; a live sink
    // reuses its address.
    bool reconnect = false;
    std::chrono::seconds reconnect_interval{3};

    // Give up after this many consecutive sessions that opened and then
    // delivered nothing — neither video nor telemetry. A source that is merely absent fails to *open*,
    // which is not counted and is waited on for as long as the run is up —
    // that is the flying-day case this mode exists for. But a session that opens and then fails, over and
    // over with nothing to show, means something retrying cannot fix: a full
    // disk, an output that broke mid-run, an endpoint that accepts and stalls.
    // libmisbklv reports those as a generic Backend error, indistinguishable
    // from a source dying, so a bound is the only defense (see ADR 0018).
    // A link that drops mid-sortie resets the count, because that session
    // delivered video before it died. Zero disables the guard.
    unsigned max_empty_sessions = 5;
};

namespace live_detail {

// Insert a zero-padded counter before the extension of a file sink spec:
// "file:/tmp/flight.ts" with index 2 -> "file:/tmp/flight_0002.ts". A udp: or
// srt: sink has no name to roll and is returned unchanged, since a live output
// reuses its address across reconnects.
std::string roll_sink_spec(const std::string& sink_spec, unsigned index);

// The first index whose rolled path does not exist yet. Re-running in a
// directory that already holds a capture appends rather than destroys —
// libmisbklv's file sink truncates an existing path when it opens, so reusing
// an index would lose the earlier capture. A path that cannot be stat'd counts
// as occupied: an unreadable entry must never be treated as free. Returns 0 for
// a non-file sink, and nullopt when the whole 0..9999 range is taken, which the
// caller must treat as a failure rather than reusing an occupied path.
std::optional<unsigned> first_free_index(const std::string& sink_spec);

}  // namespace live_detail

// Live ingest: validate cfg, register Parrot RTP header extension, create a
// KlvSink with live video_source and realtime=true, push KLV derived from
// telemetry (via to_0601) with PTS anchored to running_time, then close.
// For pipeline: video_source the path is hermetic (no RTSP server needed);
// for rtsp:// the open attempts a 5 s connection and returns SourceUnavailable
// if unreachable — proving the RTSP URI is forwarded and the extension is wired.
misbklv::Result<std::monostate> live_ingest(const LiveIngestConfig& cfg);

}  // namespace parrot_to_klv
