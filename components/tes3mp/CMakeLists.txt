if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.16)
    project(tes3mp_standalone LANGUAGES CXX)
endif()

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/TES3MPVerifyTargetBoundaries.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/TES3MPRuntimeSafety.cmake)
include(FetchContent)

tes3mp_validate_runtime_safety()

set(TES3MP_FLATBUFFERS_SOURCE_DIR "" CACHE PATH
    "Optional extracted FlatBuffers 25.12.19 source used instead of the pinned download")
if(TES3MP_FLATBUFFERS_SOURCE_DIR)
    if(NOT EXISTS "${TES3MP_FLATBUFFERS_SOURCE_DIR}/include/flatbuffers/flatbuffers.h")
        message(FATAL_ERROR
            "TES3MP_FLATBUFFERS_SOURCE_DIR does not contain include/flatbuffers/flatbuffers.h")
    endif()
    set(tes3mp_flatbuffers_SOURCE_DIR "${TES3MP_FLATBUFFERS_SOURCE_DIR}")
else()
    if(POLICY CMP0135)
        cmake_policy(SET CMP0135 NEW)
    endif()
    if(POLICY CMP0169)
        cmake_policy(SET CMP0169 OLD)
    endif()
    FetchContent_Declare(tes3mp_flatbuffers
        URL https://github.com/google/flatbuffers/archive/refs/tags/v25.12.19.tar.gz
        URL_HASH SHA256=f81c3162b1046fe8b84b9a0dbdd383e24fdbcf88583b9cb6028f90d04d90696a
        DOWNLOAD_NO_PROGRESS TRUE
    )
    FetchContent_GetProperties(tes3mp_flatbuffers)
    if(NOT tes3mp_flatbuffers_POPULATED)
        FetchContent_Populate(tes3mp_flatbuffers)
    endif()
endif()

set(TES3MP_ENGINE_INDEPENDENT_FORBIDDEN_INCLUDES
    "apps/openmw"
    "mwbase/"
    "mwworld/"
    "mwmechanics/"
    "mwrender/"
    "mwgui/"
    "SDL"
    "osg/"
    "osgViewer/"
    "GameNetworkingSockets"
    "steamnetworking"
)

function(tes3mp_add_engine_independent_library target source)
    add_library(${target} STATIC ${source})
    target_compile_features(${target} PUBLIC cxx_std_20)
    target_include_directories(${target}
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    )
    set_target_properties(${target} PROPERTIES FOLDER "TES3MP")
    tes3mp_enable_runtime_safety(${target})
endfunction()

tes3mp_add_engine_independent_library(tes3mp_protocol protocol/anchor.cpp)
add_library(TES3MP::Protocol ALIAS tes3mp_protocol)
target_sources(tes3mp_protocol PRIVATE
    include/tes3mp/command_primitives.hpp
    include/tes3mp/protocol_handshake.hpp
    include/tes3mp/protocol_frame.hpp
    include/tes3mp/spatial_types.hpp
    include/tes3mp/strong_value.hpp
    include/tes3mp/value_types.hpp
    protocol/generated/client_hello_generated.h
    protocol/generated/server_hello_generated.h
    protocol/generated/session_rejected_generated.h
    protocol/protocol_handshake.cpp
    protocol/protocol_frame.cpp
)
target_include_directories(tes3mp_protocol PRIVATE
    "${tes3mp_flatbuffers_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/protocol"
)

tes3mp_add_engine_independent_library(tes3mp_transport transport/anchor.cpp)
add_library(TES3MP::Transport ALIAS tes3mp_transport)
target_link_libraries(tes3mp_transport PUBLIC tes3mp_protocol)

tes3mp_add_engine_independent_library(tes3mp_server_core server_core/anchor.cpp)
add_library(TES3MP::ServerCore ALIAS tes3mp_server_core)
target_link_libraries(tes3mp_server_core PUBLIC tes3mp_protocol)
target_sources(tes3mp_server_core PRIVATE
    include/tes3mp/deterministic_random.hpp
    include/tes3mp/fixed_tick_scheduler.hpp
    include/tes3mp/monotonic_clock.hpp
    include/tes3mp/observability.hpp
    server_core/deterministic_random.cpp
    server_core/fixed_tick_scheduler.cpp
    server_core/observability.cpp
)

tes3mp_add_engine_independent_library(tes3mp_client_session client_session/anchor.cpp)
add_library(TES3MP::ClientSession ALIAS tes3mp_client_session)
target_link_libraries(tes3mp_client_session
    PUBLIC
        tes3mp_protocol
        tes3mp_transport
)

tes3mp_add_engine_independent_library(tes3mp_test_support test_support/anchor.cpp)
add_library(TES3MP::TestSupport ALIAS tes3mp_test_support)
target_link_libraries(tes3mp_test_support
    PUBLIC
        tes3mp_protocol
        tes3mp_transport
        tes3mp_server_core
        tes3mp_client_session
)
target_sources(tes3mp_test_support PRIVATE
    include/tes3mp/test_support/deterministic_harness.hpp
    include/tes3mp/test_support/fault_injecting_link.hpp
    include/tes3mp/test_support/in_memory_link.hpp
    include/tes3mp/test_support/manual_clock.hpp
    include/tes3mp/test_support/recording_observability.hpp
    include/tes3mp/test_support/spatial_round_trip.hpp
    test_support/deterministic_harness.cpp
    test_support/fault_injecting_link.cpp
    test_support/in_memory_link.cpp
    test_support/manual_clock.cpp
    test_support/recording_observability.cpp
    test_support/spatial_round_trip.cpp
)

tes3mp_verify_target_dependencies(tes3mp_protocol)
tes3mp_verify_target_dependencies(tes3mp_transport ALLOWED tes3mp_protocol)
tes3mp_verify_target_dependencies(tes3mp_server_core ALLOWED tes3mp_protocol)
tes3mp_verify_target_dependencies(tes3mp_client_session
    ALLOWED tes3mp_protocol tes3mp_transport)
tes3mp_verify_target_dependencies(tes3mp_test_support
    ALLOWED
        tes3mp_protocol
        tes3mp_transport
        tes3mp_server_core
        tes3mp_client_session
)

foreach(target
    tes3mp_protocol
    tes3mp_transport
    tes3mp_server_core
    tes3mp_client_session
    tes3mp_test_support
)
    tes3mp_verify_target_includes(${target}
        FORBIDDEN ${TES3MP_ENGINE_INDEPENDENT_FORBIDDEN_INCLUDES})
endforeach()

foreach(target
    tes3mp_protocol
    tes3mp_transport
    tes3mp_server_core
    tes3mp_client_session
)
    tes3mp_verify_target_includes(${target} FORBIDDEN "tes3mp/test_support")
endforeach()

add_executable(tes3mp_protocol_tests tests/strong_value_tests.cpp)
target_compile_features(tes3mp_protocol_tests PRIVATE cxx_std_20)
target_link_libraries(tes3mp_protocol_tests PRIVATE tes3mp_test_support)
set_target_properties(tes3mp_protocol_tests PROPERTIES FOLDER "TES3MP/Tests")
tes3mp_enable_runtime_safety(tes3mp_protocol_tests)

tes3mp_verify_target_dependencies(tes3mp_protocol_tests ALLOWED tes3mp_test_support)
tes3mp_verify_target_includes(tes3mp_protocol_tests
    FORBIDDEN ${TES3MP_ENGINE_INDEPENDENT_FORBIDDEN_INCLUDES})

add_executable(tes3mp_protocol_frame_tests tests/protocol_frame_tests.cpp)
target_compile_features(tes3mp_protocol_frame_tests PRIVATE cxx_std_20)
target_link_libraries(tes3mp_protocol_frame_tests PRIVATE tes3mp_protocol)
set_target_properties(tes3mp_protocol_frame_tests PROPERTIES FOLDER "TES3MP/Tests")
tes3mp_enable_runtime_safety(tes3mp_protocol_frame_tests)

tes3mp_verify_target_dependencies(tes3mp_protocol_frame_tests ALLOWED tes3mp_protocol)
tes3mp_verify_target_includes(tes3mp_protocol_frame_tests
    FORBIDDEN ${TES3MP_ENGINE_INDEPENDENT_FORBIDDEN_INCLUDES})

add_executable(tes3mp_protocol_handshake_tests tests/protocol_handshake_tests.cpp)
target_compile_features(tes3mp_protocol_handshake_tests PRIVATE cxx_std_20)
target_link_libraries(tes3mp_protocol_handshake_tests PRIVATE tes3mp_protocol)
set_target_properties(tes3mp_protocol_handshake_tests PROPERTIES FOLDER "TES3MP/Tests")
tes3mp_enable_runtime_safety(tes3mp_protocol_handshake_tests)

tes3mp_verify_target_dependencies(tes3mp_protocol_handshake_tests ALLOWED tes3mp_protocol)
tes3mp_verify_target_includes(tes3mp_protocol_handshake_tests
    FORBIDDEN ${TES3MP_ENGINE_INDEPENDENT_FORBIDDEN_INCLUDES})

add_executable(tes3mp_spatial_primitive_tests tests/spatial_primitive_tests.cpp)
target_compile_features(tes3mp_spatial_primitive_tests PRIVATE cxx_std_20)
target_link_libraries(tes3mp_spatial_primitive_tests PRIVATE tes3mp_test_support)
set_target_properties(tes3mp_spatial_primitive_tests PROPERTIES FOLDER "TES3MP/Tests")
tes3mp_enable_runtime_safety(tes3mp_spatial_primitive_tests)

tes3mp_verify_target_dependencies(tes3mp_spatial_primitive_tests ALLOWED tes3mp_test_support)
tes3mp_verify_target_includes(tes3mp_spatial_primitive_tests
    FORBIDDEN ${TES3MP_ENGINE_INDEPENDENT_FORBIDDEN_INCLUDES})

add_executable(tes3mp_deterministic_facilities_tests tests/deterministic_facilities_tests.cpp)
target_compile_features(tes3mp_deterministic_facilities_tests PRIVATE cxx_std_20)
target_link_libraries(tes3mp_deterministic_facilities_tests PRIVATE tes3mp_test_support)
set_target_properties(tes3mp_deterministic_facilities_tests PROPERTIES FOLDER "TES3MP/Tests")
tes3mp_enable_runtime_safety(tes3mp_deterministic_facilities_tests)

tes3mp_verify_target_dependencies(tes3mp_deterministic_facilities_tests ALLOWED tes3mp_test_support)
tes3mp_verify_target_includes(tes3mp_deterministic_facilities_tests
    FORBIDDEN ${TES3MP_ENGINE_INDEPENDENT_FORBIDDEN_INCLUDES})

add_executable(tes3mp_deterministic_harness_tests tests/deterministic_harness_tests.cpp)
target_compile_features(tes3mp_deterministic_harness_tests PRIVATE cxx_std_20)
target_link_libraries(tes3mp_deterministic_harness_tests PRIVATE tes3mp_test_support)
set_target_properties(tes3mp_deterministic_harness_tests PROPERTIES FOLDER "TES3MP/Tests")
tes3mp_enable_runtime_safety(tes3mp_deterministic_harness_tests)

tes3mp_verify_target_dependencies(tes3mp_deterministic_harness_tests ALLOWED tes3mp_test_support)
tes3mp_verify_target_includes(tes3mp_deterministic_harness_tests
    FORBIDDEN ${TES3MP_ENGINE_INDEPENDENT_FORBIDDEN_INCLUDES})

add_executable(tes3mp_fault_injection_tests tests/fault_injection_tests.cpp)
target_compile_features(tes3mp_fault_injection_tests PRIVATE cxx_std_20)
target_link_libraries(tes3mp_fault_injection_tests PRIVATE tes3mp_test_support)
set_target_properties(tes3mp_fault_injection_tests PROPERTIES FOLDER "TES3MP/Tests")
tes3mp_enable_runtime_safety(tes3mp_fault_injection_tests)

tes3mp_verify_target_dependencies(tes3mp_fault_injection_tests ALLOWED tes3mp_test_support)
tes3mp_verify_target_includes(tes3mp_fault_injection_tests
    FORBIDDEN ${TES3MP_ENGINE_INDEPENDENT_FORBIDDEN_INCLUDES})

add_executable(tes3mp_observability_tests tests/observability_tests.cpp)
target_compile_features(tes3mp_observability_tests PRIVATE cxx_std_20)
target_link_libraries(tes3mp_observability_tests PRIVATE tes3mp_test_support)
set_target_properties(tes3mp_observability_tests PROPERTIES FOLDER "TES3MP/Tests")
tes3mp_enable_runtime_safety(tes3mp_observability_tests)

tes3mp_verify_target_dependencies(tes3mp_observability_tests ALLOWED tes3mp_test_support)
tes3mp_verify_target_includes(tes3mp_observability_tests
    FORBIDDEN ${TES3MP_ENGINE_INDEPENDENT_FORBIDDEN_INCLUDES})

if(TES3MP_BUILD_FUZZERS)
    add_executable(tes3mp_spatial_round_trip_fuzz tests/fuzz/spatial_round_trip_fuzz.cpp)
    target_compile_features(tes3mp_spatial_round_trip_fuzz PRIVATE cxx_std_20)
    target_link_libraries(tes3mp_spatial_round_trip_fuzz PRIVATE tes3mp_test_support)
    set_target_properties(tes3mp_spatial_round_trip_fuzz PROPERTIES FOLDER "TES3MP/Fuzzers")
    tes3mp_enable_runtime_safety(tes3mp_spatial_round_trip_fuzz)
    tes3mp_enable_libfuzzer(tes3mp_spatial_round_trip_fuzz)
    tes3mp_verify_target_dependencies(tes3mp_spatial_round_trip_fuzz ALLOWED tes3mp_test_support)
    tes3mp_verify_target_includes(tes3mp_spatial_round_trip_fuzz
        FORBIDDEN ${TES3MP_ENGINE_INDEPENDENT_FORBIDDEN_INCLUDES})

    add_executable(tes3mp_protocol_frame_fuzz tests/fuzz/protocol_frame_fuzz.cpp)
    target_compile_features(tes3mp_protocol_frame_fuzz PRIVATE cxx_std_20)
    target_link_libraries(tes3mp_protocol_frame_fuzz PRIVATE tes3mp_protocol)
    set_target_properties(tes3mp_protocol_frame_fuzz PROPERTIES FOLDER "TES3MP/Fuzzers")
    tes3mp_enable_runtime_safety(tes3mp_protocol_frame_fuzz)
    tes3mp_enable_libfuzzer(tes3mp_protocol_frame_fuzz)
    tes3mp_verify_target_dependencies(tes3mp_protocol_frame_fuzz ALLOWED tes3mp_protocol)
    tes3mp_verify_target_includes(tes3mp_protocol_frame_fuzz
        FORBIDDEN ${TES3MP_ENGINE_INDEPENDENT_FORBIDDEN_INCLUDES})

    add_executable(tes3mp_protocol_handshake_fuzz tests/fuzz/protocol_handshake_fuzz.cpp)
    target_compile_features(tes3mp_protocol_handshake_fuzz PRIVATE cxx_std_20)
    target_link_libraries(tes3mp_protocol_handshake_fuzz PRIVATE tes3mp_protocol)
    set_target_properties(tes3mp_protocol_handshake_fuzz PROPERTIES FOLDER "TES3MP/Fuzzers")
    tes3mp_enable_runtime_safety(tes3mp_protocol_handshake_fuzz)
    tes3mp_enable_libfuzzer(tes3mp_protocol_handshake_fuzz)
    tes3mp_verify_target_dependencies(tes3mp_protocol_handshake_fuzz ALLOWED tes3mp_protocol)
    tes3mp_verify_target_includes(tes3mp_protocol_handshake_fuzz
        FORBIDDEN ${TES3MP_ENGINE_INDEPENDENT_FORBIDDEN_INCLUDES})
endif()

add_custom_target(tes3mp_protocol_tests_run
    COMMAND $<TARGET_FILE:tes3mp_protocol_tests>
    COMMAND $<TARGET_FILE:tes3mp_protocol_frame_tests>
    COMMAND $<TARGET_FILE:tes3mp_protocol_handshake_tests>
    COMMAND $<TARGET_FILE:tes3mp_spatial_primitive_tests>
    COMMAND $<TARGET_FILE:tes3mp_deterministic_facilities_tests>
    COMMAND $<TARGET_FILE:tes3mp_deterministic_harness_tests>
    COMMAND $<TARGET_FILE:tes3mp_fault_injection_tests>
    COMMAND $<TARGET_FILE:tes3mp_observability_tests>
    DEPENDS
        tes3mp_protocol_tests
        tes3mp_protocol_frame_tests
        tes3mp_protocol_handshake_tests
        tes3mp_spatial_primitive_tests
        tes3mp_deterministic_facilities_tests
        tes3mp_deterministic_harness_tests
        tes3mp_fault_injection_tests
        tes3mp_observability_tests
    COMMENT "Running TES3MP protocol, deterministic facility, fault, and observability contracts"
)
