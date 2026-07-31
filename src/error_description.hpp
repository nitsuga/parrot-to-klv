// Copyright 2026 Eric Schneider
// Licensed under the Apache License, Version 2.0

#pragma once

#include <string>

#include "misbklv/types.hpp"

namespace parrot_to_klv::detail {

/// Render a libmisbklv error for this application's diagnostic messages.
std::string describe_misbklv_error(misbklv::Error error);

}  // namespace parrot_to_klv::detail
