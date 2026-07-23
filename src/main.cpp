// Copyright 2026 Eric Schneider
// Licensed under the Apache License, Version 2.0

#include "mp4_metadata_extractor.hpp"

#include <iostream>
#include <iomanip>

using namespace parrot_to_klv;

int main(int argc, char* argv[]) {
    std::cout << "Parrot to KLV converter\n\n";

    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <input.mp4>\n";
        return 1;
    }

    const char* input_file = argv[1];
    std::cout << "Opening: " << input_file << "\n\n";

    // Open MP4 and locate metadata track
    auto extractor = Mp4MetadataExtractor::open(input_file);
    if (!extractor) {
        std::cerr << "Failed to open MP4 or find metadata track\n";
        return 1;
    }

    std::cout << "\n--- Extracting metadata samples ---\n";

    // Extract first few samples to verify
    const uint32_t samples_to_extract = std::min(5u, extractor->sample_count());
    for (uint32_t i = 0; i < samples_to_extract; ++i) {
        auto sample = extractor->extract_sample(i);
        if (sample) {
            std::cout << "Sample " << i << ":\n";
            std::cout << "  Timestamp: " << sample->timestamp_us << " µs\n";
            std::cout << "  Size: " << sample->data.size() << " bytes\n";
            std::cout << "  Data (hex): ";
            for (size_t j = 0; j < std::min(size_t(16), sample->data.size()); ++j) {
                std::cout << std::hex << std::setw(2) << std::setfill('0')
                          << static_cast<int>(sample->data[j]) << " ";
            }
            std::cout << std::dec;
            if (sample->data.size() > 16) {
                std::cout << "... (+" << (sample->data.size() - 16) << " bytes)";
            }
            std::cout << "\n\n";
        }
    }

    std::cout << "Successfully extracted " << samples_to_extract << " samples\n";
    std::cout << "Total samples in file: " << extractor->sample_count() << "\n";

    return 0;
}
