cmake_minimum_required(VERSION 3.24)

project(SmartMaskNetwork VERSION 0.13.0 LANGUAGES C CXX)

# The project's own folder ("Gabci's Smart Mask Network") contains an
# apostrophe. CMake's escaping for the raw shell command chains it emits for
# custom POST_BUILD steps backslash-escapes spaces but does NOT escape
# apostrophes, so if the *build directory* lives under this folder, JUCE's
# VST3 manifest-generation post-build step breaks with a cryptic
# "/bin/sh: unexpected EOF while looking for matching `'" error -- this cost
# a couple of hours to diagnose in Phase 4 (see CHANGELOG 0.4.0). It happens
# with both the Unix Makefiles and Ninja generators (it's a CMake path
# -escaping issue, not a generator quirk). Fail fast instead of repeating
# that: configure the build directory OUTSIDE this folder, e.g.
#   cmake -S . -B /somewhere/without/an/apostrophe/build -G Ninja
if(CMAKE_BINARY_DIR MATCHES "'")
    message(FATAL_ERROR
        "The build directory path contains an apostrophe (inherited from "
        "this project folder's name). This breaks JUCE's VST3 post-build "
        "manifest step -- see the comment right above this check in "
        "CMakeLists.txt. Configure the build directory somewhere else, e.g.:\n"
        "  cmake -S . -B /somewhere/without/an/apostrophe/build -G Ninja")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# JUCE 9.0.0: the spec's document (written before JUCE 9 shipped) asked for
# JUCE 8, but the user asked to track the latest release instead, which at
# the time of writing is 9.0.0 -- pin it explicitly rather than floating on
# a branch, so the build stays reproducible.
include(FetchContent)

FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 9.0.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(JUCE)

add_subdirectory(Source)

option(SMARTMASK_BUILD_TESTS "Build the Catch2 unit tests" ON)
if(SMARTMASK_BUILD_TESTS)
    enable_testing()
    add_subdirectory(Tests)
endif()
