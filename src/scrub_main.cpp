// Copyright 2026 Eric Schneider
// Licensed under the Apache License, Version 2.0

#include "parrot_scrub.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

using namespace parrot_to_klv;

namespace {

void usage(const char* argv0) {
    std::cerr
        << "Usage: " << argv0 << " <input.mp4> <output.mp4> [options]\n\n"
        << "Replaces the location-bearing fields of a Parrot recording with synthetic\n"
        << "values, so a real flight can serve as a committable test fixture.\n\n"
        << "Options:\n"
        << "  --lat <deg>     synthetic latitude (default 0)\n"
        << "  --lon <deg>     synthetic longitude (default 0)\n"
        << "  --alt <m>       synthetic altitude AMSL (default 0)\n"
        << "  --zero-media    also blank the video and audio payloads\n\n"
        << "Scrubs: the v3 telemetry track (latitude, longitude, altitude, and the\n"
        << "automation extension's target coordinates) and any ISO 6709 geotag in\n"
        << "moov/udta.\n\n"
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
        const auto next = [&](double& dst) {
            if (i + 1 < argc) dst = std::atof(argv[++i]);
        };
        if (arg == "--lat") next(options.values.latitude_deg);
        else if (arg == "--lon") next(options.values.longitude_deg);
        else if (arg == "--alt") next(options.values.altitude_amsl_m);
        else if (arg == "--zero-media") options.zero_media = true;
        else if (arg == "-h" || arg == "--help") { usage(argv[0]); return 0; }
        else if (input.empty()) input = arg;
        else if (output.empty()) output = arg;
    }

    if (input.empty() || output.empty()) {
        usage(argv[0]);
        return 2;
    }
    if (input == output) {
        std::cerr << "Refusing to scrub in place — give a distinct output path.\n";
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
