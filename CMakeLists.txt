cmake_minimum_required(VERSION 3.20)
project(parrot-to-klv VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Fetch libmisbklv from GitHub
include(FetchContent)
FetchContent_Declare(
  misbklv
  GIT_REPOSITORY https://github.com/nitsuga/libmisbklv.git
  GIT_TAG main
)
FetchContent_MakeAvailable(misbklv)

# Project source files
add_executable(parrot-to-klv src/main.cpp)

target_link_libraries(parrot-to-klv PRIVATE
  parrot_extract
  misbklv::gst  # libmisbklv with GStreamer support
)

# Extraction sources, shared by the tool and its tests.
add_library(parrot_extract STATIC
  src/mp4_box_scan.cpp
  src/mp4_metadata_extractor.cpp
)

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
