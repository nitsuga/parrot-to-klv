// Copyright 2026 Eric Schneider
// Licensed under the Apache License, Version 2.0

#include "error_description.hpp"

namespace parrot_to_klv::detail {

std::string describe_misbklv_error(misbklv::Error error) {
    switch (error) {
        case misbklv::Error::Truncated:        return "packet truncated";
        case misbklv::Error::BadLength:        return "bad BER length";
        case misbklv::Error::UnknownTag:       return "unknown tag";
        case misbklv::Error::OutOfRange:       return "value out of range";
        case misbklv::Error::MissingMandatory: return "mandatory item missing";
        case misbklv::Error::RangeError:       return "value not encodable";
        case misbklv::Error::Backend:          return "media pipeline failed";
        case misbklv::Error::Unsupported:      return "unsupported by the backend";
        case misbklv::Error::TypeMismatch:     return "value type does not match item";
        case misbklv::Error::ResourceLimit:    return "resource limit exceeded";
    }
    return "unknown error";
}

}  // namespace parrot_to_klv::detail
