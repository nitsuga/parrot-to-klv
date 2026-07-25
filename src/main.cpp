// Copyright 2026 Eric Schneider
// Licensed under the Apache License, Version 2.0

#include "mp4_metadata_extractor.hpp"
#include "parrot_v3.hpp"

#include <iomanip>
#include <iostream>
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

}  // namespace

int main(int argc, char* argv[]) {
    std::string input;
    bool with_location = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--with-location") {
            with_location = true;
        } else if (input.empty()) {
            input = arg;
        }
    }

    if (input.empty()) {
        std::cerr << "Usage: " << argv[0] << " <input.mp4> [--with-location]\n\n"
                  << "  Location is withheld by default so the output is safe to share;\n"
                  << "  --with-location prints latitude/longitude from the flight.\n";
        return 1;
    }

    auto extractor = Mp4MetadataExtractor::open(input);
    if (!extractor) {
        std::cerr << "Failed to open MP4 or find a Parrot metadata track\n";
        return 1;
    }

    const auto& info = extractor->track_info();
    if (info.format != ParrotFormat::V3) {
        std::cerr << "\nOnly the v3 format is decoded so far; this file is "
                  << to_string(info.format) << ".\n";
        return 1;
    }

    std::cout << "\n--- Decoding v3 samples ---\n";

    uint32_t decoded = 0;
    uint32_t failed = 0;
    const uint32_t to_show = std::min(3u, extractor->sample_count());

    for (uint32_t i = 0; i < extractor->sample_count(); ++i) {
        auto sample = extractor->extract_sample(i);
        if (!sample) {
            ++failed;
            continue;
        }
        ParrotV3Error err{};
        auto frame = parse_v3(sample->data, &err);
        if (!frame) {
            if (failed == 0) {
                std::cerr << "Sample " << i << ": " << to_string(err) << "\n";
            }
            ++failed;
            continue;
        }
        ++decoded;
        if (i < to_show) {
            std::cout << "  Sample " << i << " (" << sample->data.size() << " bytes):\n";
            print_frame(*frame, with_location);
        }
    }

    std::cout << "\nDecoded " << decoded << " of " << extractor->sample_count() << " samples";
    if (failed > 0) {
        std::cout << " (" << failed << " failed)";
    }
    std::cout << "\n";
    return failed > 0 ? 1 : 0;
}
