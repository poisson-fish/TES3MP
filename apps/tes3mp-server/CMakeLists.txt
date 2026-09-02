include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/TES3MPVerifyTargetBoundaries.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/TES3MPRuntimeSafety.cmake)

add_library(tes3mp_server_app STATIC
    authenticated_join_composition.hpp
    authenticated_join_composition.cpp
    server_config.hpp
    server_config.cpp
    server_application.hpp
    server_application.cpp)
add_library(TES3MP::ServerApp ALIAS tes3mp_server_app)
target_compile_features(tes3mp_server_app PUBLIC cxx_std_20)
target_include_directories(tes3mp_server_app PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
target_link_libraries(tes3mp_server_app PUBLIC tes3mp_protocol tes3mp_transport tes3mp_server_core)
set_target_properties(tes3mp_server_app PROPERTIES FOLDER "TES3MP")
tes3mp_enable_runtime_safety(tes3mp_server_app)

add_executable(tes3mp_server_app_tests server_app_tests.cpp)
target_compile_features(tes3mp_server_app_tests PRIVATE cxx_std_20)
target_link_libraries(tes3mp_server_app_tests PRIVATE tes3mp_server_app)
set_target_properties(tes3mp_server_app_tests PROPERTIES FOLDER "TES3MP/Tests")
tes3mp_enable_runtime_safety(tes3mp_server_app_tests)
add_custom_target(tes3mp_server_app_tests_run
    COMMAND $<TARGET_FILE:tes3mp_server_app_tests>
    DEPENDS tes3mp_server_app_tests
    COMMENT "Running TES3MP dedicated-server app contracts")

if(TES3MP_ENABLE_GNS_TRANSPORT)
    add_executable(tes3mp_server main.cpp)
    target_compile_features(tes3mp_server PRIVATE cxx_std_20)
    target_link_libraries(tes3mp_server PRIVATE tes3mp_server_app tes3mp_transport_gns)
    set_target_properties(tes3mp_server PROPERTIES FOLDER "TES3MP")
    tes3mp_enable_runtime_safety(tes3mp_server)
    tes3mp_verify_target_dependencies(tes3mp_server
        ALLOWED tes3mp_server_app tes3mp_transport_gns)
endif()

tes3mp_verify_target_dependencies(tes3mp_server_app
    ALLOWED tes3mp_protocol tes3mp_transport tes3mp_server_core)
tes3mp_verify_target_dependencies(tes3mp_server_app_tests ALLOWED tes3mp_server_app)
tes3mp_verify_target_includes(tes3mp_server_app FORBIDDEN "apps/openmw" "tes3mp/test_support")
