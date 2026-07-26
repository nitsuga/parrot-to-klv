// Copyright 2026 Eric Schneider
// Licensed under the Apache License, Version 2.0

#include "conformance.hpp"

#include "mp4_metadata_extractor.hpp"
#include "parrot_v3.hpp"

#define XXH_INLINE_ALL
#include "../external/xxhash.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

namespace parrot_to_klv {
namespace {

std::string json_escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if (c == '\n') out += "\\n";
        else out += c;
    }
    return out;
}

std::string round3(double v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.3f", v);
    return buf;
}

}  // namespace

ConformanceReport run_conformance(const std::string& input, const std::string& label) {
    ConformanceReport r;

    {
        std::ifstream in(input, std::ios::binary);
        if (!in) {
            r.error = "cannot read " + input;
            return r;
        }
        XXH3_state_t* state = XXH3_createState();
        if (!state) {
            r.error = "cannot allocate hash state";
            return r;
        }
        XXH3_128bits_reset(state);
        std::array<char, 65536> chunk{};
        constexpr size_t kProgressInterval = 100 * 1024 * 1024;  // 100 MB
        size_t next_progress = kProgressInterval;
        std::cout << "  computing xxHash";
        std::cout.flush();
        while (in.read(chunk.data(), chunk.size()) || in.gcount() > 0) {
            const auto got = static_cast<size_t>(in.gcount());
            XXH3_128bits_update(state, chunk.data(), got);
            r.input_bytes += got;
            if (r.input_bytes >= next_progress) {
                std::cout << ".";
                std::cout.flush();
                next_progress += kProgressInterval;
            }
        }
        const XXH128_hash_t hash = XXH3_128bits_digest(state);
        XXH3_freeState(state);

        std::ostringstream out;
        out << std::hex << std::setw(16) << std::setfill('0') << hash.high64
            << std::setw(16) << std::setfill('0') << hash.low64;
        r.input_hash = out.str();
        std::cout << " done\n";
    }

    // Use hash prefix as label if not explicitly provided (anonymizes sensitive filenames)
    r.label = label.empty() ? r.input_hash.substr(0, 16) : label;

    auto extractor = Mp4MetadataExtractor::open(input);
    if (!extractor) {
        r.error = "no Parrot metadata track";
        r.not_applicable = true;
        return r;
    }
    r.format = to_string(extractor->track_info().format);
    r.sample_count = extractor->sample_count();

    if (extractor->track_info().format != ParrotFormat::V3) {
        r.error = "conformance checking is implemented for v3 only; this file is " + r.format;
        r.not_applicable = true;
        return r;
    }

    // Assertions start true and are falsified by any sample that breaks them.
    r.checks["declared_length_matches_sample_size"] = true;
    r.checks["quaternions_are_unit_norm"] = true;
    r.checks["latitude_within_range"] = true;
    r.checks["longitude_within_range"] = true;
    r.checks["altitude_within_documented_range"] = true;
    r.checks["timestamp_extension_always_present"] = true;
    r.checks["timestamps_strictly_increasing"] = true;
    r.checks["fov_plausible"] = true;
    r.checks["battery_within_0_100"] = true;

    bool first = true;
    uint64_t previous_ts = 0;

    for (uint32_t i = 0; i < extractor->sample_count(); ++i) {
        auto sample = extractor->extract_sample(i);
        if (!sample) {
            ++r.failed;
            r.decode_errors["sample could not be extracted"]++;
            continue;
        }
        r.sample_size_histogram[static_cast<uint32_t>(sample->data.size())]++;

        ParrotV3Error err{};
        auto f = parse_v3(sample->data, &err);
        if (!f) {
            ++r.failed;
            r.decode_errors[to_string(err)]++;
            continue;
        }
        ++r.decoded;

        // Declared length must account for the whole sample.
        const size_t declared =
            4 + (static_cast<size_t>(sample->data[2]) << 8 | sample->data[3]) * 4;
        if (declared != sample->data.size()) {
            r.checks["declared_length_matches_sample_size"] = false;
        }

        // Seed once, on the first quaternion of the first decoded sample — not
        // once per quaternion, which would discard the two before it.
        bool first_quat = first;
        for (const auto& q : {f->drone_quat, f->frame_quat, f->frame_base_quat}) {
            const double n = q.norm();
            if (first_quat) { r.quat_norm_min = r.quat_norm_max = n; first_quat = false; }
            r.quat_norm_min = std::min(r.quat_norm_min, n);
            r.quat_norm_max = std::max(r.quat_norm_max, n);
            if (std::abs(n - 1.0) > 0.01) r.checks["quaternions_are_unit_norm"] = false;
        }

        // Range checks only — the values themselves never enter the report.
        if (!(f->latitude_deg >= -90.0 && f->latitude_deg <= 90.0)) {
            r.checks["latitude_within_range"] = false;
        }
        if (!(f->longitude_deg >= -180.0 && f->longitude_deg <= 180.0)) {
            r.checks["longitude_within_range"] = false;
        }
        if (!(f->altitude_amsl_m >= -900.0 && f->altitude_amsl_m <= 19000.0)) {
            r.checks["altitude_within_documented_range"] = false;
        }
        if (!(f->picture_hfov_deg > 0.0 && f->picture_hfov_deg <= 180.0 &&
              f->picture_vfov_deg > 0.0 && f->picture_vfov_deg <= 180.0)) {
            r.checks["fov_plausible"] = false;
        }
        if (f->battery_percentage > 100) r.checks["battery_within_0_100"] = false;

        if (!f->frame_timestamp_us) {
            r.checks["timestamp_extension_always_present"] = false;
        } else {
            const uint64_t ts = *f->frame_timestamp_us;
            if (!first) {
                if (ts <= previous_ts) r.checks["timestamps_strictly_increasing"] = false;
                const uint64_t d = ts > previous_ts ? ts - previous_ts : 0;
                r.timestamp_delta_min = r.timestamp_delta_min == 0
                                            ? d
                                            : std::min(r.timestamp_delta_min, d);
                r.timestamp_delta_max = std::max(r.timestamp_delta_max, d);
            }
            previous_ts = ts;
        }

        const int sv = f->sv_count, bat = f->battery_percentage, rssi = f->wifi_rssi_dbm;
        if (first) {
            r.sv_count_min = r.sv_count_max = sv;
            r.battery_min = r.battery_max = bat;
            r.rssi_min = r.rssi_max = rssi;
        }
        r.sv_count_min = std::min(r.sv_count_min, sv);
        r.sv_count_max = std::max(r.sv_count_max, sv);
        r.battery_min = std::min(r.battery_min, bat);
        r.battery_max = std::max(r.battery_max, bat);
        r.rssi_min = std::min(r.rssi_min, rssi);
        r.rssi_max = std::max(r.rssi_max, rssi);

        r.flying_states_seen.insert(to_string(f->flying_state));
        r.piloting_modes_seen.insert(to_string(f->piloting_mode));
        r.fov_seen.insert(round3(f->picture_hfov_deg) + " x " + round3(f->picture_vfov_deg));

        // Walk the extension chain for ids and shapes, including undecoded ones.
        size_t off = kParrotV3BaseSize;
        while (off + 4 <= declared && off + 4 <= sample->data.size()) {
            const uint16_t id =
                static_cast<uint16_t>(sample->data[off] << 8 | sample->data[off + 1]);
            const uint16_t words =
                static_cast<uint16_t>(sample->data[off + 2] << 8 | sample->data[off + 3]);
            r.extension_lengths[id].insert(words);
            r.extension_counts[id]++;
            off += 4 + static_cast<size_t>(words) * 4;
        }

        first = false;
    }

    r.ok = true;
    return r;
}

std::string to_json(const ConformanceReport& r) {
    std::ostringstream o;
    o << "{\n";
    o << "  \"schema_version\": " << kReportSchemaVersion << ",\n";
    o << "  \"label\": \"" << json_escape(r.label) << "\",\n";
    o << "  \"input_hash\": \"" << r.input_hash << "\",\n";
    o << "  \"input_bytes\": " << r.input_bytes << ",\n";
    o << "  \"format\": \"" << json_escape(r.format) << "\",\n";
    o << "  \"sample_count\": " << r.sample_count << ",\n";
    o << "  \"decoded\": " << r.decoded << ",\n";
    o << "  \"failed\": " << r.failed << ",\n";

    o << "  \"decode_errors\": {";
    bool sep = false;
    for (const auto& [k, v] : r.decode_errors) {
        o << (sep ? ", " : "") << "\"" << json_escape(k) << "\": " << v;
        sep = true;
    }
    o << "},\n";

    o << "  \"sample_size_histogram\": {";
    sep = false;
    for (const auto& [size, count] : r.sample_size_histogram) {
        o << (sep ? ", " : "") << "\"" << size << "\": " << count;
        sep = true;
    }
    o << "},\n";

    o << "  \"extensions\": {\n";
    sep = false;
    for (const auto& [id, words] : r.extension_lengths) {
        char idbuf[16];
        std::snprintf(idbuf, sizeof(idbuf), "0x%04X", id);
        o << (sep ? ",\n" : "") << "    \"" << idbuf << "\": {\"count\": "
          << r.extension_counts.at(id) << ", \"ext_length_words\": [";
        bool s2 = false;
        for (uint16_t w : words) { o << (s2 ? ", " : "") << w; s2 = true; }
        o << "], \"payload_bytes\": [";
        s2 = false;
        for (uint16_t w : words) { o << (s2 ? ", " : "") << (w * 4); s2 = true; }
        o << "]}";
        sep = true;
    }
    o << "\n  },\n";

    o << "  \"checks\": {\n";
    sep = false;
    for (const auto& [k, v] : r.checks) {
        o << (sep ? ",\n" : "") << "    \"" << json_escape(k) << "\": " << (v ? "true" : "false");
        sep = true;
    }
    o << "\n  },\n";

    o << "  \"quaternion_norm\": {\"min\": " << round3(r.quat_norm_min)
      << ", \"max\": " << round3(r.quat_norm_max) << "},\n";
    o << "  \"sv_count\": {\"min\": " << r.sv_count_min << ", \"max\": " << r.sv_count_max
      << "},\n";
    o << "  \"battery_percent\": {\"min\": " << r.battery_min << ", \"max\": " << r.battery_max
      << "},\n";
    o << "  \"wifi_rssi_dbm\": {\"min\": " << r.rssi_min << ", \"max\": " << r.rssi_max << "},\n";
    o << "  \"timestamp_delta_us\": {\"min\": " << r.timestamp_delta_min
      << ", \"max\": " << r.timestamp_delta_max << "},\n";

    const auto set_field = [&](const char* name, const std::set<std::string>& s, bool last) {
        o << "  \"" << name << "\": [";
        bool f = false;
        for (const auto& v : s) { o << (f ? ", " : "") << "\"" << json_escape(v) << "\""; f = true; }
        o << "]" << (last ? "\n" : ",\n");
    };
    set_field("flying_states_seen", r.flying_states_seen, false);
    set_field("piloting_modes_seen", r.piloting_modes_seen, false);
    set_field("field_of_view_seen", r.fov_seen, true);

    o << "}\n";
    return o.str();
}

}  // namespace parrot_to_klv
