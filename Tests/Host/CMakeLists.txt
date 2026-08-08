cmake_minimum_required(VERSION 3.21)
project(una-sdk-host-tests CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Export compile commands for host tests" FORCE)

if(MSVC)
    add_compile_options(/W4 /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

set(UNA_SDK_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

include(${CMAKE_CURRENT_LIST_DIR}/cmake/FetchGoogleTest.cmake)

# coreJSON backs the SDK JSON reader/writer (Running settings + calibrator store).
set(COREJSON_DIR "${UNA_SDK_ROOT}/ThirdParty/coreJSON/source")
if(NOT IS_DIRECTORY "${COREJSON_DIR}/include")
    message(FATAL_ERROR
        "coreJSON sources not found at ${COREJSON_DIR}. "
        "Initialize submodules: git submodule update --init --recursive"
    )
endif()

add_library(sdk_host_test_support STATIC
    support/KernelTestDoubles.cpp
    "${UNA_SDK_ROOT}/Libs/Source/UnaLogger/Logger.cpp"
)

target_include_directories(sdk_host_test_support PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}/support"
    "${UNA_SDK_ROOT}/Libs/Header"
)

add_executable(una-sdk-host-tests
    apps/Running/Settings_test.cpp
    apps/Running/SettingsSerializer_test.cpp
    calibration/OutdoorStrideCalibrator_test.cpp
    calibration/StrideLut_test.cpp
    calibration/StrideMath_test.cpp
    calibration/CadenceStrideModel_test.cpp
    calibration/TreadmillSpeedEstimator_test.cpp
    fit/FitWriter_test.cpp
    fit/FitProfile_test.cpp
    fit/FitRoundTrip_test.cpp
    fit/RecordingMarker_test.cpp
    apps/Running/ActivityWriter_test.cpp
    sensorlayer/HeartRateParser_test.cpp
    sensorlayer/HeartRateExParser_test.cpp
    simulator/FileSystemMock_test.cpp
    sensorlayer/GyroscopeParser_test.cpp
    sensorlayer/SensorConnection_test.cpp
    metrics/MonotonicCounter_test.cpp
    metrics/SpeedSmoother_test.cpp
    metrics/VariableCounter_test.cpp
    utils/ClockTime_test.cpp
    utils/Utils_test.cpp
    utils/CopyUtf8_test.cpp
    support/KernelTestDoubles_test.cpp
    apps/Stopwatch/Stopwatch_test.cpp
    apps/Timer/TimerManager_test.cpp
    apps/Timer/Commands_test.cpp
    "${UNA_SDK_ROOT}/Examples/Apps/Timer/Software/Libs/Sources/TimerManager.cpp"
    "${UNA_SDK_ROOT}/Examples/Apps/Running/Software/Libs/Sources/SettingsSerializer.cpp"
    "${UNA_SDK_ROOT}/Examples/Apps/Running/Software/Libs/Sources/ActivityWriter.cpp"
    "${UNA_SDK_ROOT}/Libs/Source/Calibration/OutdoorStrideCalibrator.cpp"
    "${UNA_SDK_ROOT}/Libs/Source/Calibration/StrideLut.cpp"
    "${UNA_SDK_ROOT}/Libs/Source/Calibration/CadenceStrideModel.cpp"
    "${UNA_SDK_ROOT}/Libs/Source/Calibration/TreadmillSpeedEstimator.cpp"
    "${UNA_SDK_ROOT}/Libs/Source/Fit/FitCrc.cpp"
    "${UNA_SDK_ROOT}/Libs/Source/Fit/FitWriter.cpp"
    "${UNA_SDK_ROOT}/Libs/Source/Fit/FitRecordCadence.cpp"
    "${UNA_SDK_ROOT}/Libs/Source/Fit/RecordingMarker.cpp"
    "${UNA_SDK_ROOT}/Libs/Source/SensorLayer/SensorConnection.cpp"
    "${UNA_SDK_ROOT}/Libs/Source/JSON/JsonStreamReader.cpp"
    "${UNA_SDK_ROOT}/Libs/Source/JSON/JsonStreamWriter.cpp"
    "${COREJSON_DIR}/core_json.c"
    "${UNA_SDK_ROOT}/Libs/Source/Simulator/Kernel/Mock/FileSystem.cpp"
    "${UNA_SDK_ROOT}/Libs/Source/Wrappers/StdLibWrappers.c"
)
# FIT encoding is covered by the SDK::Fit tests (fit/*) and the Running
# ActivityWriter smoke test (apps/Running/ActivityWriter_test.cpp).

target_include_directories(una-sdk-host-tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}"
    "${UNA_SDK_ROOT}/Libs/Header"
    "${COREJSON_DIR}/include"
    "${UNA_SDK_ROOT}/Examples/Apps/Running/Software/Libs/Header"
    "${UNA_SDK_ROOT}/Examples/Apps/Stopwatch/Software/Libs/Header"
    "${UNA_SDK_ROOT}/Examples/Apps/Timer/Software/Libs/Header"
)

target_link_libraries(una-sdk-host-tests PRIVATE
    sdk_host_test_support
    GTest::gtest_main
    GTest::gmock
)

# AthensRun is a standalone copy of the Running app (PoC map screen), so its
# service classes collide with Running's at link time -- second executable,
# same pattern as the GpsLab branch. Only the app's pure map math was tested
# here originally; the vendored rawtiles Container carries its own
# conformance evidence on the branch it was vendored from -- this session's
# additive, local-only skipCrcVerify/declaredCrc32 surface (see the
# Background CRC verification design doc) is NOT part of that upstream
# conformance evidence, so it gets its own narrow test here instead.
add_executable(una-sdk-athensrun-tests
    apps/AthensRun/MapMath_test.cpp
    apps/AthensRun/TraceBuffer_test.cpp
    apps/AthensRun/ContainerCrcSkip_test.cpp
    "${UNA_SDK_ROOT}/Examples/Apps/AthensRun/Software/Apps/TouchGFX-GUI/gui/src/rawtiles/Container.cpp"
)

target_include_directories(una-sdk-athensrun-tests PRIVATE
    "${UNA_SDK_ROOT}/Examples/Apps/AthensRun/Software/Apps/TouchGFX-GUI/gui/include"
    "${UNA_SDK_ROOT}/Examples/Apps/AthensRun/Software/Libs/Header"
)

# sdk_host_test_support (InMemoryFileSystem / KernelFixture, from
# support/KernelTestDoubles.hpp) is newly linked into this target here --
# previously only una-sdk-host-tests used it. This target's own Service-class
# collision with Running (see comment above) is between the two apps' own
# Sources, not with this shared test-support lib, so it should be safe; if a
# future addition to this target reintroduces a real collision, split this
# back out into its own executable rather than fighting the shared lib.
target_link_libraries(una-sdk-athensrun-tests PRIVATE
    sdk_host_test_support
    GTest::gtest_main
)

enable_testing()
add_test(NAME una-sdk-host-tests COMMAND una-sdk-host-tests)
add_test(NAME una-sdk-athensrun-tests COMMAND una-sdk-athensrun-tests)
