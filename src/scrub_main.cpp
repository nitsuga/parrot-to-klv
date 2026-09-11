// Copyright 2026 Eric Schneider
// Licensed under the Apache License, Version 2.0

#include "parrot_scrub.hpp"

#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>

using namespace parrot_to_klv;

namespace {

/// Strict decimal parse: the whole argument must be consumed, and the result
/// must be finite. `std::atof` silently returns 0 on garbage ("12abc" parses
/// as 12, "abc" as 0) and happily returns NaN/inf, which
/// `to_q10_22()`/`std::lround()` then turn into a domain error whose result
/// is undefined behavior once cast to `int32_t` — for a privacy tool, a
/// mistyped coordinate must be refused, not silently written into every
/// sample as garbage bits.
std::optional<double> strict_parse_double(const std::string& text) {
    if (text.empty()) return std::nullopt;
    errno = 0;
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (end != text.c_str() + text.size()) return std::nullopt;  // trailing junk
    if (errno == ERANGE) return std::nullopt;
    if (!std::isfinite(value)) return std::nullopt;
    return value;
}

void usage(const char* argv0) {
    std::cerr
        << "Usage: " << argv0 << " <input.mp4> <output.mp4> [options]\n\n"
        << "Replaces the location-bearing fields of a Parrot recording with synthetic\n"
        << "values, so a real flight can serve as a committable test fixture.\n\n"
        << "Options:\n"
        << "  --lat <deg>     synthetic latitude, [-90, 90] (default 0)\n"
        << "  --lon <deg>     synthetic longitude, [-180, 180] (default 0)\n"
        << "  --alt <m>       synthetic altitude AMSL, [-32768, 32767.99609375] (default 0)\n"
        << "  --zero-media    also blank the video and audio payloads\n"
        << "  --keep-unknown-extensions\n"
        << "                  leave undecoded extension payloads intact. Their contents\n"
        << "                  are unknown and may include position; the output is then\n"
        << "                  NOT scrubbed. For studying an extension, not for sharing.\n\n"
        << "Scrubs: the v3 telemetry track (latitude, longitude, altitude, and the\n"
        << "automation extension's target coordinates); any ISO 6709 geotag in\n"
        << "moov/udta; and moov/udta/meta/ilst's media date, serial number,\n"
        << "friendly name, title and copyright.\n\n"
        << "Does NOT scrub the imagery. A video of your street is still a video of\n"
        << "your street after its coordinates are zeroed — pass --zero-media when the\n"
        << "output is going into a repository.\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string input;
    std::string output;
    ScrubOptions options;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        // A coordinate flag with no value, an unparseable one, or one outside
        // a representable coordinate's range must stop the program rather
        // than run with a silently-wrong (or zero, or garbage-bit) coordinate
        // baked into every sample.
        const auto next_numeric = [&](double& dst, std::optional<double> lo,
                                      std::optional<double> hi) -> bool {
            if (i + 1 >= argc) {
                std::cerr << "Error: " << arg << " needs a value.\n";
                return false;
            }
            const std::string text = argv[++i];
            const auto value = strict_parse_double(text);
            if (!value) {
                std::cerr << "Error: " << arg << " '" << text << "' is not a valid number.\n";
                return false;
            }
            if ((lo && *value < *lo) || (hi && *value > *hi)) {
                std::cerr << "Error: " << arg << " " << *value << " is outside [" << *lo << ", "
                          << *hi << "].\n";
                return false;
            }
            dst = *value;
            return true;
        };
        if (arg == "--lat") {
            if (!next_numeric(options.values.latitude_deg, kMinLatitudeDeg, kMaxLatitudeDeg)) return 1;
        } else if (arg == "--lon") {
            if (!next_numeric(options.values.longitude_deg, kMinLongitudeDeg, kMaxLongitudeDeg)) return 1;
        } else if (arg == "--alt") {
            if (!next_numeric(options.values.altitude_amsl_m, kMinAltitudeAmslM, kMaxAltitudeAmslM)) {
                return 1;
            }
        } else if (arg == "--zero-media") {
            options.zero_media = true;
        } else if (arg == "--keep-unknown-extensions") {
            options.zero_unknown_extensions = false;
        } else if (arg == "-h" || arg == "--help") {
            usage(argv[0]);
            return 0;
        } else if (!arg.empty() && arg[0] == '-') {
            // Unrecognized flags are refused, never ignored or (worse) taken
            // as a positional: a misspelled "--zero_media" (underscore) used
            // to run to completion and exit 0, leaving the imagery in place
            // in a tool whose whole point is scrubbing it.
            std::cerr << "Error: unknown option '" << arg << "'.\n"
                      << "Run " << argv[0] << " --help for the options.\n";
            return 1;
        } else if (input.empty()) {
            input = arg;
        } else if (output.empty()) {
            output = arg;
        } else {
            std::cerr << "Error: unexpected extra argument '" << arg << "'.\n";
            return 1;
        }
    }

    if (input.empty() || output.empty()) {
        usage(argv[0]);
        return 2;
    }
    const auto report = scrub_file(input, output, options);

    if (!report.ok) {
        std::cerr << "Scrub failed: " << report.error << "\n";
        return 1;
    }

    std::cout << "Scrubbed " << report.samples_rewritten << " telemetry sample(s)";
    if (report.automation_extensions_rewritten > 0) {
        std::cout << ", " << report.automation_extensions_rewritten << " automation extension(s)";
    }
    std::cout << "\n";
    std::cout << "Geotag boxes rewritten: " << report.geotag_boxes_rewritten << "\n";
    std::cout << "Identity boxes rewritten: " << report.identity_boxes_rewritten << "\n";
    if (!report.unknown_extension_ids.empty()) {
        std::cout << "Undecoded extension(s) found:";
        for (uint16_t id : report.unknown_extension_ids) {
            std::printf(" 0x%04X", id);
        }
        std::cout << (report.unknown_extension_payloads_zeroed > 0
                          ? " — payloads blanked in " : " — LEFT INTACT in ")
                  << (report.unknown_extension_payloads_zeroed > 0
                          ? report.unknown_extension_payloads_zeroed
                          : report.samples_rewritten)
                  << " sample(s)\n";
    }
    if (report.media_bytes_zeroed > 0) {
        std::cout << "Media payload zeroed: " << report.media_bytes_zeroed << " bytes\n";
    }
    std::cout << "Wrote " << output << "\n";

    if (!report.warnings.empty()) {
        std::cout << "\nStill your call before sharing this file:\n";
        for (const auto& w : report.warnings) {
            std::cout << "  - " << w << "\n";
        }
    }
    return 0;
}
