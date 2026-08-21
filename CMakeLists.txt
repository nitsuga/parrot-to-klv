cmake_minimum_required(VERSION 3.20)
project(parrot-to-klv VERSION 0.1.6 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Fetch libmisbklv from GitHub, pinned to an exact commit.
#
# Not a branch: FetchContent does not re-fetch an already-populated dependency,
# so tracking `main` lets two checkouts of identical source build against
# different libmisbklv commits depending on when each was first configured.
# Bumping this line is the reviewable record of moving to a new upstream.
#
# To develop both sides at once, point a build directory at a local checkout —
# per build directory, nothing committed:
#   cmake -S . -B build -DFETCHCONTENT_SOURCE_DIR_MISBKLV=~/workspaces/libmisbklv
# and drop the redirect with -UFETCHCONTENT_SOURCE_DIR_MISBKLV. The redirect is
# silent, so verify against the pin before trusting a green suite.
include(FetchContent)
FetchContent_Declare(
  misbklv
  GIT_REPOSITORY git@github.com:nitsuga/libmisbklv.git
  # Live Generate gated on H.264 caps (non-H.264 produces unstamped H.265) — ADR 0013.
  GIT_TAG 4400a2135f6fe7a8ea3826deb874edeef9c2e5cc
)
FetchContent_MakeAvailable(misbklv)

# The project warning baseline applies to our translation units, not warnings
# emitted from libmisbklv's public headers when those units include them.
get_target_property(_misbklv_public_include_directories
  misbklv INTERFACE_INCLUDE_DIRECTORIES)
target_include_directories(misbklv SYSTEM INTERFACE
  ${_misbklv_public_include_directories})
unset(_misbklv_public_include_directories)

# libmisbklv treats its gstreamer backend as optional: if pkg-config cannot find
# the modules it needs, it prints one status line and carries on without
# defining `misbklv::gst`. The mux stage cannot be built without that target, so
# the failure would otherwise surface further down as a bare "target not found",
# which says nothing about the actual cause. CI spent a while red on exactly
# that. Fail here instead, naming the package.
if(NOT TARGET misbklv::gst)
  message(FATAL_ERROR
    "libmisbklv was configured without its gstreamer backend, so misbklv::gst "
    "does not exist and the mux stage cannot link.\n"
    "Look further up this output for 'gstreamer not found' — pkg-config is "
    "missing one of gstreamer-1.0, gstreamer-app-1.0 or "
    "gstreamer-codecparsers-1.0. On Debian/Ubuntu:\n"
    "  sudo apt-get install libgstreamer1.0-dev "
    "libgstreamer-plugins-base1.0-dev libgstreamer-plugins-bad1.0-dev\n"
    "codecparsers ships in the plugins-bad *-dev* package; installing only the "
    "runtime gstreamer1.0-plugins-bad is the usual reason this fires.")
endif()

# Keep the project's warning policy separate from fetched and vendored code.
# Linking this target through parrot_extract reaches every project executable and
# test, while libmisbklv remains outside the policy.
add_library(parrot_project_warnings INTERFACE)
if(MSVC)
  target_compile_options(parrot_project_warnings INTERFACE /W4 /WX)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
  target_compile_options(parrot_project_warnings INTERFACE
    -Wall -Wextra -Wpedantic -Werror)
endif()

# Sanitizers are opt-in so ordinary developer and release builds retain their
# usual runtime and ABI. Keep their policy in a separate interface target for
# the same reason as warnings: instrument project-owned targets without adding
# flags globally to fetched dependency targets.
option(PARROT_SANITIZE
  "Enable AddressSanitizer and UndefinedBehaviorSanitizer for project targets" OFF)
if(PARROT_SANITIZE)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    message(FATAL_ERROR
      "PARROT_SANITIZE requires a GNU, Clang, or AppleClang C++ compiler; "
      "got ${CMAKE_CXX_COMPILER_ID}.")
  endif()

  add_library(parrot_project_sanitizers INTERFACE)
  target_compile_options(parrot_project_sanitizers INTERFACE
    -fsanitize=address,undefined
    -fno-omit-frame-pointer)
  target_link_options(parrot_project_sanitizers INTERFACE
    -fsanitize=address,undefined)
endif()

# Live ingest needs gstreamer-rtp for GstRTPHeaderExtension.
find_package(PkgConfig REQUIRED)
pkg_check_modules(GSTREAMER_RTP REQUIRED gstreamer-rtp-1.0)

add_library(parrot_live STATIC
  src/live/parrot_rtp_header_extension.cpp
  src/live/live_ingest.cpp
)
target_link_libraries(parrot_live PUBLIC parrot_extract misbklv::gst)
target_include_directories(parrot_live PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_include_directories(parrot_live SYSTEM PUBLIC ${GSTREAMER_RTP_INCLUDE_DIRS})
target_link_directories(parrot_live PUBLIC ${GSTREAMER_RTP_LIBRARY_DIRS})
target_link_libraries(parrot_live PUBLIC ${GSTREAMER_RTP_LIBRARIES})
target_compile_options(parrot_live PUBLIC ${GSTREAMER_RTP_CFLAGS_OTHER})

# Project source files
add_executable(parrot-to-klv src/main.cpp)
target_link_libraries(parrot-to-klv PRIVATE parrot_live)

# Turns a real recording into a shareable fixture by replacing its coordinates.
add_executable(parrot-scrub src/scrub_main.cpp)
target_link_libraries(parrot-scrub PRIVATE parrot_extract)

# Gets real-encoder evidence into the repo without the recording.
add_executable(parrot-conform src/conform_main.cpp)
target_link_libraries(parrot-conform PRIVATE parrot_extract)

target_link_libraries(parrot-to-klv PRIVATE parrot_mux)

# So `--version` can answer. Declarative like project(VERSION) itself — the
# release artifacts are named from the git tag, and the release commit bumps
# both together so the binary and the tag agree.
target_compile_definitions(parrot-to-klv
  PRIVATE PARROT_TO_KLV_VERSION="${PROJECT_VERSION}")

# Release bundling (packaging/make-bundle.sh). Off for ordinary builds — this
# only makes sense for the artifact that ships.
#
# The rpath lets the binary find the bundle's own libraries when run directly,
# without the wrapper. libstdc++ and libgcc go in statically because nothing
# else in the process uses them: every bundled library and every GStreamer
# plugin is C, so no C++ ABI crosses the dlopen boundary and there is no second
# runtime to disagree with. That drops ~2 MB and removes the host's libstdc++
# from the compatibility surface entirely.
option(PARROT_BUNDLE "Build the CLI for a relocatable release bundle" OFF)
if(PARROT_BUNDLE)
  set_target_properties(parrot-to-klv PROPERTIES
    BUILD_RPATH "\$ORIGIN/../lib"
    INSTALL_RPATH "\$ORIGIN/../lib")
  target_link_options(parrot-to-klv PRIVATE -static-libstdc++ -static-libgcc)
endif()

# The mux stage, split from parrot_extract because it is the one part that needs
# a media backend: everything else parses and maps with no gstreamer in sight.
add_library(parrot_mux STATIC src/mux.cpp src/error_description.cpp)
target_link_libraries(parrot_mux PUBLIC
  parrot_extract
  misbklv::gst  # libmisbklv with GStreamer support
)
# Live ingest's continuous loop checks the shared interrupt flag set by
# request_mux_interrupt() (SIGINT handling). That flag lives in parrot_mux,
# so parrot_live links it. The link is added here after parrot_mux is defined
# because CMake needs the target to exist.
target_link_libraries(parrot_live PRIVATE parrot_mux)

# Extraction sources, shared by the tool and its tests.
add_library(parrot_extract STATIC
  src/file_identity.cpp
  src/mp4_box_scan.cpp
  src/mp4_metadata_extractor.cpp
  src/parrot_v3.cpp
  src/parrot_proto.cpp
  src/session_metadata.cpp
  src/telemetry.cpp
  src/transform.cpp
  src/parrot_scrub.cpp
  src/conformance.cpp
)

# The transform stage encodes ST 0601 through libmisbklv's registry codec.
target_link_libraries(parrot_extract PUBLIC
  misbklv::misbklv
  parrot_project_warnings
)

if(PARROT_SANITIZE)
  target_link_libraries(parrot_extract PUBLIC parrot_project_sanitizers)
endif()

target_include_directories(parrot_extract PUBLIC ${CMAKE_SOURCE_DIR}/src)

# Vendored headers are consumed by project sources, but are not subject to the
# project's warning baseline.
target_include_directories(parrot_extract SYSTEM
  PUBLIC ${CMAKE_SOURCE_DIR}/external/minimp4  # Vendored single-header MP4 parser
  PRIVATE ${CMAKE_SOURCE_DIR}/external         # Vendored single-header xxHash
)

# Enable testing
enable_testing()

# Fixtures are synthetic and committed (test/make_fixture.py regenerates them);
# real Parrot footage carries live location data and is never committed.
add_executable(mp4_extraction_test test/mp4_extraction_test.cpp)
target_link_libraries(mp4_extraction_test PRIVATE parrot_extract)

add_test(NAME mp4_extraction
         COMMAND mp4_extraction_test ${CMAKE_SOURCE_DIR}/test/fixtures)

add_executable(parrot_v3_test test/parrot_v3_test.cpp)
target_link_libraries(parrot_v3_test PRIVATE parrot_extract)

add_test(NAME parrot_v3
         COMMAND parrot_v3_test ${CMAKE_SOURCE_DIR}/test/fixtures)

add_executable(parrot_proto_test test/parrot_proto_test.cpp)
target_link_libraries(parrot_proto_test PRIVATE parrot_extract)

add_test(NAME parrot_proto
         COMMAND parrot_proto_test ${CMAKE_SOURCE_DIR}/test/fixtures)

add_executable(session_metadata_test test/session_metadata_test.cpp)
target_link_libraries(session_metadata_test PRIVATE parrot_extract)

add_test(NAME session_metadata
         COMMAND session_metadata_test ${CMAKE_SOURCE_DIR}/test/fixtures)

add_executable(transform_test test/transform_test.cpp)
target_link_libraries(transform_test PRIVATE parrot_extract)

add_test(NAME transform COMMAND transform_test)

# The mux stage needs a media backend, so this test needs one too — it converts
# a fixture and reads the result back as a consumer would.
add_executable(mux_test test/mux_test.cpp)
target_link_libraries(mux_test PRIVATE parrot_mux)

add_test(NAME mux COMMAND mux_test ${CMAKE_SOURCE_DIR}/test/fixtures
                           ${CMAKE_CURRENT_BINARY_DIR})

add_executable(parrot_scrub_test test/parrot_scrub_test.cpp)
target_link_libraries(parrot_scrub_test PRIVATE parrot_extract)

add_test(NAME parrot_scrub
         COMMAND parrot_scrub_test ${CMAKE_SOURCE_DIR}/test/fixtures
                                   ${CMAKE_CURRENT_BINARY_DIR})

add_executable(conformance_test test/conformance_test.cpp)
target_link_libraries(conformance_test PRIVATE parrot_extract)

add_test(NAME conformance
         COMMAND conformance_test ${CMAKE_SOURCE_DIR}/test/fixtures)

# Replays the committed conformance reports against whatever real recordings sit
# in data/ (gitignored). Skips cleanly where there is no footage, so it is a
# no-op in CI and on machines that have never held a recording.
# The CLI's argument handling, which now rejects rather than ignores. These are
# one-line ctest entries rather than a test binary because the thing under test
# *is* the process: its exit status and nothing else.
#
# The rejection cases matter most. An unknown flag used to be silently dropped,
# so `--klvv` ran to completion and exited 0 having quietly not done what was
# asked; that is the regression these pin.
add_executable(cli_test test/cli_test.cpp)

add_test(NAME cli_converts_fixture
         COMMAND cli_test $<TARGET_FILE:parrot-to-klv>
                          ${CMAKE_SOURCE_DIR}/test/fixtures
                          ${CMAKE_CURRENT_BINARY_DIR})

add_test(NAME cli_version COMMAND parrot-to-klv --version)
add_test(NAME cli_help COMMAND parrot-to-klv --help)

# The rejection cases matter most. A shell wrapper executes each command and
# verifies BOTH that it exits with a non-zero status AND that it emits the
# expected diagnostic message on stderr.
add_test(NAME cli_rejects_unknown_option
         COMMAND ${CMAKE_SOURCE_DIR}/test/cli_reject_test.sh "unknown option"
                 $<TARGET_FILE:parrot-to-klv> --not-an-option)
add_test(NAME cli_rejects_output_without_path
         COMMAND ${CMAKE_SOURCE_DIR}/test/cli_reject_test.sh "needs a path"
                 $<TARGET_FILE:parrot-to-klv> ${CMAKE_SOURCE_DIR}/test/fixtures/parrot_v3.mp4 -o)
add_test(NAME cli_rejects_two_inputs
         COMMAND ${CMAKE_SOURCE_DIR}/test/cli_reject_test.sh "more than one input file"
                 $<TARGET_FILE:parrot-to-klv> one.mp4 two.mp4)
add_test(NAME cli_rejects_live_flags_removed
         COMMAND ${CMAKE_SOURCE_DIR}/test/cli_reject_test.sh "removed"
                 $<TARGET_FILE:parrot-to-klv> --live-rtsp rtsp://127.0.0.1/live --live-udp udp:239.0.0.1:5000)
add_test(NAME cli_rejects_udp_flag_without_udp
         COMMAND ${CMAKE_SOURCE_DIR}/test/cli_reject_test.sh "only valid with a udp"
                 $<TARGET_FILE:parrot-to-klv> ${CMAKE_SOURCE_DIR}/test/fixtures/parrot_v3.mp4 -o ${CMAKE_CURRENT_BINARY_DIR}/cli_udp_iface_out.ts --iface eth1)
add_test(NAME cli_rejects_ttl_without_udp
         COMMAND ${CMAKE_SOURCE_DIR}/test/cli_reject_test.sh "only valid with a udp"
                 $<TARGET_FILE:parrot-to-klv> ${CMAKE_SOURCE_DIR}/test/fixtures/parrot_v3.mp4 -o ${CMAKE_CURRENT_BINARY_DIR}/cli_udp_ttl_out.ts --ttl 5)
add_test(NAME cli_rejects_loop_without_udp
         COMMAND ${CMAKE_SOURCE_DIR}/test/cli_reject_test.sh "only valid with a udp"
                 $<TARGET_FILE:parrot-to-klv> ${CMAKE_SOURCE_DIR}/test/fixtures/parrot_v3.mp4 -o ${CMAKE_CURRENT_BINARY_DIR}/cli_udp_loop_out.ts --loop)
add_test(NAME cli_rejects_file_to_live_sink
         COMMAND ${CMAKE_SOURCE_DIR}/test/cli_reject_test.sh "not yet supported"
                 $<TARGET_FILE:parrot-to-klv> ${CMAKE_SOURCE_DIR}/test/fixtures/parrot_v3.mp4 -o udp:239.0.0.1:5000)
add_test(NAME cli_rejects_live_without_output
         COMMAND ${CMAKE_SOURCE_DIR}/test/cli_reject_test.sh "live input needs"
                 $<TARGET_FILE:parrot-to-klv> -i rtsp://127.0.0.1/live)
add_test(NAME cli_rejects_udp_ttl_out_of_range
         COMMAND ${CMAKE_SOURCE_DIR}/test/cli_reject_test.sh "must be 0..255"
                 $<TARGET_FILE:parrot-to-klv> -i ${CMAKE_SOURCE_DIR}/test/fixtures/parrot_v3.mp4 -o udp:239.0.0.1:5000 --ttl 300)

# parrot-scrub and parrot-conform's argument handling, same rejection
# discipline and same reasoning as the parrot-to-klv cases above (issue #13):
# a misspelled flag or an out-of-range coordinate used to run to completion
# and exit 0 -- for a privacy tool, that meant the location data silently
# stayed in the output.
add_test(NAME scrub_rejects_unknown_option
         COMMAND ${CMAKE_SOURCE_DIR}/test/cli_reject_test.sh "unknown option"
                 $<TARGET_FILE:parrot-scrub>
                 ${CMAKE_SOURCE_DIR}/test/fixtures/parrot_v3.mp4
                 ${CMAKE_CURRENT_BINARY_DIR}/scrub_cli_reject_out.mp4 --bogus)
add_test(NAME scrub_rejects_nan_lat
         COMMAND ${CMAKE_SOURCE_DIR}/test/cli_reject_test.sh "is not a valid number"
                 $<TARGET_FILE:parrot-scrub> --lat nan
                 ${CMAKE_SOURCE_DIR}/test/fixtures/parrot_v3.mp4
                 ${CMAKE_CURRENT_BINARY_DIR}/scrub_cli_reject_out.mp4)
add_test(NAME scrub_rejects_out_of_range_lat
         COMMAND ${CMAKE_SOURCE_DIR}/test/cli_reject_test.sh "is outside"
                 $<TARGET_FILE:parrot-scrub> --lat 200
                 ${CMAKE_SOURCE_DIR}/test/fixtures/parrot_v3.mp4
                 ${CMAKE_CURRENT_BINARY_DIR}/scrub_cli_reject_out.mp4)
add_test(NAME scrub_rejects_missing_value
         COMMAND ${CMAKE_SOURCE_DIR}/test/cli_reject_test.sh "needs a value"
                 $<TARGET_FILE:parrot-scrub>
                 ${CMAKE_SOURCE_DIR}/test/fixtures/parrot_v3.mp4
                 ${CMAKE_CURRENT_BINARY_DIR}/scrub_cli_reject_out.mp4 --lat)
add_test(NAME scrub_rejects_extra_argument
         COMMAND ${CMAKE_SOURCE_DIR}/test/cli_reject_test.sh "unexpected extra argument"
                 $<TARGET_FILE:parrot-scrub>
                 ${CMAKE_SOURCE_DIR}/test/fixtures/parrot_v3.mp4
                 ${CMAKE_CURRENT_BINARY_DIR}/scrub_cli_reject_out.mp4 extra)

# --bogus after the positional, same reasoning as scrub_rejects_unknown_option
# above: given first, old code took it as `input` and only failed because
# that path did not exist -- not because it detected the unknown flag.
add_test(NAME conform_rejects_unknown_option
         COMMAND ${CMAKE_SOURCE_DIR}/test/cli_reject_test.sh "unknown option"
                 $<TARGET_FILE:parrot-conform>
                 ${CMAKE_SOURCE_DIR}/test/fixtures/parrot_v3.mp4 --bogus)
# A trailing "--label" with nothing after it: old code's `next()` silently
# left `label` empty on a missing value, which reads identically to --label
# never having been passed at all, so single-file mode fell back to its
# default (hash-based) label and still exited 0.
add_test(NAME conform_rejects_missing_value
         COMMAND ${CMAKE_SOURCE_DIR}/test/cli_reject_test.sh "needs a value"
                 $<TARGET_FILE:parrot-conform>
                 ${CMAKE_SOURCE_DIR}/test/fixtures/parrot_v3.mp4 --label)
add_test(NAME conform_rejects_extra_argument
         COMMAND ${CMAKE_SOURCE_DIR}/test/cli_reject_test.sh "unexpected extra argument"
                 $<TARGET_FILE:parrot-conform>
                 ${CMAKE_SOURCE_DIR}/test/fixtures/parrot_v3.mp4 extra)
# Scans test/fixtures/ itself (it holds real applicable .mp4s) rather than a
# nonexistent directory: old code's scan-mode loop hardcoded
# `run_conformance(p.string(), "")` regardless of `--label`, so it needs a
# scan that actually runs and succeeds to show --label being silently
# dropped -- a nonexistent --scan dir "succeeds" at WILL_FAIL for the wrong
# reason (it skips with a nonzero exit before ever reaching that code).
add_test(NAME conform_rejects_label_with_scan
         COMMAND ${CMAKE_SOURCE_DIR}/test/cli_reject_test.sh "--label/--out apply to single-file mode"
                 $<TARGET_FILE:parrot-conform> --scan ${CMAKE_SOURCE_DIR}/test/fixtures
                 --reports ${CMAKE_CURRENT_BINARY_DIR}/conform_cli_reject_reports
                 --label foo)

add_test(NAME conformance_real_data
         COMMAND parrot-conform --scan ${CMAKE_SOURCE_DIR}/data
                                --reports ${CMAKE_SOURCE_DIR}/test/conformance
                                --check)
set_tests_properties(conformance_real_data PROPERTIES SKIP_RETURN_CODE 77)

# Packaging refuses to clear paths that could erase a checkout, an input, or a
# broad system location. The script uses one disposable tree and does not need
# GStreamer, Docker, or a release build to exercise those early guards.
add_test(NAME packaging_output_guard
         COMMAND ${CMAKE_SOURCE_DIR}/test/packaging_output_guard_test.sh
                 ${CMAKE_SOURCE_DIR})

# A recording that vanishes between --scan's directory listing and its
# per-file stat (here, a dangling symlink -- deterministic and privilege-
# independent, unlike a permission-denied directory) must be reported by
# name with the real OS error, not silently absorbed into a bogus
# multi-terabyte "file size" (issue #14). A bare add_test + WILL_FAIL cannot
# catch this: the exit code alone does not discriminate between the old and
# new behavior here, only the message does.
add_test(NAME conform_reports_stat_failure
         COMMAND ${CMAKE_SOURCE_DIR}/test/conform_stat_failure_test.sh
                 $<TARGET_FILE:parrot-conform>
                 ${CMAKE_SOURCE_DIR}/test/fixtures)

add_executable(live_rtp_test test/live_rtp_test.cpp)
target_link_libraries(live_rtp_test PRIVATE parrot_live)
add_test(NAME live_rtp COMMAND live_rtp_test ${CMAKE_SOURCE_DIR}/test/fixtures)

# libmisbklv is a subproject, so `ctest` here runs its suite too — useful
# locally as an early warning when the pin moves, but several of its tests need
# hundreds of megabytes of git-lfs media that this project has no reason to
# fetch. Labeling ours lets CI run `ctest -L parrot` and test this project.
#
# Every add_test above appears here. A new test that does not is simply absent
# from CI, so keep the list in step — that is the one thing this arrangement
# asks for in exchange for not enumerating someone else's tests to exclude.
#
# The two shell-script tests below (packaging_output_guard and
# conformance_real_data) are not compiled by CMake, so ASan/UBSan
# instrumentation buys them nothing; they keep the plain "parrot" label.
set_tests_properties(
  mp4_extraction parrot_v3 parrot_proto session_metadata transform
  parrot_scrub conformance mux cli_converts_fixture
  cli_version cli_help cli_rejects_unknown_option
  cli_rejects_output_without_path cli_rejects_two_inputs
  cli_rejects_live_flags_removed cli_rejects_udp_flag_without_udp
  cli_rejects_ttl_without_udp cli_rejects_loop_without_udp
  cli_rejects_file_to_live_sink cli_rejects_live_without_output
  cli_rejects_udp_ttl_out_of_range
  scrub_rejects_unknown_option scrub_rejects_nan_lat
  scrub_rejects_out_of_range_lat scrub_rejects_missing_value
  scrub_rejects_extra_argument
  conform_rejects_unknown_option conform_rejects_missing_value
  conform_rejects_extra_argument conform_rejects_label_with_scan
  conform_reports_stat_failure live_rtp
  PROPERTIES LABELS "parrot;parrot-sanitize")
set_tests_properties(
  conformance_real_data packaging_output_guard
  PROPERTIES LABELS "parrot")
