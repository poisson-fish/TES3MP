# Temporary M1 link: openmw-lib brings rendering/audio/UI libraries into the
# dependency graph, but the loader does not initialize those services. Replace
# this edge with extracted engine content/runtime libraries before production
# headless packaging. Do not relax components/tes3mp's independent boundaries.
add_library(tes3mp_native_equipment_runtime STATIC
    equipment_runtime.cpp equipment_runtime.hpp equipment_command.hpp inventory_identity.hpp actor_inventory.hpp
    connected_runtime.cpp world_items.cpp door_runtime.cpp ordinary_door.cpp ordinary_door.hpp door_codec.cpp door_codec.hpp equipment_session.cpp equipment_session.hpp inventory_transfer_command.cpp inventory_transfer_command.hpp
    equipment_codec.cpp equipment_codec.hpp equipment_file.cpp equipment_file.hpp
    bounded_file.cpp bounded_file.hpp persistence.hpp runtime_phases.hpp session_commit.hpp)
target_compile_features(tes3mp_native_equipment_runtime PUBLIC cxx_std_20)
target_link_libraries(tes3mp_native_equipment_runtime PRIVATE openmw-lib)
tes3mp_verify_target_dependencies(tes3mp_native_equipment_runtime ALLOWED openmw-lib)
tes3mp_verify_target_includes(tes3mp_native_equipment_runtime
    FORBIDDEN "components/tes3mp" "apps/openmw/mwbase" "apps/openmw/mwgui" "apps/openmw/mwrender"
        "test_allocations.hpp" "equipment_tests.hpp" "transfer_rehearsal.hpp" "transfer_file_sink.hpp")

add_library(tes3mp_native_loadout STATIC loadout.cpp loadout.hpp cell_selection.hpp diagnostic.hpp inventory.cpp enchantment.cpp equipment_probe.cpp placed_containers.cpp placed_door.cpp)
target_compile_features(tes3mp_native_loadout PUBLIC cxx_std_20)
target_link_libraries(tes3mp_native_loadout PRIVATE tes3mp_native_equipment_runtime openmw-lib Boost::program_options)
tes3mp_verify_target_dependencies(tes3mp_native_loadout ALLOWED tes3mp_native_equipment_runtime openmw-lib Boost::program_options)
tes3mp_verify_target_includes(tes3mp_native_loadout
    FORBIDDEN "components/tes3mp" "apps/openmw/mwbase" "apps/openmw/mwgui" "apps/openmw/mwrender")

file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/loadout-dependencies-$<CONFIG>.txt"
    CONTENT "Probe direct: tes3mp_native_loadout\nLoadout direct: $<TARGET_PROPERTY:tes3mp_native_loadout,LINK_LIBRARIES>\nEquipment runtime direct: $<TARGET_PROPERTY:tes3mp_native_equipment_runtime,LINK_LIBRARIES>\nOpenMW direct: $<TARGET_PROPERTY:openmw-lib,LINK_LIBRARIES>\nComponents direct: $<TARGET_PROPERTY:components,LINK_LIBRARIES>\nInitialized: ConfigurationManager, Utf8Encoder, ReadersCache, EsmLoader, ESMStore\nInventory mode additionally initializes: engine classes, WorldModel (one-entry cache), ManualRef, base ContainerStore, CompilerContext/extensions, ScriptManager (declarations only), LocalScripts\nEquipment mode additionally initializes: EquipmentRuntime, two actor InventoryStores, owned effects, plain-shirt transfer/drop/take and coherent session persistence/restart with an optional shared base ContainerStore; NPC stats and declaration locals when requested\nNot constructed: World, Environment, renderer, UI, audio, Lua runtime\nNot exercised: script instructions, live-world compiler queries, global script startup\n")

# Read-only stock OpenMW scene queries. No graphics context or gameplay writer.
add_library(tes3mp_native_placement_scene STATIC placement_scene.cpp placement_scene.hpp)
target_compile_features(tes3mp_native_placement_scene PUBLIC cxx_std_20)
target_link_libraries(tes3mp_native_placement_scene PRIVATE tes3mp_native_loadout tes3mp_server_app openmw-lib)
tes3mp_verify_target_dependencies(tes3mp_native_placement_scene
    ALLOWED tes3mp_native_loadout tes3mp_server_app openmw-lib)
tes3mp_verify_target_includes(tes3mp_native_placement_scene
    FORBIDDEN "apps/openmw/mwbase" "apps/openmw/mwgui" "apps/openmw/mwphysics")

# Concrete application adapter; engine/network types meet only in this leaf.
add_library(tes3mp_native_inventory_service STATIC inventory_service.cpp inventory_service.hpp area_doors.cpp)
target_compile_features(tes3mp_native_inventory_service PUBLIC cxx_std_20)
target_link_libraries(tes3mp_native_inventory_service PUBLIC tes3mp_server_app PRIVATE tes3mp_native_equipment_runtime openmw-lib)
tes3mp_verify_target_dependencies(tes3mp_native_inventory_service
    ALLOWED tes3mp_native_equipment_runtime tes3mp_server_app openmw-lib)
tes3mp_verify_target_includes(tes3mp_native_inventory_service
    FORBIDDEN "apps/openmw/mwbase" "apps/openmw/mwgui" "apps/openmw/mwrender" "transfer_rehearsal.hpp" "equipment_tests.hpp")

add_library(tes3mp_native_inventory_host STATIC inventory_host.cpp inventory_host.hpp environment.cpp environment.hpp)
target_compile_features(tes3mp_native_inventory_host PUBLIC cxx_std_20)
target_link_libraries(tes3mp_native_inventory_host PUBLIC tes3mp_native_inventory_service
    PRIVATE tes3mp_native_loadout tes3mp_native_placement_scene openmw-lib)
tes3mp_verify_target_dependencies(tes3mp_native_inventory_host
    ALLOWED tes3mp_native_inventory_service tes3mp_native_loadout tes3mp_native_placement_scene openmw-lib)
tes3mp_verify_target_includes(tes3mp_native_inventory_host
    FORBIDDEN "apps/openmw/mwbase" "apps/openmw/mwgui" "apps/openmw/mwrender" "transfer_rehearsal.hpp" "equipment_tests.hpp")

add_executable(tes3mp_native_loadout_probe main.cpp)
add_executable(tes3mp_native_bootstrap bootstrap.cpp)
target_link_libraries(tes3mp_native_bootstrap PRIVATE tes3mp_native_loadout tes3mp_server_core)
tes3mp_verify_target_dependencies(tes3mp_native_bootstrap ALLOWED tes3mp_native_loadout tes3mp_server_core)
target_link_libraries(tes3mp_native_loadout_probe PRIVATE tes3mp_native_loadout)
tes3mp_verify_target_dependencies(tes3mp_native_loadout_probe ALLOWED tes3mp_native_loadout)

# Detached M4 interior collision binding; no network or live inventory writer.
add_library(tes3mp_native_actor_runtime STATIC actor_scene.cpp actor_scene.hpp)
target_compile_features(tes3mp_native_actor_runtime PUBLIC cxx_std_20)
target_link_libraries(tes3mp_native_actor_runtime PRIVATE tes3mp_native_loadout openmw-lib)
tes3mp_verify_target_dependencies(tes3mp_native_actor_runtime ALLOWED tes3mp_native_loadout openmw-lib)
tes3mp_verify_target_includes(tes3mp_native_actor_runtime
    FORBIDDEN "components/tes3mp" "apps/openmw/mwbase" "apps/openmw/mwgui" "apps/openmw/mwrender")
add_executable(tes3mp_native_actor_probe actor_probe.cpp)
target_link_libraries(tes3mp_native_actor_probe PRIVATE tes3mp_native_actor_runtime tes3mp_native_loadout)
tes3mp_verify_target_dependencies(tes3mp_native_actor_probe ALLOWED tes3mp_native_actor_runtime tes3mp_native_loadout)

# Standalone, filtered synthetic checks, not the upstream openmw-tests target.
# M4 movement seam only: the stock solver is linked, without constructing its
# World/renderer/player services. This does not yet install an NPC authority.
add_executable(tes3mp_native_actor_tests actor_physics_tests.cpp)
target_compile_features(tes3mp_native_actor_tests PRIVATE cxx_std_20)
target_link_libraries(tes3mp_native_actor_tests PRIVATE openmw-lib)
tes3mp_verify_target_dependencies(tes3mp_native_actor_tests ALLOWED openmw-lib)
tes3mp_verify_target_includes(tes3mp_native_actor_tests
    FORBIDDEN "components/tes3mp" "apps/openmw/mwbase" "apps/openmw/mwgui" "apps/openmw/mwrender")
set_target_properties(tes3mp_native_actor_tests PROPERTIES FOLDER "TES3MP/NativeProbe")

add_executable(tes3mp_native_loadout_tests loadout_tests.cpp environment_tests.cpp environment_tests.hpp
    ../server_app_tests.cpp
    inventory_service_tests.cpp inventory_service_tests.hpp
    placed_container_tests.cpp placement_tests.cpp door_tests.cpp door_tests.hpp
    equipment_tests.cpp equipment_tests.hpp equipment_command.hpp
    transfer_rehearsal.cpp transfer_rehearsal.hpp transfer_rehearsal_tests.cpp
    transfer_save_codec.cpp transfer_save_codec.hpp
    transfer_restart.cpp
    transfer_file_sink.cpp transfer_file_sink.hpp
    transfer_rehearsal_command.cpp
    inventory_view.cpp inventory_view.hpp
    test_allocations.cpp test_allocations.hpp)
target_link_libraries(tes3mp_native_loadout_tests PRIVATE tes3mp_native_loadout tes3mp_native_inventory_host tes3mp_native_inventory_service tes3mp_client_session)
target_compile_definitions(tes3mp_native_loadout_tests PRIVATE TES3MP_NATIVE_INVENTORY_SCENARIO
    TES3MP_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../../..")
tes3mp_verify_target_dependencies(tes3mp_native_loadout_tests ALLOWED tes3mp_native_loadout tes3mp_native_inventory_host tes3mp_native_inventory_service tes3mp_client_session)
set_target_properties(tes3mp_native_equipment_runtime tes3mp_native_loadout tes3mp_native_loadout_probe tes3mp_native_loadout_tests
    PROPERTIES FOLDER "TES3MP/NativeProbe")
