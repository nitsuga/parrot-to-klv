// Copyright 2026 Eric Schneider
// Licensed under the Apache License, Version 2.0
//
// End-to-end CLI integration test: exercises parrot-to-klv binary directly
// with full option combinations, verifying exit code, output creation,
// and deliverable content (KLVA descriptor and ST 0604 SEI).
//
// argv: <parrot-to-klv-bin> <fixtures-dir> <temp-dir>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

int g_failures = 0;

void check(bool condition, const std::string& what) {
    if (!condition) {
        std::cerr << "FAIL: " << what << "\n";
        ++g_failures;
    }
}

bool file_contains_subsequence(const fs::path& path, const std::string& needle) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::string content((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
    return content.find(needle) != std::string::npos;
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " <parrot-to-klv-bin> <fixtures-dir> <temp-dir>\n";
        return 2;
    }

    const fs::path bin = argv[1];
    const fs::path fixtures = argv[2];
    const fs::path temp = argv[3];

    check(fs::exists(bin), "parrot-to-klv binary exists");

    // 1. Happy-path v3 conversion with options
    const fs::path v3_in = fixtures / "parrot_v3_video.mp4";
    const fs::path v3_out = temp / "cli_v3_converted.ts";
    std::error_code ec;
    fs::remove(v3_out, ec);

    std::string cmd_v3 = bin.string() + " " + v3_in.string() + " -o " + v3_out.string() +
                         " --clamp-attitude --infer-image-source";
    int ret_v3 = std::system(cmd_v3.c_str());
    check(ret_v3 == 0, "parrot-to-klv converts v3 video fixture cleanly");
    check(fs::exists(v3_out) && fs::file_size(v3_out) > 0, "v3 output file created and non-empty");
    check(file_contains_subsequence(v3_out, "KLVA"), "v3 output contains KLVA registration descriptor");
    check(file_contains_subsequence(v3_out, "MISPmicrosectime"), "v3 output carries ST 0604 SEI");
    fs::remove(v3_out, ec);

    // 2. Happy-path Proto conversion
    const fs::path proto_in = fixtures / "parrot_proto_video.mp4";
    const fs::path proto_out = temp / "cli_proto_converted.ts";
    fs::remove(proto_out, ec);

    std::string cmd_proto = bin.string() + " " + proto_in.string() + " -o " + proto_out.string();
    int ret_proto = std::system(cmd_proto.c_str());
    check(ret_proto == 0, "parrot-to-klv converts proto video fixture cleanly");
    check(fs::exists(proto_out) && fs::file_size(proto_out) > 0, "proto output file created and non-empty");
    check(file_contains_subsequence(proto_out, "KLVA"), "proto output contains KLVA descriptor");
    fs::remove(proto_out, ec);

    // 3. Happy-path v3 conversion via explicit -i flag (unified CLI)
    const fs::path v3_out_i = temp / "cli_v3_i_converted.ts";
    fs::remove(v3_out_i, ec);
    std::string cmd_v3_i = bin.string() + " -i " + v3_in.string() + " -o " + v3_out_i.string();
    int ret_v3_i = std::system(cmd_v3_i.c_str());
    check(ret_v3_i == 0, "parrot-to-klv converts v3 video fixture via -i flag");
    check(fs::exists(v3_out_i) && fs::file_size(v3_out_i) > 0, "v3 -i output file created and non-empty");
    check(file_contains_subsequence(v3_out_i, "KLVA"), "v3 -i output contains KLVA descriptor");
    fs::remove(v3_out_i, ec);

    // 4. `file:` is a sink prefix, not part of the output filesystem path.
    const fs::path v3_out_file_uri = temp / "cli_v3_file_uri.ts";
    fs::remove(v3_out_file_uri, ec);
    std::string cmd_v3_file_uri = bin.string() + " " + v3_in.string() + " -o file:" +
                                  v3_out_file_uri.string();
    int ret_v3_file_uri = std::system(cmd_v3_file_uri.c_str());
    check(ret_v3_file_uri == 0, "parrot-to-klv accepts file: output");
    check(fs::exists(v3_out_file_uri) && fs::file_size(v3_out_file_uri) > 0,
          "file: output creates the requested path");
    fs::remove(v3_out_file_uri, ec);

    // 5. Inspection mode (no output flag) with disclosure flags
    std::string cmd_inspect = bin.string() + " " + (fixtures / "parrot_v3.mp4").string() +
                              " --with-location --with-identity > /dev/null";
    int ret_inspect = std::system(cmd_inspect.c_str());
    check(ret_inspect == 0, "parrot-to-klv inspection mode exits 0");

    // 6. Geotag is location data, not identity data.
    const fs::path identity_report = temp / "cli_identity_report.txt";
    const fs::path location_report = temp / "cli_location_report.txt";
    std::string cmd_identity = bin.string() + " " + (fixtures / "parrot_v3.mp4").string() +
                               " --with-identity > " + identity_report.string();
    std::string cmd_location = bin.string() + " " + (fixtures / "parrot_v3.mp4").string() +
                               " --with-location > " + location_report.string();
    check(std::system(cmd_identity.c_str()) == 0, "identity-only inspection exits 0");
    check(file_contains_subsequence(identity_report, "geotag: withheld"),
          "identity-only inspection withholds geotag");
    check(std::system(cmd_location.c_str()) == 0, "location inspection exits 0");
    check(file_contains_subsequence(location_report, "geotag: +"),
          "location inspection reveals geotag");
    fs::remove(identity_report, ec);
    fs::remove(location_report, ec);

    // 7. Inspection mode via -i
    std::string cmd_inspect_i = bin.string() + " -i " + (fixtures / "parrot_v3.mp4").string() +
                                " --with-location > /dev/null";
    int ret_inspect_i = std::system(cmd_inspect_i.c_str());
    check(ret_inspect_i == 0, "parrot-to-klv inspection via -i exits 0");

    if (g_failures == 0) {
        std::cout << "All CLI integration tests passed\n";
        return 0;
    }
    std::cerr << g_failures << " CLI check(s) failed\n";
    return 1;
}
