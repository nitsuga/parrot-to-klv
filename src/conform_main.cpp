// Copyright 2026 Eric Schneider
// Licensed under the Apache License, Version 2.0

#include "conformance.hpp"

#include <algorithm>
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
        << "Report files are named using the first 16 characters of the input file's\n"
        << "xxHash, anonymizing sensitive source filenames while uniquely identifying\n"
        << "each recording.\n\n"
        << "  --scan <dir>      analyze every .mp4/.MP4 in <dir> (e.g. data/)\n"
        << "  --reports <dir>   where reports go, named by hash prefix\n"
        << "  --check           regenerate and compare instead of writing; non-zero\n"
        << "                    exit if a report changed. This is the regression test.\n"
        << "  --label <name>    override label in report (default: hash prefix)\n\n"
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
        FILE* f = std::fopen(out.c_str(), "w");
        if (!f) {
            std::cerr << "  cannot write " << out << "\n";
            return 1;
        }
        if (std::fwrite(text.data(), 1, text.size(), f) != text.size() || std::fflush(f) != 0) {
            std::fclose(f);
            std::error_code ec;
            fs::remove(out, ec);
            std::cerr << "  write failed: " << out << "\n";
            return 1;
        }
        if (std::fclose(f) != 0) {
            std::error_code ec;
            fs::remove(out, ec);
            std::cerr << "  close failed: " << out << "\n";
            return 1;
        }
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
    bool label_given = false;
    bool out_given = false;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        // A trailing flag with no value must stop the program, not silently
        // leave the destination string empty (which reads the same as the
        // flag never having been passed at all).
        const auto next = [&](std::string& dst) -> bool {
            if (i + 1 >= argc) {
                std::cerr << "Error: " << a << " needs a value.\n";
                return false;
            }
            dst = argv[++i];
            return true;
        };
        if (a == "--scan") {
            if (!next(scan_dir)) return 1;
        } else if (a == "--reports") {
            if (!next(reports_dir)) return 1;
        } else if (a == "--label") {
            if (!next(label)) return 1;
            label_given = true;
        } else if (a == "--out") {
            if (!next(out)) return 1;
            out_given = true;
        } else if (a == "--check") {
            check = true;
        } else if (a == "-h" || a == "--help") {
            usage(argv[0]);
            return 0;
        } else if (!a.empty() && a[0] == '-') {
            // Unrecognized flags are refused, never silently taken as the
            // input path (an unknown "--bogus" used to become `input`) or
            // dropped outright.
            std::cerr << "Error: unknown option '" << a << "'.\n"
                      << "Run " << argv[0] << " --help for the options.\n";
            return 1;
        } else if (input.empty()) {
            input = a;
        } else {
            std::cerr << "Error: unexpected extra argument '" << a << "'.\n";
            return 1;
        }
    }

    if (!scan_dir.empty()) {
        if (reports_dir.empty()) { usage(argv[0]); return 2; }
        // --label and --out each name one report; scan mode processes a
        // whole directory and names each report from its own file hash, so
        // neither has a meaning to silently drop here (the old code hardcoded
        // an empty label into every call, discarding --label with no error).
        if (label_given || out_given) {
            std::cerr << "Error: --label/--out apply to single-file mode; --scan names "
                         "each report from its own file hash.\n";
            return 1;
        }
        std::error_code ec;
        if (!fs::is_directory(scan_dir, ec)) {
            std::cout << "No " << scan_dir << " directory — skipping conformance check.\n";
            return kSkipExitCode;
        }
        std::vector<fs::path> inputs;
        // A fresh error_code per call: a stale one left set by an earlier,
        // unrelated call would otherwise make the checks below fire on the
        // wrong error, or not at all.
        std::error_code iterate_ec;
        // directory_iterator(path, ec) only guards *construction* -- range-for
        // calls the throwing operator++() internally, so a directory that
        // changes mid-scan (a recording removed, a permission change) would
        // otherwise surface as an uncaught filesystem_error and crash the
        // whole scan instead of reporting one bad entry. The manual loop
        // below uses increment(ec) so that stays a checked, reported failure.
        fs::directory_iterator it(scan_dir, iterate_ec);
        if (iterate_ec) {
            std::cerr << "Error: cannot list " << scan_dir << ": " << iterate_ec.message()
                      << "\n";
            return 1;
        }
        const fs::directory_iterator end;
        for (; it != end; it.increment(iterate_ec)) {
            if (iterate_ec) {
                std::cerr << "Error: " << scan_dir << " changed while being scanned: "
                          << iterate_ec.message() << "\n";
                return 1;
            }
            const auto ext = it->path().extension().string();
            if (ext == ".mp4" || ext == ".MP4") inputs.push_back(it->path());
        }
        std::sort(inputs.begin(), inputs.end());
        if (inputs.empty()) {
            std::cout << "No recordings in " << scan_dir << " — skipping conformance check.\n";
            return kSkipExitCode;
        }
        std::error_code mkdir_ec;
        fs::create_directories(reports_dir, mkdir_ec);
        if (mkdir_ec) {
            std::cerr << "Error: cannot create " << reports_dir << ": " << mkdir_ec.message()
                      << "\n";
            return 1;
        }

        int failures = 0;
        int applicable = 0;
        for (const auto& p : inputs) {
            std::error_code size_ec;
            const uintmax_t size = fs::file_size(p, size_ec);
            if (size_ec) {
                // A recording that vanished (or became unreadable) between the
                // scan above and here must be reported, not silently skipped
                // or shown as a bogus "0 MB" (file_size(p, ec) on error
                // returns static_cast<uintmax_t>(-1), which / (1024*1024)
                // would otherwise print as an enormous, equally wrong size).
                std::cerr << p.filename().string() << ": cannot stat: " << size_ec.message()
                          << "\n";
                ++failures;
                continue;
            }
            std::cout << p.filename().string() << " (" << (size / (1024 * 1024)) << " MB):\n";
            const auto report = run_conformance(p.string(), "");
            if (report.input_hash.empty()) {
                std::cerr << "  failed to compute hash\n";
                ++failures;
                continue;
            }
            // Use first 16 chars of hash as anonymous filename
            const std::string report_name = report.input_hash.substr(0, 16);
            const int rc = emit(report, fs::path(reports_dir) / (report_name + ".json"), check);
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

    std::error_code ec;
    const uintmax_t size = fs::file_size(input, ec);
    std::cout << fs::path(input).filename().string() << " (" << (ec ? 0 : size / (1024 * 1024)) << " MB):\n";
    const auto report = run_conformance(input, label);

    if (report.input_hash.empty()) {
        std::cerr << "  failed to compute hash\n";
        return 1;
    }

    // Default to hash-based anonymous filename if not specified
    if (label.empty()) label = report.input_hash.substr(0, 16);
    if (out.empty()) out = report.input_hash.substr(0, 16) + ".json";

    const int rc = emit(report, out, check);
    return rc < 0 ? kSkipExitCode : rc;
}
