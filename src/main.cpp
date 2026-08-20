// Copyright 2026 Eric Schneider
// Licensed under the Apache License, Version 2.0

#include "live/live_ingest.hpp"
#include "mp4_box_scan.hpp"
#include "mp4_metadata_extractor.hpp"
#include "mux.hpp"
#include "parrot_proto.hpp"
#include "parrot_v3.hpp"
#include "session_metadata.hpp"
#include "telemetry.hpp"
#include "transform.hpp"

#include <algorithm>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <optional>
#include <span>
#include <string>

using namespace parrot_to_klv;

namespace {

/// Set from `project(VERSION)` at configure time. The release artifacts take
/// their name from the git tag, so this is the one place the binary itself can
/// say what it is — and the release procedure bumps the two together.
#ifndef PARROT_TO_KLV_VERSION
#define PARROT_TO_KLV_VERSION "unknown"
#endif
constexpr const char* kVersion = PARROT_TO_KLV_VERSION;

/// Structural summary of a decoded frame.
///
/// Deliberately omits latitude/longitude: this is meant to be safe to paste
/// into a bug report or a chat, and telemetry from a real flight carries the
/// operator's location. Use --with-location when you actually need to see it.
void print_frame(const ParrotV3Frame& f, bool with_location) {
    std::cout << "    state=" << to_string(f.flying_state)
              << " mode=" << to_string(f.piloting_mode)
              << " battery=" << int{f.battery_percentage} << "%"
              << " rssi=" << int{f.wifi_rssi_dbm} << "dBm"
              << " sv=" << int{f.sv_count} << "\n";
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "    agl=" << f.ground_distance_m << "m"
              << " speed_ned=(" << f.north_speed_mps << ", " << f.east_speed_mps << ", "
              << f.down_speed_mps << ")m/s"
              << " fov=" << f.picture_hfov_deg << "x" << f.picture_vfov_deg << "deg\n";
    std::cout << "    |drone_quat|=" << f.drone_quat.norm()
              << " |frame_quat|=" << f.frame_quat.norm();
    if (f.frame_timestamp_us) {
        std::cout << " t=" << *f.frame_timestamp_us << "us";
    }
    std::cout << "\n";
    if (with_location) {
        std::cout << std::setprecision(7) << "    lat=" << f.latitude_deg
                  << " lon=" << f.longitude_deg << std::setprecision(3)
                  << " alt_amsl=" << f.altitude_amsl_m << "m\n";
    }
    if (f.automation) std::cout << "    + automation extension\n";
    if (f.thermal) std::cout << "    + thermal extension\n";
    for (uint16_t id : f.skipped_extension_ids) {
        std::cout << "    + unknown extension 0x" << std::hex << id << std::dec << " (skipped)\n";
    }
    std::cout << std::defaultfloat;
}

/// Structural summary of a decoded proto frame.
///
/// Withholds location for the same reason as the v3 summary above. The units
/// differ from v3 and the output says so: the proto format carries the FOVs in
/// radians, and both altitude datums rather than one.
void print_frame(const ParrotProtoFrame& f, bool with_location) {
    std::cout << "    state=" << to_string(f.flying_state)
              << " mode=" << to_string(f.piloting_mode)
              << " battery=" << f.battery_percentage << "%";
    if (!f.wifi_links.empty()) {
        std::cout << " rssi=" << f.wifi_links[0].rssi_dbm << "dBm";
    }
    std::cout << " sv=" << f.drone_location.sv_count << "\n";
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "    agl=" << f.ground_distance_m << "m"
              << " speed_ned=(" << f.speed.north_mps << ", " << f.speed.east_mps << ", "
              << f.speed.down_mps << ")m/s"
              << " fov=" << f.picture_hfov_rad << "x" << f.picture_vfov_rad << "rad\n";
    std::cout << "    |drone_quat|=" << f.drone_quat.norm()
              << " |frame_quat|=" << f.frame_quat.norm()
              << " t=" << f.camera_timestamp_us << "us";
    // Zero accuracy means the UTC stamp is not usable, which matters more than
    // the value itself.
    if (f.utc_timestamp_accuracy == 0) {
        std::cout << " utc=invalid";
    }
    std::cout << "\n";
    if (f.zoom_level != 0.0) std::cout << "    zoom=" << f.zoom_level << "x\n";
    if (with_location) {
        std::cout << std::setprecision(7) << "    lat=" << f.drone_location.latitude_deg
                  << " lon=" << f.drone_location.longitude_deg << std::setprecision(3)
                  << " alt_amsl=" << f.drone_location.altitude_egm96_m << "m"
                  << " alt_wgs84=" << f.drone_location.altitude_wgs84_m << "m\n";
    }
    if (!f.lfic.empty()) {
        std::cout << "    + " << f.lfic.size() << " LFIC entry(ies)\n";
    }
    if (f.tracking) std::cout << "    + tracking\n";
    for (uint32_t number : f.skipped_field_numbers) {
        std::cout << "    + field " << number << " (skipped)\n";
    }
    std::cout << std::defaultfloat;
}

/// Decode every sample of one format, printing the first few, plus a coverage
/// summary aggregated across every decoded sample when `--klv` is passed.
///
/// Templated over the frame type so v3 and proto share the loop: the formats
/// differ in what a frame *is*, not in how a track is walked.
template <typename Parse, typename Normalize>
int decode_all(Mp4MetadataExtractor& extractor, const SessionMetadata& session,
               Disclosure disclosure, bool show_klv, const TransformOptions& options,
               Parse parse, Normalize normalize) {
    uint32_t decoded = 0;
    uint32_t failed = 0;
    const uint32_t to_show = std::min(3u, extractor.sample_count());
    // Coverage across every decoded sample, not just the first few printed in
    // full below -- an attitude sentinel or a mid-flight extension change is
    // otherwise invisible past sample 2.
    AggregatedCoverage totals;

    // The first sample's clock is the origin the session media date anchors to.
    // Left empty (never defaulted to zero) if the origin sample's own
    // extraction or parse fails -- anchor_to_session treats an unknown origin
    // as "do not anchor" rather than assuming a zero clock base.
    std::optional<uint64_t> first_monotonic_us;
    if (auto first = extractor.extract_sample(0)) {
        std::string ignored;
        if (auto frame = parse(first->data, ignored)) {
            first_monotonic_us = normalize(*frame).monotonic_us;
        }
    }

    for (uint32_t i = 0; i < extractor.sample_count(); ++i) {
        auto sample = extractor.extract_sample(i);
        if (!sample) {
            ++failed;
            continue;
        }
        std::string error;
        auto frame = parse(sample->data, error);
        if (!frame) {
            if (failed == 0) std::cerr << "Sample " << i << ": " << error << "\n";
            ++failed;
            continue;
        }
        ++decoded;
        // Computed for every sample when --klv is on (not just the ones shown
        // in full below) so `totals` covers the whole recording.
        std::optional<Klv0601Frame> klv;
        if (show_klv) {
            auto snapshot = normalize(*frame);
            // v3 frames carry only a monotonic clock; the session's media
            // date is what turns it into wall time.
            anchor_to_session(snapshot, session, first_monotonic_us);
            klv = to_0601(snapshot, &session, options);
            totals.accumulate(klv->coverage);
        }
        if (i < to_show) {
            std::cout << "  Sample " << i << " (" << sample->data.size() << " bytes):\n";
            print_frame(*frame, disclosure.location);
            if (klv) {
                std::cout << "    -> ST 0601 packet: " << klv->packet.size() << " bytes\n"
                          << klv->coverage.to_text(disclosure);
            }
        }
    }

    if (show_klv && totals.frames_counted > 0) {
        std::cout << "\n" << totals.to_text();
    }

    std::cout << "\nDecoded " << decoded << " of " << extractor.sample_count() << " samples";
    if (failed > 0) std::cout << " (" << failed << " failed)";
    std::cout << "\n";
    return failed > 0 ? 1 : 0;
}

/// The usage text, shared by `--help` and the no-arguments path so the two can
/// never drift. `--help` sends it to stdout and succeeds; a missing argument
/// sends it to stderr and fails.
void print_usage(std::ostream& out, const char* program) {
    out << "Usage: " << program << " <input.mp4> [-o <output.ts>] [options]\n\n"
        << "  -o/--output converts: writes an MPEG-TS carrying the source's\n"
        << "  video plus an ST 0601 KLV stream. The video is never decoded or\n"
        << "  re-encoded, but its timing NALs are rewritten: ST 0604 SEI is\n"
        << "  added for frame-accurate time. Without -o the tool only reads\n"
        << "  the file and reports what it found.\n"
        << "  Location is withheld by default so the output is safe to share;\n"
        << "  --with-location prints latitude/longitude from the flight.\n"
        << "  --klv maps each frame to ST 0601 and prints the conversion\n"
        << "  coverage report — what was carried, omitted, and dropped.\n"
        << "  --with-identity prints the airframe serial and geotag from the\n"
        << "  session metadata, which are withheld by default.\n"
        << "  -h/--help prints this; --version prints the version.\n\n"
        << "Attitude handling (Items 6/7):\n"
        << "  --clamp-attitude clamps pitch/roll to spec ranges (±20°, ±50°)\n"
        << "  instead of emitting 0x8000 when out of range. Use for consumers\n"
        << "  that can't handle missing values gracefully. Items 90/91 always\n"
        << "  carry the true full-range attitude either way.\n\n"
        << "Image Source Sensor (Item 11):\n"
        << "  --infer-image-source names the sensor EO or IR for a recording\n"
        << "  whose format states no spectrum (every v3 recording), inferring\n"
        << "  it from whether the frame carried thermal metadata. This is a\n"
        << "  guess from a proxy, not a converted field: a thermal recording\n"
        << "  that wrote no thermal metadata would be named EO. Off by default,\n"
        << "  and the coverage report marks the item (inferred).\n\n"
        << "Output streams: the video is announced first, so 0:0 is the video\n"
        << "  and 0:1 the KLV (ffmpeg -map 0:0 selects the video).\n";
}

/// Ctrl-C (or a `kill`) mid-conversion must not leave a truncated output that
/// looks like a finished one. Default disposition terminates the process
/// immediately — no stack unwinding, so the sink's RAII cleanup never runs.
/// This handler only sets a flag; `mux_to_ts()` checks it cooperatively
/// between samples and returns normally, which is what lets that cleanup fire.
extern "C" void handle_interrupt_signal(int /*signal_number*/) {
    request_mux_interrupt();
}

}  // namespace

int main(int argc, char* argv[]) {
    std::signal(SIGINT, handle_interrupt_signal);
    std::signal(SIGTERM, handle_interrupt_signal);

    std::string input;
    std::string output;
    std::string live_rtsp;
    std::string live_udp;
    bool with_location = false;
    bool show_klv = false;
    bool with_identity = false;
    TransformOptions options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--with-location") {
            with_location = true;
        } else if (arg == "--klv") {
            show_klv = true;
        } else if (arg == "--with-identity") {
            with_identity = true;
        } else if (arg == "--clamp-attitude") {
            options.clamp_attitude = true;
        } else if (arg == "--infer-image-source") {
            options.infer_image_source = true;
        } else if (arg == "--force-emit-attitude") {
            // Removed (ADR 0008). Rejected rather than ignored: an unknown
            // argument here is silently dropped, and this one used to change
            // output, so a script still passing it deserves to be told rather
            // than to get the default and assume otherwise.
            std::cerr << "Error: --force-emit-attitude has been removed. It never emitted\n"
                         "out-of-spec values — ST 0601 has no encoding for an angle past\n"
                         "Items 6/7's range, so it produced exactly what --clamp-attitude\n"
                         "produces. Use --clamp-attitude, or read Items 90/91, which always\n"
                         "carry the true full-range attitude.\n";
            return 1;
        } else if (arg == "-h" || arg == "--help") {
            print_usage(std::cout, argv[0]);
            return 0;
        } else if (arg == "--version") {
            std::cout << "parrot-to-klv " << kVersion << "\n";
            return 0;
        } else if (arg == "-o" || arg == "--output") {
            // Checked separately from the value so a trailing "-o" is an error
            // rather than falling through and being taken as the input path.
            if (i + 1 >= argc) {
                std::cerr << "Error: " << arg << " needs a path.\n";
                return 1;
            }
            output = argv[++i];
            // An empty path is rejected outright rather than left to
            // `!output.empty()` below, which reads a blank "-o" as no output
            // requested at all and silently falls back to inspection mode --
            // a successful-looking no-op for a script that passed one by
            // mistake.
            if (output.empty()) {
                std::cerr << "Error: " << arg << " needs a non-empty path.\n";
                return 1;
            }
        } else if (arg == "--live-rtsp") {
            // pipeline: is test-only hermetic escape hatch; CLI gates it via PARROT_TEST_ALLOW_PIPELINE
            if (i + 1 >= argc) {
                std::cerr << "Error: --live-rtsp needs a URI.\n";
                return 1;
            }
            live_rtsp = argv[++i];
            if (live_rtsp.empty()) {
                std::cerr << "Error: --live-rtsp needs a non-empty URI.\n";
                return 1;
            }
            if (live_rtsp.rfind("pipeline:", 0) == 0) {
                if (!std::getenv("PARROT_TEST_ALLOW_PIPELINE")) {
                    std::fprintf(stderr,
                                 "error: pipeline: video_source is test-only; set "
                                 "PARROT_TEST_ALLOW_PIPELINE=1 to allow\n");
                    return 1;
                }
            }
        } else if (arg == "--live-udp") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --live-udp needs a URI.\n";
                return 1;
            }
            live_udp = argv[++i];
            if (live_udp.empty()) {
                std::cerr << "Error: --live-udp needs a non-empty URI.\n";
                return 1;
            }
        } else if (!arg.empty() && arg[0] == '-') {
            // Unrecognized *flags* are refused, never ignored. A silently
            // dropped "--klvv" runs to completion, exits 0, and produces output
            // missing what the caller asked for — the same reasoning that made
            // the removed --force-emit-attitude an error rather than a no-op.
            std::cerr << "Error: unknown option '" << arg << "'.\n"
                      << "Run " << argv[0] << " --help for the options.\n";
            return 1;
        } else if (input.empty()) {
            input = arg;
        } else {
            std::cerr << "Error: more than one input file given ('" << input
                      << "' and '" << arg << "').\n";
            return 1;
        }
    }

    // Live ingest stub (ADR 0013) — RTSP URI + UDP sink.
    if (!live_rtsp.empty() || !live_udp.empty()) {
        if (live_rtsp.empty() || live_udp.empty()) {
            std::cerr << "Error: --live-rtsp and --live-udp must be used together.\n";
            return 1;
        }
        LiveIngestConfig cfg;
        cfg.rtsp_uri = live_rtsp;
        cfg.udp_sink = live_udp;
        // TODO: surface Result when live ingest is un-stubbed (deferred per PR 44 review #4) — main currently discards Result and always exits 0 for live path
        (void)live_ingest(cfg);
        std::cout << "live ingest not yet implemented — stub\n";
        return 0;
    }

    if (input.empty()) {
        print_usage(std::cerr, argv[0]);
        return 1;
    }

    // Converting is the tool's job; the summary below is the inspection mode.
    if (!output.empty()) {
        const auto report = mux_to_ts(input, output, options);
        if (!report.error.empty()) {
            std::cerr << "Conversion failed: " << report.error << "\n";
            return 1;
        }
        std::cout << "Wrote " << output << ": " << report.frames_emitted
                  << " ST 0601 packets from " << report.samples_seen
                  << " metadata samples";
        if (report.frames_skipped > 0) {
            std::cout << " (" << report.frames_skipped << " skipped)";
        }
        // Say which of the two video paths ran: a consumer reading time from the
        // elementary stream needs to know, and finding out by looking for SEI
        // that is not there is a bad way to learn it (ADR 0007).
        if (report.st0604_generated) {
            std::cout << ", video never re-encoded (ST 0604 SEI added)\n";
        } else if (report.video_sample_entry != 0) {
            std::cout << ", video carried unchanged — no ST 0604: generation needs "
                         "H.264, source is "
                      << fourcc_to_string(report.video_sample_entry) << "\n";
        } else {
            std::cout << ", no video track in the source\n";
        }
        if (show_klv) {
            std::cout << "\n" << report.coverage.to_text();
        }
        return 0;
    }

    auto extractor = Mp4MetadataExtractor::open(input);
    if (!extractor) {
        std::cerr << "Failed to open MP4 or find a Parrot metadata track\n";
        return 1;
    }

    // Session metadata is per-file: the device that recorded it and when.
    SessionMetadata session;
    if (FILE* handle = std::fopen(input.c_str(), "rb")) {
        session = parse_session_metadata(handle);
        std::fclose(handle);
    }
    const Disclosure disclosure{with_location, with_identity};
    if (!session.empty()) {
        std::cout << "\nSession metadata:\n";
        if (session.model) std::cout << "  model: " << *session.model << "\n";
        if (session.software_version) std::cout << "  software: " << *session.software_version << "\n";
        if (session.media_date) std::cout << "  media date: " << *session.media_date << "\n";
        // Serial and geotag identify the airframe and the place; withheld like
        // every other sensitive value unless asked for.
        if (session.serial_number) {
            std::cout << "  serial: "
                      << (with_identity ? *session.serial_number : "withheld") << "\n";
        }
        if (session.geotag) {
            std::cout << "  geotag: " << (with_identity ? *session.geotag : "withheld") << "\n";
        }
    }

    const auto& info = extractor->track_info();
    if (info.format == ParrotFormat::V3) {
        std::cout << "\n--- Decoding v3 samples ---\n";
        return decode_all(
            *extractor, session, disclosure, show_klv, options,
            [](std::span<const uint8_t> data, std::string& error) -> std::optional<ParrotV3Frame> {
                ParrotV3Error err{};
                auto frame = parse_v3(data, &err);
                if (!frame) error = to_string(err);
                return frame;
            },
            [](const ParrotV3Frame& f) { return from_v3(f); });
    }
    if (info.format == ParrotFormat::Proto) {
        std::cout << "\n--- Decoding proto samples ---\n";
        return decode_all(
            *extractor, session, disclosure, show_klv, options,
            [](std::span<const uint8_t> data,
               std::string& error) -> std::optional<ParrotProtoFrame> {
                ParrotProtoError err{};
                auto frame = parse_proto(data, &err);
                if (!frame) error = to_string(err);
                return frame;
            },
            [](const ParrotProtoFrame& f) { return from_proto(f); });
    }

    std::cerr << "\nOnly the v3 and proto formats are decoded so far; this file is "
              << to_string(info.format) << ".\n";
    return 1;
}
