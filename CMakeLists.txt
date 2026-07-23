cmake_minimum_required(VERSION 3.20)
project(parrot-to-klv VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Find libmisbklv (either installed or via CMAKE_PREFIX_PATH)
# For development, point to local clone:
#   cmake -DCMAKE_PREFIX_PATH=/home/schneiderer/workspaces/github/libmisbklv/build ...
# Or use FetchContent when authenticated:
option(USE_LOCAL_MISBKLV "Use local libmisbklv instead of FetchContent" ON)

if(USE_LOCAL_MISBKLV)
  # Use locally installed libmisbklv
  set(CMAKE_PREFIX_PATH "/tmp/misbklv-install" ${CMAKE_PREFIX_PATH})
  find_package(misbklv REQUIRED COMPONENTS gst)
else()
  # Fetch from GitHub (requires authentication)
  include(FetchContent)
  FetchContent_Declare(
    misbklv
    GIT_REPOSITORY https://github.com/nitsuga/libmisbklv.git
    GIT_TAG main
  )
  FetchContent_MakeAvailable(misbklv)
endif()

# Project source files
add_executable(parrot-to-klv
  src/main.cpp
  src/mp4_metadata_extractor.cpp
)

target_include_directories(parrot-to-klv PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/external/minimp4  # Vendored single-header MP4 parser
)

target_link_libraries(parrot-to-klv PRIVATE
  misbklv::gst  # libmisbklv with GStreamer support
)

# Enable testing
enable_testing()
# Tests will be added here
