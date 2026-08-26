cmake_minimum_required(VERSION 3.16)

project(vnext_flatbuffers_selection_proof LANGUAGES CXX)

option(VNEXT_FLATBUFFERS_BUILD_FUZZER "Build the Clang sanitizer-backed selection-proof fuzzer" OFF)

if(NOT DEFINED FLATBUFFERS_SOURCE_DIR)
    message(FATAL_ERROR "FLATBUFFERS_SOURCE_DIR must name the extracted pinned FlatBuffers source")
endif()

add_executable(vnext-flatbuffers-proof proof.cpp proof_codec.cpp)
target_compile_features(vnext-flatbuffers-proof PRIVATE cxx_std_20)
target_include_directories(
    vnext-flatbuffers-proof
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/generated"
        "${FLATBUFFERS_SOURCE_DIR}/include"
)

if(MSVC)
    target_compile_options(vnext-flatbuffers-proof PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(vnext-flatbuffers-proof PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

enable_testing()
add_test(NAME vnext-flatbuffers-proof COMMAND vnext-flatbuffers-proof)

if(VNEXT_FLATBUFFERS_BUILD_FUZZER)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "VNEXT_FLATBUFFERS_BUILD_FUZZER requires Clang with libFuzzer")
    endif()
    add_executable(vnext-flatbuffers-fuzz fuzz.cpp proof_codec.cpp)
    target_compile_features(vnext-flatbuffers-fuzz PRIVATE cxx_std_20)
    target_include_directories(
        vnext-flatbuffers-fuzz
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/generated"
            "${FLATBUFFERS_SOURCE_DIR}/include"
    )
    target_compile_options(
        vnext-flatbuffers-fuzz
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
            -fno-omit-frame-pointer
            -fsanitize=fuzzer,address,undefined
    )
    target_link_options(vnext-flatbuffers-fuzz PRIVATE -fsanitize=fuzzer,address,undefined)
endif()
