cmake_minimum_required(VERSION 3.20)
project(parrot-to-klv VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Fetch libmisbklv
include(FetchContent)
FetchContent_Declare(
  misbklv
  GIT_REPOSITORY https://github.com/nitsuga/libmisbklv.git
  GIT_TAG main
)
FetchContent_MakeAvailable(misbklv)

# Project source files (placeholder - to be populated)
add_executable(parrot-to-klv
  src/main.cpp
)

target_include_directories(parrot-to-klv PRIVATE
  ${CMAKE_SOURCE_DIR}/external/minimp4  # Vendored single-header MP4 parser
)

target_link_libraries(parrot-to-klv PRIVATE
  misbklv::gst  # libmisbklv with GStreamer support
)

# Enable testing
enable_testing()
# Tests will be added here
