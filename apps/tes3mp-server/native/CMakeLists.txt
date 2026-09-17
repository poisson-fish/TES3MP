# Temporary M1 link: openmw-lib brings rendering/audio/UI libraries into the
# dependency graph, but the loader does not initialize those services. Replace
# this edge with extracted engine content/runtime libraries before production
# headless packaging. Do not relax components/tes3mp's independent boundaries.
add_library(tes3mp_native_equipment_runtime STATIC
    equipment_runtime.cpp equipment_runtime.hpp equipment_command.hpp inventory_identity.hpp
    connected_runtime.cpp equipment_session.cpp equipment_session.hpp inventory_transfer_command.cpp inventory_transfer_command.hpp
    equipment_codec.cpp equipment_codec.hpp equipment_file.cpp equipment_file.hpp
    bounded_file.cpp bounded_file.hpp persistence.hpp runtime_phases.hpp session_commit.hpp)
target_compile_features(tes3mp_native_equipment_runtime PUBLIC cxx_std_20)
target_link_libraries(tes3mp_native_equipment_runtime PRIVATE openmw-lib)
tes3mp_verify_target_dependencies(tes3mp_native_equipment_runtime ALLOWED openmw-lib)
tes3mp_verify_target_includes(tes3mp_native_equipment_runtime
    FORBIDDEN "components/tes3mp" "apps/openmw/mwbase" "apps/openmw/mwgui" "apps/openmw/mwrender"
        "test_allocations.hpp" "equipment_tests.hpp" "transfer_rehearsal.hpp" "transfer_file_sink.hpp")

add_library(tes3mp_native_loadout STATIC loadout.cpp loadout.hpp diagnostic.hpp inventory.cpp enchantment.cpp equipment_probe.cpp)
target_compile_features(tes3mp_native_loadout PUBLIC cxx_std_20)
target_link_libraries(tes3mp_native_loadout PRIVATE tes3mp_native_equipment_runtime openmw-lib Boost::program_options)
tes3mp_verify_target_dependencies(tes3mp_native_loadout ALLOWED tes3mp_native_equipment_runtime openmw-lib Boost::program_options)
tes3mp_verify_target_includes(tes3mp_native_loadout
    FORBIDDEN "components/tes3mp" "apps/openmw/mwbase" "apps/openmw/mwgui" "apps/openmw/mwrender")

file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/loadout-dependencies-$<CONFIG>.txt"
    CONTENT "Probe direct: tes3mp_native_loadout\nLoadout direct: $<TARGET_PROPERTY:tes3mp_native_loadout,LINK_LIBRARIES>\nEquipment runtime direct: $<TARGET_PROPERTY:tes3mp_native_equipment_runtime,LINK_LIBRARIES>\nOpenMW direct: $<TARGET_PROPERTY:openmw-lib,LINK_LIBRARIES>\nComponents direct: $<TARGET_PROPERTY:components,LINK_LIBRARIES>\nInitialized: ConfigurationManager, Utf8Encoder, ReadersCache, EsmLoader, ESMStore\nInventory mode additionally initializes: engine classes, WorldModel (one-entry cache), ManualRef, base ContainerStore, CompilerContext/extensions, ScriptManager (declarations only), LocalScripts\nEquipment mode additionally initializes: EquipmentRuntime, two actor InventoryStores, owned effects, plain-shirt transfer/drop/take and coherent session persistence/restart with an optional shared base ContainerStore; NPC stats and declaration locals when requested\nNot constructed: World, Environment, renderer, UI, audio, Lua runtime\nNot exercised: script instructions, live-world compiler queries, global script startup\n")

# Concrete application adapter; engine/network types meet only in this leaf.
# Not linked into server startup until joint command durability is installed.
add_library(tes3mp_native_inventory_service STATIC inventory_service.cpp inventory_service.hpp)
target_compile_features(tes3mp_native_inventory_service PUBLIC cxx_std_20)
target_link_libraries(tes3mp_native_inventory_service PUBLIC tes3mp_server_app PRIVATE tes3mp_native_equipment_runtime openmw-lib)
tes3mp_verify_target_dependencies(tes3mp_native_inventory_service
    ALLOWED tes3mp_native_equipment_runtime tes3mp_server_app openmw-lib)
tes3mp_verify_target_includes(tes3mp_native_inventory_service
    FORBIDDEN "apps/openmw/mwbase" "apps/openmw/mwgui" "apps/openmw/mwrender" "transfer_rehearsal.hpp" "equipment_tests.hpp")

add_executable(tes3mp_native_loadout_probe main.cpp)
target_link_libraries(tes3mp_native_loadout_probe PRIVATE tes3mp_native_loadout)
tes3mp_verify_target_dependencies(tes3mp_native_loadout_probe ALLOWED tes3mp_native_loadout)

# Standalone, filtered synthetic checks, not the upstream openmw-tests target.
add_executable(tes3mp_native_loadout_tests loadout_tests.cpp
    inventory_service_tests.cpp inventory_service_tests.hpp
    equipment_tests.cpp equipment_tests.hpp equipment_command.hpp
    transfer_rehearsal.cpp transfer_rehearsal.hpp transfer_rehearsal_tests.cpp
    transfer_save_codec.cpp transfer_save_codec.hpp
    transfer_restart.cpp
    transfer_file_sink.cpp transfer_file_sink.hpp
    transfer_rehearsal_command.cpp
    inventory_view.cpp inventory_view.hpp
    test_allocations.cpp test_allocations.hpp)
target_link_libraries(tes3mp_native_loadout_tests PRIVATE tes3mp_native_loadout tes3mp_native_inventory_service tes3mp_client_session)
tes3mp_verify_target_dependencies(tes3mp_native_loadout_tests ALLOWED tes3mp_native_loadout tes3mp_native_inventory_service tes3mp_client_session)
set_target_properties(tes3mp_native_equipment_runtime tes3mp_native_loadout tes3mp_native_loadout_probe tes3mp_native_loadout_tests
    PROPERTIES FOLDER "TES3MP/NativeProbe")
