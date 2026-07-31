// Copyright 2026 Eric Schneider
// Licensed under the Apache License, Version 2.0

#pragma once

#include <filesystem>

namespace parrot_to_klv {

/// Whether two paths could name the same file.
///
/// This is deliberately more conservative than a string comparison: it catches
/// lexical aliases (such as `./recording.mp4`), symbolic links, and hard links.
/// The normalized-path comparison remains useful when the output does not exist
/// yet, while `equivalent` supplies the filesystem identity check when it does.
bool paths_alias(const std::filesystem::path& first,
                 const std::filesystem::path& second);

}  // namespace parrot_to_klv
