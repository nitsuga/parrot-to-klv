// Copyright 2026 Eric Schneider
// Licensed under the Apache License, Version 2.0

#include "conformance.hpp"

#include "mp4_metadata_extractor.hpp"
#include "parrot_v3.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace parrot_to_klv {
namespace {

// --- Minimal SHA-256, so a report can name its input without embedding it ---

struct Sha256 {
    std::array<uint32_t, 8> h{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                              0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::array<uint8_t, 64> buf{};
    size_t buffered = 0;
    uint64_t total_bits = 0;

    static uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

    void block(const uint8_t* p) {
        static constexpr uint32_t k[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
            0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
            0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
            0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
            0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
            0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
            0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
            0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
            0xc67178f2};
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (uint32_t(p[i * 4]) << 24) | (uint32_t(p[i * 4 + 1]) << 16) |
                   (uint32_t(p[i * 4 + 2]) << 8) | p[i * 4 + 3];
        }
        for (int i = 16; i < 64; ++i) {
            const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        auto v = h;
        for (int i = 0; i < 64; ++i) {
            const uint32_t S1 = rotr(v[4], 6) ^ rotr(v[4], 11) ^ rotr(v[4], 25);
            const uint32_t ch = (v[4] & v[5]) ^ (~v[4] & v[6]);
            const uint32_t t1 = v[7] + S1 + ch + k[i] + w[i];
            const uint32_t S0 = rotr(v[0], 2) ^ rotr(v[0], 13) ^ rotr(v[0], 22);
            const uint32_t maj = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
            const uint32_t t2 = S0 + maj;
            v[7] = v[6]; v[6] = v[5]; v[5] = v[4]; v[4] = v[3] + t1;
            v[3] = v[2]; v[2] = v[1]; v[1] = v[0]; v[0] = t1 + t2;
        }
        for (int i = 0; i < 8; ++i) h[i] += v[i];
    }

    void update(const uint8_t* p, size_t n) {
        total_bits += static_cast<uint64_t>(n) * 8;
        while (n > 0) {
            const size_t take = std::min(n, 64 - buffered);
            std::copy_n(p, take, buf.begin() + static_cast<long>(buffered));
            buffered += take;
            p += take;
            n -= take;
            if (buffered == 64) {
                block(buf.data());
                buffered = 0;
            }
        }
    }

    std::string final_hex() {
        const uint64_t bits = total_bits;
        uint8_t pad = 0x80;
        update(&pad, 1);
        total_bits = bits;
        pad = 0;
        while (buffered != 56) {
            update(&pad, 1);
            total_bits = bits;
        }
        uint8_t len[8];
        for (int i = 0; i < 8; ++i) len[i] = static_cast<uint8_t>(bits >> (56 - i * 8));
        update(len, 8);

        std::ostringstream out;
        for (uint32_t x : h) out << std::hex << std::setw(8) << std::setfill('0') << x;
        return out.str();
    }
};

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
    r.label = label;

    {
        std::ifstream in(input, std::ios::binary);
        if (!in) {
            r.error = "cannot read " + input;
            return r;
        }
        Sha256 sha;
        std::array<char, 65536> chunk{};
        while (in.read(chunk.data(), chunk.size()) || in.gcount() > 0) {
            const auto got = static_cast<size_t>(in.gcount());
            sha.update(reinterpret_cast<const uint8_t*>(chunk.data()), got);
            r.input_bytes += got;
        }
        r.input_sha256 = sha.final_hex();
    }

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

        for (const auto& q : {f->drone_quat, f->frame_quat, f->frame_base_quat}) {
            const double n = q.norm();
            if (first) { r.quat_norm_min = r.quat_norm_max = n; }
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
    o << "  \"input_sha256\": \"" << r.input_sha256 << "\",\n";
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
