// Copyright 2026 Eric Schneider
// Licensed under the Apache License, Version 2.0

#include "conformance.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace parrot_to_klv;
namespace fs = std::filesystem;

namespace {

constexpr int kSkipExitCode = 77;  // ctest SKIP_RETURN_CODE

void usage(const char* argv0) {
    std::cerr
        << "Usage: " << argv0 << " --scan <dir> --reports <dir> [--check]\n"
        << "       " << argv0 << " <input.mp4> [--label <name>] [--out <report.json>]\n\n"
        << "Runs the decoder over real Parrot recordings and writes a conformance\n"
        << "report: what the encoder actually produced, in a form safe to commit.\n\n"
        << "The report holds only non-identifying facts — sample sizes, extension ids\n"
        << "and lengths, decode rates, quaternion norms, aircraft state ranges, and\n"
        << "pass/fail assertions. Position, absolute time and device identity are\n"
        << "excluded by construction: no coordinate is ever written, only whether it\n"
        << "fell in range.\n\n"
        << "  --scan <dir>      analyse every .mp4/.MP4 in <dir> (e.g. data/)\n"
        << "  --reports <dir>   where reports go, named by label\n"
        << "  --check           regenerate and compare instead of writing; non-zero\n"
        << "                    exit if a report changed. This is the regression test.\n"
        << "  --label <name>    name for a single recording (default: file stem)\n\n"
        << "Exits " << kSkipExitCode << " when --scan finds no recordings, so the\n"
        << "check is a no-op on machines without the footage.\n";
}

/// Returns 0 on success, 1 on failure, -1 when the input does not apply.
int emit(const ConformanceReport& report, const fs::path& out, bool check) {
    if (report.not_applicable) {
        std::cout << "  skipped — " << report.error << "\n";
        return -1;
    }
    if (!report.ok) {
        std::cerr << "  FAILED: " << report.error << "\n";
        return 1;
    }
    const std::string text = to_json(report);

    if (!check) {
        std::ofstream f(out);
        if (!f) {
            std::cerr << "  cannot write " << out << "\n";
            return 1;
        }
        f << text;
        std::cout << "  wrote " << out << " (" << report.decoded << "/" << report.sample_count
                  << " samples decoded)\n";
        return 0;
    }

    std::ifstream f(out);
    if (!f) {
        std::cerr << "  no committed report at " << out
                  << " — run without --check to create it\n";
        return 1;
    }
    std::stringstream committed;
    committed << f.rdbuf();
    if (committed.str() == text) {
        std::cout << "  matches " << out.filename() << "\n";
        return 0;
    }
    std::cerr << "  CHANGED: " << out
              << "\n  The decoder now reads this recording differently than when the\n"
                 "  report was committed. Re-run without --check to see the new values,\n"
                 "  and make sure the change was intended before committing them.\n";
    return 1;
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string scan_dir, reports_dir, input, label, out;
    bool check = false;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const auto next = [&](std::string& dst) { if (i + 1 < argc) dst = argv[++i]; };
        if (a == "--scan") next(scan_dir);
        else if (a == "--reports") next(reports_dir);
        else if (a == "--label") next(label);
        else if (a == "--out") next(out);
        else if (a == "--check") check = true;
        else if (a == "-h" || a == "--help") { usage(argv[0]); return 0; }
        else if (input.empty()) input = a;
    }

    if (!scan_dir.empty()) {
        if (reports_dir.empty()) { usage(argv[0]); return 2; }
        std::error_code ec;
        if (!fs::is_directory(scan_dir, ec)) {
            std::cout << "No " << scan_dir << " directory — skipping conformance check.\n";
            return kSkipExitCode;
        }
        std::vector<fs::path> inputs;
        for (const auto& e : fs::directory_iterator(scan_dir, ec)) {
            const auto ext = e.path().extension().string();
            if (ext == ".mp4" || ext == ".MP4") inputs.push_back(e.path());
        }
        std::sort(inputs.begin(), inputs.end());
        if (inputs.empty()) {
            std::cout << "No recordings in " << scan_dir << " — skipping conformance check.\n";
            return kSkipExitCode;
        }
        fs::create_directories(reports_dir, ec);

        int failures = 0;
        int applicable = 0;
        for (const auto& p : inputs) {
            const std::string name = p.stem().string();
            std::cout << name << ":\n";
            const auto report = run_conformance(p.string(), name);
            const int rc = emit(report, fs::path(reports_dir) / (name + ".json"), check);
            if (rc < 0) continue;
            ++applicable;
            failures += rc;
        }
        if (failures > 0) {
            std::cerr << "\n" << failures << " recording(s) failed the conformance check\n";
            return 1;
        }
        if (applicable == 0) {
            std::cout << "\nNo Parrot v3 recordings in " << scan_dir
                      << " — skipping conformance check.\n";
            return kSkipExitCode;
        }
        std::cout << "\nAll " << applicable << " recording(s) conform\n";
        return 0;
    }

    if (input.empty()) { usage(argv[0]); return 2; }
    if (label.empty()) label = fs::path(input).stem().string();
    if (out.empty()) out = label + ".json";

    std::cout << label << ":\n";
    const int rc = emit(run_conformance(input, label), out, check);
    return rc < 0 ? kSkipExitCode : rc;
}
