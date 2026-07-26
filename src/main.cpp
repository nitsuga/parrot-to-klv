// Copyright 2026 Eric Schneider
// Licensed under the Apache License, Version 2.0

#include "mp4_metadata_extractor.hpp"
#include "parrot_proto.hpp"
#include "parrot_v3.hpp"
#include "session_metadata.hpp"
#include "telemetry.hpp"
#include "transform.hpp"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <cstdio>
#include <optional>
#include <span>
#include <string>

using namespace parrot_to_klv;

namespace {

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

/// Decode every sample of one format, printing the first few.
///
/// Templated over the frame type so v3 and proto share the loop: the formats
/// differ in what a frame *is*, not in how a track is walked.
template <typename Parse, typename Normalize>
int decode_all(Mp4MetadataExtractor& extractor, const SessionMetadata& session,
               Disclosure disclosure, bool show_klv, Parse parse, Normalize normalize) {
    uint32_t decoded = 0;
    uint32_t failed = 0;
    const uint32_t to_show = std::min(3u, extractor.sample_count());

    // The first sample's clock is the origin the session media date anchors to.
    uint64_t first_monotonic_us = 0;
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
        if (i < to_show) {
            std::cout << "  Sample " << i << " (" << sample->data.size() << " bytes):\n";
            print_frame(*frame, disclosure.location);
            if (show_klv) {
                auto snapshot = normalize(*frame);
                // v3 frames carry only a monotonic clock; the session's media
                // date is what turns it into wall time.
                anchor_to_session(snapshot, session, first_monotonic_us);
                const auto klv = to_0601(snapshot, &session);
                std::cout << "    -> ST 0601 packet: " << klv.packet.size() << " bytes\n"
                          << klv.coverage.to_text(disclosure);
            }
        }
    }

    std::cout << "\nDecoded " << decoded << " of " << extractor.sample_count() << " samples";
    if (failed > 0) std::cout << " (" << failed << " failed)";
    std::cout << "\n";
    return failed > 0 ? 1 : 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string input;
    bool with_location = false;
    bool show_klv = false;
    bool with_identity = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--with-location") {
            with_location = true;
        } else if (arg == "--klv") {
            show_klv = true;
        } else if (arg == "--with-identity") {
            with_identity = true;
        } else if (input.empty()) {
            input = arg;
        }
    }

    if (input.empty()) {
        std::cerr << "Usage: " << argv[0] << " <input.mp4> [--with-location]\n\n"
                  << "  Location is withheld by default so the output is safe to share;\n"
                  << "  --with-location prints latitude/longitude from the flight.\n"
                  << "  --klv maps each frame to ST 0601 and prints the conversion\n"
                  << "  coverage report — what was carried, omitted, and dropped.\n"
                  << "  --with-identity prints the airframe serial and geotag from the\n"
                  << "  session metadata, which are withheld by default.\n";
        return 1;
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
            *extractor, session, disclosure, show_klv,
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
            *extractor, session, disclosure, show_klv,
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
