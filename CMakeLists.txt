cmake_minimum_required(VERSION 3.20)
project(parrot-to-klv VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Fetch libmisbklv from GitHub
include(FetchContent)
FetchContent_Declare(
  misbklv
  GIT_REPOSITORY git@github.com:nitsuga/libmisbklv.git
  GIT_TAG main
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

target_link_libraries(parrot-to-klv PRIVATE
  parrot_extract
  misbklv::gst  # libmisbklv with GStreamer support
)

# Extraction sources, shared by the tool and its tests.
add_library(parrot_extract STATIC
  src/mp4_box_scan.cpp
  src/mp4_metadata_extractor.cpp
  src/parrot_v3.cpp
  src/parrot_proto.cpp
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

add_executable(transform_test test/transform_test.cpp)
target_link_libraries(transform_test PRIVATE parrot_extract)

add_test(NAME transform COMMAND transform_test)

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
