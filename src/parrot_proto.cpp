// Copyright 2026 Eric Schneider
// Licensed under the Apache License, Version 2.0

#include "parrot_proto.hpp"

#include <cstring>

namespace parrot_to_klv {
namespace {

/// Proto3 wire types. 6 and 7 are undefined; 3 and 4 (start/end group) are
/// proto2-only and cannot appear in a proto3 message.
enum WireType : uint32_t {
    kVarint = 0,
    kFixed64 = 1,
    kLengthDelimited = 2,
    kStartGroup = 3,
    kEndGroup = 4,
    kFixed32 = 5,
};

/// Cursor over one message's bytes.
///
/// Every read is bounds-checked and sets the error on failure; once failed the
/// reader stays failed, so a caller can run a whole field loop and test once at
/// the end rather than after each read.
class Reader {
public:
    Reader(std::span<const uint8_t> buf, ParrotProtoError* error)
        : buf_(buf), error_(error) {}

    bool ok() const { return ok_; }
    bool eof() const { return pos_ >= buf_.size(); }

    void fail(ParrotProtoError e) {
        if (ok_ && error_) *error_ = e;
        ok_ = false;
    }

    bool read_varint(uint64_t& out) {
        if (!ok_) return false;
        uint64_t value = 0;
        for (int shift = 0; shift < 70; shift += 7) {
            if (pos_ >= buf_.size()) {
                fail(ParrotProtoError::Truncated);
                return false;
            }
            const uint8_t byte = buf_[pos_++];
            // The 10th byte contributes only one bit; higher bits are garbage
            // from an over-long encoding, so mask rather than shift them out.
            value |= static_cast<uint64_t>(byte & 0x7F) << shift;
            if ((byte & 0x80) == 0) {
                out = value;
                return true;
            }
        }
        fail(ParrotProtoError::VarintTooLong);
        return false;
    }

    bool read_fixed32(uint32_t& out) {
        if (!ok_) return false;
        if (pos_ + 4 > buf_.size()) {
            fail(ParrotProtoError::Truncated);
            return false;
        }
        out = static_cast<uint32_t>(buf_[pos_]) |
              static_cast<uint32_t>(buf_[pos_ + 1]) << 8 |
              static_cast<uint32_t>(buf_[pos_ + 2]) << 16 |
              static_cast<uint32_t>(buf_[pos_ + 3]) << 24;
        pos_ += 4;
        return true;
    }

    bool read_fixed64(uint64_t& out) {
        if (!ok_) return false;
        if (pos_ + 8 > buf_.size()) {
            fail(ParrotProtoError::Truncated);
            return false;
        }
        out = 0;
        for (int i = 7; i >= 0; --i) out = out << 8 | buf_[pos_ + static_cast<size_t>(i)];
        pos_ += 8;
        return true;
    }

    bool read_bytes(std::span<const uint8_t>& out) {
        uint64_t len = 0;
        if (!read_varint(len)) return false;
        if (len > buf_.size() - pos_) {  // no overflow: pos_ <= size()
            fail(ParrotProtoError::Truncated);
            return false;
        }
        out = buf_.subspan(pos_, static_cast<size_t>(len));
        pos_ += static_cast<size_t>(len);
        return true;
    }

    /// Read a tag. Returns false at end of message or on error — callers
    /// distinguish the two with `ok()`.
    bool read_tag(uint32_t& field, uint32_t& wire) {
        if (eof()) return false;
        uint64_t tag = 0;
        if (!read_varint(tag)) return false;
        field = static_cast<uint32_t>(tag >> 3);
        wire = static_cast<uint32_t>(tag & 0x7);
        if (field == 0) {
            fail(ParrotProtoError::BadWireType);
            return false;
        }
        return true;
    }

    /// Advance past a field whose value we do not want. This is what makes the
    /// reader forward-compatible: an unrecognised field costs a skip, not a
    /// parse failure.
    bool skip(uint32_t wire) {
        switch (wire) {
            case kVarint: {
                uint64_t ignored = 0;
                return read_varint(ignored);
            }
            case kFixed64: {
                uint64_t ignored = 0;
                return read_fixed64(ignored);
            }
            case kLengthDelimited: {
                std::span<const uint8_t> ignored;
                return read_bytes(ignored);
            }
            case kFixed32: {
                uint32_t ignored = 0;
                return read_fixed32(ignored);
            }
            default:
                // Groups (3/4) cannot occur in proto3, and 6/7 are undefined.
                // Either means we have lost sync; guessing a length would turn
                // a detectable error into silent garbage.
                fail(ParrotProtoError::BadWireType);
                return false;
        }
    }

private:
    std::span<const uint8_t> buf_;
    size_t pos_ = 0;
    bool ok_ = true;
    ParrotProtoError* error_ = nullptr;
};

float to_float(uint32_t bits) {
    float f = 0.0F;
    std::memcpy(&f, &bits, sizeof f);
    return f;
}

double to_double(uint64_t bits) {
    double d = 0.0;
    std::memcpy(&d, &bits, sizeof d);
    return d;
}

/// ZigZag decode. `sint32`/`sint64` use it; plain `int32` does not, and
/// confusing the two turns small negatives into huge positives.
int64_t unzigzag(uint64_t n) {
    return static_cast<int64_t>(n >> 1) ^ -static_cast<int64_t>(n & 1);
}

/// Read one field's value as a double, accepting either float or double on the
/// wire. The schema fixes which each field is, but tolerating both costs
/// nothing and survives a schema that widens a float to a double.
bool read_real(Reader& r, uint32_t wire, double& out) {
    if (wire == kFixed32) {
        uint32_t bits = 0;
        if (!r.read_fixed32(bits)) return false;
        out = static_cast<double>(to_float(bits));
        return true;
    }
    if (wire == kFixed64) {
        uint64_t bits = 0;
        if (!r.read_fixed64(bits)) return false;
        out = to_double(bits);
        return true;
    }
    return r.skip(wire);
}

bool read_uint(Reader& r, uint32_t wire, uint64_t& out) {
    if (wire != kVarint) return r.skip(wire);
    return r.read_varint(out);
}

/// Read a nested message, insisting on the length-delimited wire type.
///
/// Checking is not pedantry: without it a group tag (wire 3) on a message field
/// would read the following byte as a length, and a zero there would decode as
/// a perfectly empty submessage. A mis-synced stream would then parse "cleanly"
/// into a default-valued frame instead of being rejected.
bool read_msg(Reader& r, uint32_t wire, std::span<const uint8_t>& out) {
    if (wire != kLengthDelimited) {
        r.fail(ParrotProtoError::BadWireType);
        return false;
    }
    return r.read_bytes(out);
}

// Each parse_* takes the bytes of one message. The pattern throughout is: read
// tag, switch on field number, skip anything else. No parser calls itself, so
// nesting is bounded by the schema rather than by the input.

bool parse_quaternion(std::span<const uint8_t> b, ParrotProtoError* err, Quaternion& out) {
    Reader r(b, err);
    uint32_t field = 0, wire = 0;
    while (r.read_tag(field, wire)) {
        switch (field) {
            case 1: read_real(r, wire, out.w); break;
            case 2: read_real(r, wire, out.x); break;
            case 3: read_real(r, wire, out.y); break;
            case 4: read_real(r, wire, out.z); break;
            default: r.skip(wire); break;
        }
    }
    return r.ok();
}

bool parse_location(std::span<const uint8_t> b, ParrotProtoError* err, ProtoLocation& out) {
    Reader r(b, err);
    uint32_t field = 0, wire = 0;
    while (r.read_tag(field, wire)) {
        uint64_t u = 0;
        switch (field) {
            case 1: read_real(r, wire, out.latitude_deg); break;
            case 2: read_real(r, wire, out.longitude_deg); break;
            case 3: read_real(r, wire, out.altitude_wgs84_m); break;
            case 4:
                if (read_uint(r, wire, u)) out.sv_count = static_cast<uint32_t>(u);
                break;
            case 5: read_real(r, wire, out.horizontal_accuracy_m); break;
            case 6: read_real(r, wire, out.vertical_accuracy_m); break;
            // 7, not 4 — the AMSL altitude sits after the accuracies.
            case 7: read_real(r, wire, out.altitude_egm96_m); break;
            default: r.skip(wire); break;
        }
    }
    return r.ok();
}

bool parse_ned(std::span<const uint8_t> b, ParrotProtoError* err, ProtoNed& out) {
    Reader r(b, err);
    uint32_t field = 0, wire = 0;
    while (r.read_tag(field, wire)) {
        switch (field) {
            case 1: read_real(r, wire, out.north_mps); break;
            case 2: read_real(r, wire, out.east_mps); break;
            case 3: read_real(r, wire, out.down_mps); break;
            default: r.skip(wire); break;
        }
    }
    return r.ok();
}

bool parse_drone(std::span<const uint8_t> b, ParrotProtoError* err, ParrotProtoFrame& out) {
    Reader r(b, err);
    uint32_t field = 0, wire = 0;
    while (r.read_tag(field, wire)) {
        std::span<const uint8_t> nested;
        uint64_t u = 0;
        switch (field) {
            case 1:
                if (read_msg(r, wire, nested) && !parse_quaternion(nested, err, out.drone_quat))
                    return false;
                break;
            case 2:
                if (read_msg(r, wire, nested) && !parse_location(nested, err, out.drone_location))
                    return false;
                break;
            case 3: read_real(r, wire, out.ground_distance_m); break;
            case 4:
                if (read_msg(r, wire, nested) && !parse_ned(nested, err, out.speed)) return false;
                break;
            // sint32: ZigZag. Plain varint here would read -1 as 4294967295.
            case 5:
                if (read_uint(r, wire, u))
                    out.battery_percentage = static_cast<int32_t>(unzigzag(u));
                break;
            case 7:
                if (read_uint(r, wire, u) && u <= 8)
                    out.flying_state = static_cast<FlyingState>(u);
                break;
            case 9:
                if (read_msg(r, wire, nested) && !parse_ned(nested, err, out.position)) return false;
                break;
            case 11:
                if (read_uint(r, wire, u)) out.animation_in_progress = u != 0;
                break;
            case 12:
                if (read_uint(r, wire, u) && u <= 6)
                    out.piloting_mode = static_cast<ProtoPilotingMode>(u);
                break;
            case 13: read_real(r, wire, out.altitude_ato_m); break;
            default: r.skip(wire); break;
        }
    }
    return r.ok();
}

bool parse_vector2(std::span<const uint8_t> b, ParrotProtoError* err, double& x, double& y) {
    Reader r(b, err);
    uint32_t field = 0, wire = 0;
    while (r.read_tag(field, wire)) {
        switch (field) {
            case 1: read_real(r, wire, x); break;
            case 2: read_real(r, wire, y); break;
            default: r.skip(wire); break;
        }
    }
    return r.ok();
}

bool parse_camera(std::span<const uint8_t> b, ParrotProtoError* err, ParrotProtoFrame& out) {
    Reader r(b, err);
    uint32_t field = 0, wire = 0;
    while (r.read_tag(field, wire)) {
        std::span<const uint8_t> nested;
        uint64_t u = 0;
        switch (field) {
            case 1:
                if (read_uint(r, wire, u)) out.camera_timestamp_us = u;
                break;
            case 2:
                if (read_msg(r, wire, nested) && !parse_quaternion(nested, err, out.frame_base_quat))
                    return false;
                break;
            case 3:
                if (read_msg(r, wire, nested) && !parse_quaternion(nested, err, out.frame_quat))
                    return false;
                break;
            case 4: read_real(r, wire, out.exposure_time_ms); break;
            case 5:
                if (read_uint(r, wire, u)) out.iso_gain = static_cast<uint32_t>(u);
                break;
            case 6: read_real(r, wire, out.awb_r_gain); break;
            case 7: read_real(r, wire, out.awb_b_gain); break;
            // Radians, unlike v3's degrees.
            case 8: read_real(r, wire, out.picture_hfov_rad); break;
            case 9: read_real(r, wire, out.picture_vfov_rad); break;
            case 10:
                if (read_uint(r, wire, u)) out.utc_timestamp_us = u;
                break;
            case 11:
                if (read_uint(r, wire, u)) out.utc_timestamp_accuracy = static_cast<uint32_t>(u);
                break;
            case 13:
                if (read_msg(r, wire, nested) && !parse_location(nested, err, out.camera_location))
                    return false;
                break;
            case 14:
                if (read_msg(r, wire, nested) &&
                    !parse_vector2(nested, err, out.principal_point_x, out.principal_point_y))
                    return false;
                break;
            case 15:
                if (read_msg(r, wire, nested) && !parse_quaternion(nested, err, out.frame_local_quat))
                    return false;
                break;
            case 16: read_real(r, wire, out.zoom_level); break;
            case 17:
                if (read_uint(r, wire, u) && u <= 3) out.spectrum = static_cast<CameraSpectrum>(u);
                break;
            // 19 serial_number is device identity: skipped like any other
            // unmapped field, so it never reaches a frame or a report.
            default: r.skip(wire); break;
        }
    }
    return r.ok();
}

bool parse_wifi(std::span<const uint8_t> b, ParrotProtoError* err, ProtoWifiLink& out) {
    Reader r(b, err);
    uint32_t field = 0, wire = 0;
    while (r.read_tag(field, wire)) {
        uint64_t u = 0;
        switch (field) {
            case 1:
                if (read_uint(r, wire, u)) out.goodput_kbps = static_cast<uint32_t>(u);
                break;
            case 2:
                if (read_uint(r, wire, u)) out.quality = static_cast<uint32_t>(u);
                break;
            case 3:  // sint32: ZigZag. RSSI is always negative in practice.
                if (read_uint(r, wire, u)) out.rssi_dbm = static_cast<int32_t>(unzigzag(u));
                break;
            default: r.skip(wire); break;
        }
    }
    return r.ok();
}

/// `LinkMetadata` is a oneof; on the wire that is just two optional fields, so
/// "last one wins" falls out of the loop. A Starfish link is skipped — we have
/// no mapping for it — but its presence must not be mistaken for a Wi-Fi link.
bool parse_link(std::span<const uint8_t> b, ParrotProtoError* err, ParrotProtoFrame& out) {
    Reader r(b, err);
    uint32_t field = 0, wire = 0;
    while (r.read_tag(field, wire)) {
        std::span<const uint8_t> nested;
        if (field == 1) {
            ProtoWifiLink wifi;
            if (read_msg(r, wire, nested) && !parse_wifi(nested, err, wifi)) return false;
            out.wifi_links.push_back(wifi);
        } else {
            r.skip(wire);
        }
    }
    return r.ok();
}

bool parse_lfic(std::span<const uint8_t> b, ParrotProtoError* err, ProtoLfic& out) {
    Reader r(b, err);
    uint32_t field = 0, wire = 0;
    while (r.read_tag(field, wire)) {
        std::span<const uint8_t> nested;
        uint64_t u = 0;
        switch (field) {
            case 1: read_real(r, wire, out.x); break;
            case 2: read_real(r, wire, out.y); break;
            case 3:
                if (read_msg(r, wire, nested) && !parse_location(nested, err, out.location))
                    return false;
                break;
            case 4: read_real(r, wire, out.grid_precision_m); break;
            case 5:
                if (read_uint(r, wire, u) && u <= 1) out.type = static_cast<LficType>(u);
                break;
            case 6: read_real(r, wire, out.horizontal_tangential_accuracy_m); break;
            case 7: read_real(r, wire, out.horizontal_radial_accuracy_m); break;
            default: r.skip(wire); break;
        }
    }
    return r.ok();
}

bool parse_bbox(std::span<const uint8_t> b, ParrotProtoError* err, ProtoBoundingBox& out) {
    Reader r(b, err);
    uint32_t field = 0, wire = 0;
    while (r.read_tag(field, wire)) {
        uint64_t u = 0;
        switch (field) {
            case 1: read_real(r, wire, out.x); break;
            case 2: read_real(r, wire, out.y); break;
            case 3: read_real(r, wire, out.width); break;
            case 4: read_real(r, wire, out.height); break;
            case 5:
                if (read_uint(r, wire, u)) out.object_class = static_cast<uint32_t>(u);
                break;
            case 6: read_real(r, wire, out.confidence); break;
            case 7:
                if (read_uint(r, wire, u)) out.uid = static_cast<uint32_t>(u);
                break;
            default: r.skip(wire); break;
        }
    }
    return r.ok();
}

bool parse_tracking(std::span<const uint8_t> b, ParrotProtoError* err, ProtoTracking& out) {
    Reader r(b, err);
    uint32_t field = 0, wire = 0;
    while (r.read_tag(field, wire)) {
        std::span<const uint8_t> nested;
        uint64_t u = 0;
        switch (field) {
            case 1: {
                ProtoBoundingBox box;
                if (read_msg(r, wire, nested) && !parse_bbox(nested, err, box)) return false;
                out.target = box;
                break;
            }
            case 2:
                if (read_uint(r, wire, u)) out.timestamp_us = u;
                break;
            case 3:
                if (read_uint(r, wire, u)) out.quality = static_cast<uint32_t>(u);
                break;
            case 4:
                if (read_uint(r, wire, u) && u <= 2) out.state = static_cast<TrackingState>(u);
                break;
            default: r.skip(wire); break;
        }
    }
    return r.ok();
}

}  // namespace

std::string to_string(ProtoPilotingMode mode) {
    switch (mode) {
        case ProtoPilotingMode::Unknown: return "unknown";
        case ProtoPilotingMode::Manual: return "manual";
        case ProtoPilotingMode::ReturnHome: return "return-home";
        case ProtoPilotingMode::FlightPlan: return "flight-plan";
        case ProtoPilotingMode::Tracking: return "tracking";
        case ProtoPilotingMode::MoveTo: return "move-to";
        case ProtoPilotingMode::MagicCarpet: return "magic-carpet";
    }
    return "invalid";
}

std::string to_string(CameraSpectrum spectrum) {
    switch (spectrum) {
        case CameraSpectrum::Unknown: return "unknown";
        case CameraSpectrum::Visible: return "visible";
        case CameraSpectrum::Blended: return "blended";
        case CameraSpectrum::Thermal: return "thermal";
    }
    return "invalid";
}

std::string to_string(LficType type) {
    switch (type) {
        case LficType::Cot: return "cursor-on-target";
        case LficType::User: return "user";
    }
    return "invalid";
}

std::string to_string(TrackingState state) {
    switch (state) {
        case TrackingState::Tracking: return "tracking";
        case TrackingState::Searching: return "searching";
        case TrackingState::Pause: return "pause";
    }
    return "invalid";
}

std::string to_string(ParrotProtoError error) {
    switch (error) {
        case ParrotProtoError::Truncated: return "field runs past end of sample";
        case ParrotProtoError::BadWireType: return "undefined wire type";
        case ParrotProtoError::VarintTooLong: return "varint longer than 10 bytes";
    }
    return "unknown error";
}

std::optional<ParrotProtoFrame> parse_proto(std::span<const uint8_t> sample,
                                            ParrotProtoError* error) {
    ParrotProtoError local{};
    ParrotProtoError* err = error ? error : &local;

    ParrotProtoFrame frame;
    Reader r(sample, err);
    uint32_t field = 0, wire = 0;
    while (r.read_tag(field, wire)) {
        std::span<const uint8_t> nested;
        switch (field) {
            case 1:
                if (!read_msg(r, wire, nested)) break;
                if (!parse_drone(nested, err, frame)) return std::nullopt;
                break;
            case 2:
                if (!read_msg(r, wire, nested)) break;
                if (!parse_camera(nested, err, frame)) return std::nullopt;
                break;
            case 3:
                if (!read_msg(r, wire, nested)) break;
                if (!parse_link(nested, err, frame)) return std::nullopt;
                break;
            case 4: {
                if (!read_msg(r, wire, nested)) break;
                ProtoTracking tracking;
                if (!parse_tracking(nested, err, tracking)) return std::nullopt;
                frame.tracking = tracking;
                break;
            }
            case 8: {
                if (!read_msg(r, wire, nested)) break;
                ProtoLfic entry;
                if (!parse_lfic(nested, err, entry)) return std::nullopt;
                frame.lfic.push_back(entry);
                break;
            }
            default:
                // 5 proposal, 6 automation, 7 thermal, 9 user, 10 photo, and
                // anything a future SDK adds.
                frame.skipped_field_numbers.push_back(field);
                r.skip(wire);
                break;
        }
    }
    if (!r.ok()) return std::nullopt;
    return frame;
}

}  // namespace parrot_to_klv
