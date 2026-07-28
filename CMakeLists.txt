cmake_minimum_required(VERSION 3.20)
project(parrot-to-klv VERSION 0.1.0 LANGUAGES CXX)

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
  GIT_TAG b4544157240aea996b16f161d3e0c9e3f5e61420  # KLV pipeline fixes
)
FetchContent_MakeAvailable(misbklv)

# Project source files
add_executable(parrot-to-klv src/main.cpp)

# Turns a real recording into a shareable fixture by replacing its coordinates.
add_executable(parrot-scrub src/scrub_main.cpp)
target_link_libraries(parrot-scrub PRIVATE parrot_extract)

# Gets real-encoder evidence into the repo without the recording.
add_executable(parrot-conform src/conform_main.cpp)
target_link_libraries(parrot-conform PRIVATE parrot_extract)

target_link_libraries(parrot-to-klv PRIVATE parrot_mux)

# The mux stage, split from parrot_extract because it is the one part that needs
# a media backend: everything else parses and maps with no gstreamer in sight.
add_library(parrot_mux STATIC src/mux.cpp)
target_link_libraries(parrot_mux PUBLIC
  parrot_extract
  misbklv::gst  # libmisbklv with GStreamer support
)

# Extraction sources, shared by the tool and its tests.
add_library(parrot_extract STATIC
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
target_link_libraries(parrot_extract PUBLIC misbklv::misbklv)

target_include_directories(parrot_extract PUBLIC
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/external/minimp4  # Vendored single-header MP4 parser
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
add_test(NAME conformance_real_data
         COMMAND parrot-conform --scan ${CMAKE_SOURCE_DIR}/data
                                --reports ${CMAKE_SOURCE_DIR}/test/conformance
                                --check)
set_tests_properties(conformance_real_data PROPERTIES SKIP_RETURN_CODE 77)

# libmisbklv is a subproject, so `ctest` here runs its suite too — useful
# locally as an early warning when the pin moves, but several of its tests need
# hundreds of megabytes of git-lfs media that this project has no reason to
# fetch. Labelling ours lets CI run `ctest -L parrot` and test this project.
#
# Every add_test above appears here. A new test that does not is simply absent
# from CI, so keep the list in step — that is the one thing this arrangement
# asks for in exchange for not enumerating someone else's tests to exclude.
set_tests_properties(
  mp4_extraction parrot_v3 parrot_proto session_metadata transform mux
  parrot_scrub conformance conformance_real_data
  PROPERTIES LABELS parrot)
