include(FetchContent)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.7.1
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(Catch2)

find_package(Threads REQUIRED)

add_executable(SmartMaskRegistryTests SmartMaskRegistryTests.cpp)
target_link_libraries(SmartMaskRegistryTests PRIVATE
    SmartMaskCore
    Catch2::Catch2WithMain
    Threads::Threads
)

add_executable(SpectralEngineTests SpectralEngineTests.cpp)
target_link_libraries(SpectralEngineTests PRIVATE
    SmartMaskCore
    Catch2::Catch2WithMain
)

add_executable(MaskingProcessorTests MaskingProcessorTests.cpp)
target_link_libraries(MaskingProcessorTests PRIVATE
    SmartMaskCore
    Catch2::Catch2WithMain
)

add_executable(PluginProcessorTests PluginProcessorTests.cpp)
target_link_libraries(PluginProcessorTests PRIVATE
    SmartMaskCore
    Catch2::Catch2WithMain
)

add_executable(LoadTests LoadTests.cpp)
target_link_libraries(LoadTests PRIVATE
    SmartMaskCore
    Catch2::Catch2WithMain
)
# Meaningful CPU% numbers require the whole project (including SmartMaskCore,
# where the actual DSP hot path lives) configured with
# -DCMAKE_BUILD_TYPE=Release -- an unoptimized build measures debug-build
# overhead, not what a real shipped plugin binary costs.
if(NOT CMAKE_BUILD_TYPE STREQUAL "Release" AND NOT CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    message(WARNING
        "LoadTests (Phase 6 CPU budget test) is most meaningful with "
        "-DCMAKE_BUILD_TYPE=Release; current build type is "
        "'${CMAKE_BUILD_TYPE}', so its measured CPU% will not represent a "
        "real shipped plugin binary.")
endif()

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(SmartMaskRegistryTests)
catch_discover_tests(SpectralEngineTests)
catch_discover_tests(MaskingProcessorTests)
catch_discover_tests(PluginProcessorTests)
catch_discover_tests(LoadTests)
