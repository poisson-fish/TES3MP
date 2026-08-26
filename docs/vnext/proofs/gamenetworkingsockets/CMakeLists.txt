cmake_minimum_required(VERSION 3.24)

project(vnext_gamenetworkingsockets_proof LANGUAGES C CXX)

if(NOT VNEXT_GNS_SOURCE_DIR OR NOT EXISTS "${VNEXT_GNS_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "VNEXT_GNS_SOURCE_DIR must name the verified GameNetworkingSockets source tree")
endif()

set(BUILD_SHARED_LIB OFF CACHE BOOL "" FORCE)
set(BUILD_STATIC_LIB ON CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(ENABLE_ICE OFF CACHE BOOL "" FORCE)
set(USE_STEAMWEBRTC OFF CACHE BOOL "" FORCE)
set(USE_CRYPTO OpenSSL CACHE STRING "" FORCE)
set(Protobuf_USE_STATIC_LIBS ON CACHE BOOL "" FORCE)
set(OPENSSL_USE_STATIC_LIBS ON CACHE BOOL "" FORCE)

add_subdirectory("${VNEXT_GNS_SOURCE_DIR}" gns EXCLUDE_FROM_ALL)

add_executable(vnext_gamenetworkingsockets_proof proof.cpp)
target_compile_features(vnext_gamenetworkingsockets_proof PRIVATE cxx_std_20)
target_compile_definitions(vnext_gamenetworkingsockets_proof PRIVATE
    STEAMNETWORKINGSOCKETS_OPENSOURCE
)
target_link_libraries(vnext_gamenetworkingsockets_proof PRIVATE
    GameNetworkingSockets::static
)
add_sanitizers(vnext_gamenetworkingsockets_proof)

if(MSVC)
    target_compile_options(vnext_gamenetworkingsockets_proof PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(vnext_gamenetworkingsockets_proof PRIVATE
        -Wall -Wextra -Wpedantic -Werror
    )
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
add_test(NAME vnext-gamenetworkingsockets-proof COMMAND vnext_gamenetworkingsockets_proof)
set_tests_properties(vnext-gamenetworkingsockets-proof PROPERTIES TIMEOUT 45)
