include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/TES3MPVerifyTargetBoundaries.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/TES3MPRuntimeSafety.cmake)

add_library(tes3mp_server_app STATIC
    actor_content.hpp
    actor_content.cpp
    character_content.hpp
    character_content.cpp
    interactive_object_content.hpp
    interactive_object_content.cpp
    actor_interest_projection.hpp
    actor_interest_projection.cpp
    combat_interest_projection.hpp
    combat_interest_projection.cpp
    combat_content.hpp
    combat_content.cpp
    authenticated_join_composition.hpp
    authenticated_join_composition.cpp
    connection_session_coordinator.hpp
    connection_session_coordinator.cpp
    content_collision.hpp
    content_collision.cpp
    interest_projection.hpp
    interest_projection.cpp
    interactive_object_interest_projection.hpp
    interactive_object_interest_projection.cpp
    inventory_interest_projection.hpp
    inventory_interest_projection.cpp
    inventory_content.hpp
    inventory_content.cpp
    phase7_proof_profile.hpp
    phase7_queue_telemetry.hpp
    phase7_queue_telemetry.cpp
    player_identity_file.hpp
    player_identity_file.cpp
    resume_token_context.hpp
    resume_token_context.cpp
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
target_compile_definitions(tes3mp_server_app_tests PRIVATE TES3MP_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../..")
set_target_properties(tes3mp_server_app_tests PROPERTIES FOLDER "TES3MP/Tests")
tes3mp_enable_runtime_safety(tes3mp_server_app_tests)
add_custom_target(tes3mp_server_app_tests_run
    COMMAND $<TARGET_FILE:tes3mp_server_app_tests>
    COMMAND $<TARGET_FILE:tes3mp_combat_interest_projection_tests>
    DEPENDS tes3mp_server_app_tests tes3mp_combat_interest_projection_tests
    COMMENT "Running TES3MP dedicated-server app contracts")

add_executable(tes3mp_combat_interest_projection_tests combat_interest_projection_tests.cpp)
target_compile_features(tes3mp_combat_interest_projection_tests PRIVATE cxx_std_20)
target_link_libraries(tes3mp_combat_interest_projection_tests PRIVATE tes3mp_server_app)
set_target_properties(tes3mp_combat_interest_projection_tests PROPERTIES FOLDER "TES3MP/Tests")
tes3mp_enable_runtime_safety(tes3mp_combat_interest_projection_tests)
tes3mp_verify_target_dependencies(tes3mp_combat_interest_projection_tests ALLOWED tes3mp_server_app)

add_custom_target(tes3mp_combat_interest_projection_tests_run
    COMMAND $<TARGET_FILE:tes3mp_combat_interest_projection_tests>
    DEPENDS tes3mp_combat_interest_projection_tests
    COMMENT "Running TES3MP combat interest projection contracts")

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
