// Copyright 2026 Eric Schneider
// Licensed under the Apache License, Version 2.0

#include "file_identity.hpp"

#include <system_error>

namespace parrot_to_klv {
namespace {

std::filesystem::path normalized_path(const std::filesystem::path& path) {
    std::error_code error;
    const auto absolute = std::filesystem::absolute(path, error);
    return (error ? path : absolute).lexically_normal();
}

std::filesystem::path resolved_path(const std::filesystem::path& path) {
    std::error_code error;
    const auto resolved = std::filesystem::weakly_canonical(path, error);
    return error ? normalized_path(path) : resolved.lexically_normal();
}

}  // namespace

bool paths_alias(const std::filesystem::path& first,
                 const std::filesystem::path& second) {
    // This catches identical and lexically-normalized paths even if neither
    // path exists yet.
    if (normalized_path(first) == normalized_path(second)) return true;

    // Resolve any symlinked portion of either path. weakly_canonical also works
    // for a not-yet-created output beneath an existing symlinked directory.
    if (resolved_path(first) == resolved_path(second)) return true;

    // When both paths exist, compare the underlying filesystem objects. This
    // catches hard links as well as symlinks on filesystems that support them.
    std::error_code error;
    return std::filesystem::equivalent(first, second, error) && !error;
}

}  // namespace parrot_to_klv
