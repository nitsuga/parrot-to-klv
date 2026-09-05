// Copyright 2026 Eric Schneider
// Licensed under the Apache License, Version 2.0

#include "live/file_replay.hpp"
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
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <iostream>
#include <optional>
#include <span>
#include <stdexcept>
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
    // extraction, parse, or timestamp presence fails -- anchor_to_session
    // treats an unknown origin as "do not anchor" rather than assuming a zero
    // clock base.
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
    out << "Usage: " << program << " [-i <input>] [-o <output>] [options]\n"
        << "   or: " << program << " <input.mp4> [-o <output>] [options]\n\n"
        << "  -i/--input <arg>  input: filesystem path (MP4), rtsp:// or\n"
        << "                    rtsps:// URI, or pipeline:… (test-only, gated\n"
        << "                    by PARROT_TEST_ALLOW_PIPELINE). A bare\n"
        << "                    positional argument is also accepted as input\n"
        << "                    for backward compatibility. Type is inferred\n"
        << "                    from the value.\n"
        << "  -o/--output <arg> output: filesystem path (MPEG-TS), udp:// or\n"
        << "                    udp: URI, or srt: URI (passed through to the\n"
        << "                    streaming backend). Type is inferred from the\n"
        << "                    value. Without -o the tool only reads a file\n"
        << "                    input and reports what it found.\n"
        << "  --iface <iface>   multicast interface for udp: output (only\n"
        << "                    with udp:).\n"
        << "  --ttl <n>         multicast TTL 0..255 for udp: output (only\n"
        << "                    with udp:).\n"
        << "  --loop            enable multicast loopback for udp: output\n"
        << "                    (default).\n"
        << "  --no-loop         disable multicast loopback for udp: output.\n"
        << "  --idle-timeout <s> end a live run when no video has been delivered\n"
        << "                    for <s> seconds (default 10; 0 disables). Only\n"
        << "                    with a live input.\n"
        << "  --reconnect       after a live source stops, keep polling for it\n"
        << "                    and start a new capture when it returns; runs\n"
        << "                    until SIGINT/SIGTERM. A file output numbers\n"
        << "                    every session (out_0000.ts, out_0001.ts, ...);\n"
        << "                    a udp:/srt: output reuses its address. Only\n"
        << "                    with a live input.\n"
        << "  --reconnect-interval <s>  how often to poll for the source in\n"
        << "                    --reconnect (default 3).\n"
        << "  --max-empty-sessions <n>  in --reconnect, stop after <n>\n"
        << "                    consecutive sessions that connect but deliver\n"
        << "                    neither video nor telemetry (default 5; 0 never\n"
        << "                    stops). A source that is simply absent never\n"
        << "                    connects and is not counted.\n"
        << "  --repeat          loop file replay to a live sink until SIGINT\n"
        << "                    (only with -i file.mp4 -o udp:/srt:).\n"
        << "  --repeat-pts <m>  how PTS behaves when looping: reset (each\n"
        << "                    repetition restarts at 0; default) or\n"
        << "                    continue (PTS keeps increasing across loops).\n"
        << "                    Only with --repeat.\n\n"
        << "  -o/--output converts: writes an MPEG-TS carrying the source's\n"
        << "  video plus an ST 0601 KLV stream. The video is never decoded or\n"
        << "  re-encoded. For H.264 its timing NALs are rewritten and ST 0604\n"
        << "  SEI is added for frame-accurate time; other codecs are carried\n"
        << "  through unstamped. Without -o the tool only reads\n"
        << "  the file and reports what it found.\n"
        << "  Location is withheld by default so the output is safe to share;\n"
        << "  --with-location prints latitude/longitude from the flight.\n"
        << "  --klv maps each successfully decoded sample to ST 0601 and prints\n"
        << "  the conversion coverage report — what was carried, omitted, and\n"
        << "  dropped.\n"
        << "  --with-identity prints the airframe serial and geotag from the\n"
        << "  session metadata, which are withheld by default.\n"
        << "  -h/--help prints this; --version prints the version.\n\n"
        << "  Examples:\n"
        << "    " << program << " flight.mp4 -o flight.ts\n"
        << "    " << program << " -i rtsp://192.168.0.1/live -o flight.ts\n"
        << "    " << program << " -i rtsp://192.168.0.1/live -o udp://239.255.0.1:5000\n"
        << "    " << program << " -i flight.mp4 -o udp://239.0.0.1:5000 --iface eth1\n"
        << "    " << program << " -i flight.mp4 -o srt://127.0.0.1:5000 --repeat\n\n"
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

namespace cli_detail {

std::string normalize_udp_sink(const std::string& s) {
    if (s.starts_with("udp://")) {
        return "udp:" + s.substr(6);
    }
    return s;
}

// Early-validate that a udp: output has a host:port remainder after the
// scheme (IPv4/hostname or bracketed IPv6), rather than deferring a bare
// "udp:" to a sink-open failure. Accepts udp:host:port and udp://host:port.
bool udp_sink_host_port_valid(const std::string& s) {
    size_t pos = s.find("udp:") + 4;  // immediately past the "udp:" scheme
    if (pos + 1 < s.size() && s[pos] == '/' && s[pos + 1] == '/') {
        pos += 2;
    }
    const std::string rem = s.substr(pos);
    return !rem.empty() && rem.find(':') != std::string::npos;
}

bool is_live_input_value(const std::string& s) {
    return s.starts_with("rtsp://") || s.starts_with("rtsps://") ||
           s.starts_with("pipeline:");
}

bool is_udp_sink_value(const std::string& s) { return s.starts_with("udp:"); }

bool is_srt_sink_value(const std::string& s) { return s.starts_with("srt:"); }

}  // namespace cli_detail

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
    bool with_location = false;
    bool show_klv = false;
    bool with_identity = false;
    TransformOptions options;

    // UDP sink options — only valid when resolved output is udp:.
    std::string udp_iface;
    bool has_iface = false;
    int udp_ttl = 1;
    bool has_ttl = false;
    bool has_loop = false;
    bool udp_loop = true;
    // Live-input only: end the run after this long with no video delivered (ADR 0018).
    std::chrono::seconds idle_timeout{10};
    bool has_idle_timeout = false;
    bool reconnect = false;
    std::chrono::seconds reconnect_interval{3};
    bool has_reconnect_interval = false;
    unsigned max_empty_sessions = 5;
    bool has_max_empty_sessions = false;
    bool repeat = false;
    bool has_repeat_pts = false;
    bool repeat_pts_continue = false;  // reset is the default — matches per-loop video restart; continue would desync KLV from the per-loop video (see PR #58 review)

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
        } else if (arg == "-i" || arg == "--input") {
            if (i + 1 >= argc) {
                std::cerr << "Error: " << arg << " needs a path.\n";
                return 1;
            }
            std::string val = argv[++i];
            if (val.empty()) {
                std::cerr << "Error: " << arg << " needs a non-empty path.\n";
                return 1;
            }
            if (!input.empty()) {
                std::cerr << "Error: more than one input file given ('" << input
                          << "' and '" << val << "').\n";
                return 1;
            }
            input = std::move(val);
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
        } else if (arg == "--iface") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --iface needs a value.\n";
                return 1;
            }
            std::string val = argv[++i];
            if (val.empty()) {
                std::cerr << "Error: --iface needs a non-empty value.\n";
                return 1;
            }
            udp_iface = std::move(val);
            has_iface = true;
        } else if (arg == "--ttl") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --ttl needs a value.\n";
                return 1;
            }
            std::string val = argv[++i];
            try {
                size_t pos = 0;
                long parsed = std::stol(val, &pos, 10);
                if (pos != val.size()) throw std::invalid_argument("trailing");
                if (parsed < 0 || parsed > 255) {
                    std::cerr << "Error: --ttl must be 0..255 (got " << val << ").\n";
                    return 1;
                }
                udp_ttl = static_cast<int>(parsed);
                has_ttl = true;
            } catch (const std::exception&) {
                std::cerr << "Error: --ttl must be 0..255 (got " << val << ").\n";
                return 1;
            }
        } else if (arg == "--idle-timeout") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --idle-timeout needs a value.\n";
                return 1;
            }
            std::string val = argv[++i];
            try {
                size_t pos = 0;
                long parsed = std::stol(val, &pos, 10);
                if (pos != val.size()) throw std::invalid_argument("trailing");
                if (parsed < 0) {
                    std::cerr << "Error: --idle-timeout must be >= 0 (got " << val << ").\n";
                    return 1;
                }
                idle_timeout = std::chrono::seconds(parsed);
                has_idle_timeout = true;
            } catch (const std::exception&) {
                std::cerr << "Error: --idle-timeout must be a whole number of seconds (got "
                          << val << ").\n";
                return 1;
            }
        } else if (arg == "--reconnect") {
            reconnect = true;
        } else if (arg == "--reconnect-interval") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --reconnect-interval needs a value.\n";
                return 1;
            }
            std::string val = argv[++i];
            try {
                size_t pos = 0;
                long parsed = std::stol(val, &pos, 10);
                if (pos != val.size()) throw std::invalid_argument("trailing");
                if (parsed < 1) {
                    std::cerr << "Error: --reconnect-interval must be >= 1 (got " << val << ").\n";
                    return 1;
                }
                reconnect_interval = std::chrono::seconds(parsed);
                has_reconnect_interval = true;
            } catch (const std::exception&) {
                std::cerr << "Error: --reconnect-interval must be a whole number of seconds (got "
                          << val << ").\n";
                return 1;
            }
        } else if (arg == "--max-empty-sessions") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --max-empty-sessions needs a value.\n";
                return 1;
            }
            std::string val = argv[++i];
            try {
                size_t pos = 0;
                long parsed = std::stol(val, &pos, 10);
                if (pos != val.size()) throw std::invalid_argument("trailing");
                if (parsed < 0 ||
                    static_cast<unsigned long long>(parsed) >
                        std::numeric_limits<unsigned>::max()) {
                    // Casting a wider value would wrap, and wrapping to 0
                    // silently disables the safeguard this flag configures.
                    std::cerr << "Error: --max-empty-sessions must be 0.."
                              << std::numeric_limits<unsigned>::max() << " (got " << val << ").\n";
                    return 1;
                }
                max_empty_sessions = static_cast<unsigned>(parsed);
                has_max_empty_sessions = true;
            } catch (const std::exception&) {
                std::cerr << "Error: --max-empty-sessions must be a whole number (got " << val
                          << ").\n";
                return 1;
            }
        } else if (arg == "--loop") {
            udp_loop = true;
            has_loop = true;
        } else if (arg == "--no-loop") {
            udp_loop = false;
            has_loop = true;
        } else if (arg == "--repeat") {
            repeat = true;
        } else if (arg == "--repeat-pts") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --repeat-pts needs a value (reset or continue).\n";
                return 1;
            }
            std::string val = argv[++i];
            if (val != "reset" && val != "continue") {
                std::cerr << "Error: --repeat-pts must be reset or continue (got '" << val << "').\n";
                return 1;
            }
            repeat_pts_continue = (val == "continue");
            has_repeat_pts = true;
        } else if (arg == "--live-rtsp" || arg == "--live-udp") {
            std::cerr
                << "Error: --live-rtsp/--live-udp have been removed. Pass the input and "
                   "output instead: -i <rtsp://…> -o <path|udp://…>.\n";
            return 1;
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

    if (input.empty()) {
        print_usage(std::cerr, argv[0]);
        return 1;
    }

    const bool is_live_input = cli_detail::is_live_input_value(input);
    const bool is_udp_output = !output.empty() && cli_detail::is_udp_sink_value(output);
    const bool is_srt_output = !output.empty() && cli_detail::is_srt_sink_value(output);

    // A udp: output must name a host:port, not a bare "udp:"/"udp://" scheme.
    if (is_udp_output && !cli_detail::udp_sink_host_port_valid(output)) {
        std::cerr << "Error: udp output needs host:port (got '" << output << "').\n";
        return 1;
    }

    // pipeline: is test-only hermetic escape hatch; CLI gates it via PARROT_TEST_ALLOW_PIPELINE.
    if (is_live_input && input.starts_with("pipeline:")) {
        if (!std::getenv("PARROT_TEST_ALLOW_PIPELINE")) {
            std::fprintf(stderr,
                         "error: pipeline: video_source is test-only; set "
                         "PARROT_TEST_ALLOW_PIPELINE=1 to allow\n");
            return 1;
        }
    }

    // The idle timeout ends a live source that has stopped delivering; a file
    // input has no such source, so accepting it silently would be a lie.
    if (has_idle_timeout && !is_live_input) {
        std::cerr << "Error: --idle-timeout is only valid with a live input.\n";
        return 1;
    }
    if (reconnect && !is_live_input) {
        std::cerr << "Error: --reconnect is only valid with a live input.\n";
        return 1;
    }
    if (has_reconnect_interval && !reconnect) {
        std::cerr << "Error: --reconnect-interval is only valid with --reconnect.\n";
        return 1;
    }
    if (has_max_empty_sessions && !reconnect) {
        std::cerr << "Error: --max-empty-sessions is only valid with --reconnect.\n";
        return 1;
    }

    // UDP sink options are only valid with a udp: output.
    if ((has_iface || has_ttl || has_loop) && !is_udp_output) {
        if (has_iface) {
            std::cerr << "Error: --iface is only valid with a udp: output.\n";
            return 1;
        }
        if (has_ttl) {
            std::cerr << "Error: --ttl is only valid with a udp: output.\n";
            return 1;
        }
        std::cerr << "Error: --loop/--no-loop is only valid with a udp: output.\n";
        return 1;
    }

    // --repeat is only for file → live.
    if (repeat && (is_live_input || !(is_udp_output || is_srt_output))) {
        std::cerr << "Error: --repeat is only valid with -i file.mp4 -o udp:/srt:.\n";
        return 1;
    }
    if (has_repeat_pts && !repeat) {
        std::cerr << "Error: --repeat-pts is only valid with --repeat.\n";
        return 1;
    }

    // Routing by inferred input×output types.
    if (is_live_input && output.empty()) {
        std::cerr << "Error: live input needs an output (-o <path|udp://…|srt:…>).\n";
        return 1;
    }
    if (!is_live_input && (is_udp_output || is_srt_output)) {
        std::string sink;
        if (is_udp_output) sink = cli_detail::normalize_udp_sink(output);
        else sink = output;
        // Session metadata for file replay (same as offline path).
        SessionMetadata session;
        if (FILE* h = std::fopen(input.c_str(), "rb")) {
            session = parse_session_metadata(h);
            std::fclose(h);
        }
        FileReplayConfig cfg;
        cfg.input_path = input;
        cfg.output_sink = std::move(sink);
        cfg.transform_options = options;
        cfg.session = session.empty() ? std::nullopt : std::optional<SessionMetadata>{session};
        cfg.udp_ttl = udp_ttl;
        cfg.udp_iface = udp_iface;
        cfg.udp_loop = udp_loop;
        cfg.repeat = repeat;
        cfg.repeat_pts_continue = repeat_pts_continue;
        auto res = replay_file_to_live(cfg);
        if (!res) {
            std::cerr << "file replay failed: " << static_cast<int>(res.error()) << "\n";
            return 1;
        }
        return 0;
    }
    if (is_live_input && !output.empty()) {
        std::string sink;
        if (is_udp_output) {
            sink = cli_detail::normalize_udp_sink(output);
        } else if (is_srt_output) {
            sink = output;
        } else {
            sink = "file:" + output;
        }
        LiveIngestConfig cfg;
        cfg.rtsp_uri = input;
        cfg.udp_sink = std::move(sink);
        cfg.run_duration = std::nullopt;  // indefinite until SIGINT
        cfg.transform_options = options;
        if (has_iface) cfg.udp_mcast_iface = udp_iface;
        if (has_ttl) cfg.udp_ttl = udp_ttl;
        if (has_loop) cfg.udp_loop = udp_loop;
        if (has_idle_timeout) cfg.idle_timeout = idle_timeout;
        cfg.reconnect = reconnect;
        if (has_reconnect_interval) cfg.reconnect_interval = reconnect_interval;
        if (has_max_empty_sessions) cfg.max_empty_sessions = max_empty_sessions;
        auto res = live_ingest(cfg);
        if (!res) {
            std::cerr << "live ingest failed: " << static_cast<int>(res.error()) << "\n";
            return 1;
        }
        return 0;
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
