cmake_minimum_required(VERSION 3.20)
project(parrot-to-klv VERSION 0.1.2 LANGUAGES CXX)

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
  # Hardening/refactor: typed validation, bounded parsing, split GStreamer backend.
  GIT_TAG 1732e0c986e4b252a030147c389585da8248ddb2
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

# Project source files
add_executable(parrot-to-klv src/main.cpp)

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
add_test(NAME cli_version COMMAND parrot-to-klv --version)
add_test(NAME cli_help COMMAND parrot-to-klv --help)
add_test(NAME cli_rejects_unknown_option COMMAND parrot-to-klv --not-an-option)
set_tests_properties(cli_rejects_unknown_option PROPERTIES WILL_FAIL TRUE)
add_test(NAME cli_rejects_output_without_path
         COMMAND parrot-to-klv ${CMAKE_SOURCE_DIR}/test/fixtures/parrot_v3.mp4 -o)
set_tests_properties(cli_rejects_output_without_path PROPERTIES WILL_FAIL TRUE)
add_test(NAME cli_rejects_two_inputs COMMAND parrot-to-klv one.mp4 two.mp4)
set_tests_properties(cli_rejects_two_inputs PROPERTIES WILL_FAIL TRUE)

add_test(NAME conformance_real_data
         COMMAND parrot-conform --scan ${CMAKE_SOURCE_DIR}/data
                                --reports ${CMAKE_SOURCE_DIR}/test/conformance
                                --check)
set_tests_properties(conformance_real_data PROPERTIES SKIP_RETURN_CODE 77)

# libmisbklv is a subproject, so `ctest` here runs its suite too — useful
# locally as an early warning when the pin moves, but several of its tests need
# hundreds of megabytes of git-lfs media that this project has no reason to
# fetch. Labeling ours lets CI run `ctest -L parrot` and test this project.
#
# Every add_test above appears here. A new test that does not is simply absent
# from CI, so keep the list in step — that is the one thing this arrangement
# asks for in exchange for not enumerating someone else's tests to exclude.
set_tests_properties(
  mp4_extraction parrot_v3 parrot_proto session_metadata transform
  parrot_scrub conformance
  PROPERTIES LABELS "parrot;parrot-sanitize")
set_tests_properties(
  mux conformance_real_data
  cli_version cli_help cli_rejects_unknown_option
  cli_rejects_output_without_path cli_rejects_two_inputs
  PROPERTIES LABELS parrot)
