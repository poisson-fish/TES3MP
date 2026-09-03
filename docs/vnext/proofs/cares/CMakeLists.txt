cmake_minimum_required(VERSION 3.24)

project(vnext_cares_proof LANGUAGES C CXX)

if(NOT VNEXT_CARES_SOURCE_DIR OR NOT EXISTS "${VNEXT_CARES_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "VNEXT_CARES_SOURCE_DIR must name the verified c-ares source tree")
endif()

set(CARES_STATIC ON CACHE BOOL "" FORCE)
set(CARES_SHARED OFF CACHE BOOL "" FORCE)
set(CARES_INSTALL OFF CACHE BOOL "" FORCE)
set(CARES_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CARES_BUILD_CONTAINER_TESTS OFF CACHE BOOL "" FORCE)
set(CARES_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(CARES_COVERAGE OFF CACHE BOOL "" FORCE)
set(CARES_WERROR OFF CACHE BOOL "" FORCE)
if(MSVC)
    set(CARES_MSVC_STATIC_RUNTIME OFF CACHE BOOL "" FORCE)
endif()

add_subdirectory("${VNEXT_CARES_SOURCE_DIR}" cares EXCLUDE_FROM_ALL)

add_executable(vnext_cares_proof proof.cpp)
target_compile_features(vnext_cares_proof PRIVATE cxx_std_20)
target_link_libraries(vnext_cares_proof PRIVATE c-ares::cares_static)

if(MSVC)
    target_compile_options(vnext_cares_proof PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(vnext_cares_proof PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

file(WRITE "${CMAKE_BINARY_DIR}/vnext-toolchain.txt"
    "C_COMPILER=${CMAKE_C_COMPILER}\n"
    "C_COMPILER_ID=${CMAKE_C_COMPILER_ID}\n"
    "C_COMPILER_VERSION=${CMAKE_C_COMPILER_VERSION}\n"
    "CXX_COMPILER=${CMAKE_CXX_COMPILER}\n"
    "CXX_COMPILER_ID=${CMAKE_CXX_COMPILER_ID}\n"
    "CXX_COMPILER_VERSION=${CMAKE_CXX_COMPILER_VERSION}\n"
    "MSVC_RUNTIME=${CMAKE_MSVC_RUNTIME_LIBRARY}\n"
)

enable_testing()
add_test(NAME vnext-cares-proof COMMAND vnext_cares_proof)
set_tests_properties(vnext-cares-proof PROPERTIES TIMEOUT 30)
