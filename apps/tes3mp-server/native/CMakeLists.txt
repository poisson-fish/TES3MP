# Temporary M1 link: openmw-lib brings rendering/audio/UI libraries into the
# dependency graph, but the loader does not initialize those services. Replace
# this edge with extracted engine content/runtime libraries before production
# headless packaging. Do not relax components/tes3mp's independent boundaries.
add_library(tes3mp_native_loadout STATIC loadout.cpp loadout.hpp diagnostic.hpp inventory.cpp enchantment.cpp)
target_compile_features(tes3mp_native_loadout PUBLIC cxx_std_20)
target_link_libraries(tes3mp_native_loadout PRIVATE openmw-lib Boost::program_options)
tes3mp_verify_target_dependencies(tes3mp_native_loadout ALLOWED openmw-lib Boost::program_options)
tes3mp_verify_target_includes(tes3mp_native_loadout
    FORBIDDEN "components/tes3mp" "apps/openmw/mwbase" "apps/openmw/mwgui" "apps/openmw/mwrender")

file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/loadout-dependencies-$<CONFIG>.txt"
    CONTENT "Probe direct: tes3mp_native_loadout\nLoadout direct: $<TARGET_PROPERTY:tes3mp_native_loadout,LINK_LIBRARIES>\nOpenMW direct: $<TARGET_PROPERTY:openmw-lib,LINK_LIBRARIES>\nComponents direct: $<TARGET_PROPERTY:components,LINK_LIBRARIES>\nInitialized: ConfigurationManager, Utf8Encoder, ReadersCache, EsmLoader, ESMStore\nInventory mode additionally initializes: engine classes, WorldModel (one-entry cache), ManualRef, base ContainerStore, CompilerContext/extensions, ScriptManager (declarations only), LocalScripts\nNot constructed: World, Environment, renderer, UI, audio, Lua runtime, actor InventoryStore\nNot exercised: script instructions, live-world compiler queries, global script startup\n")

add_executable(tes3mp_native_loadout_probe main.cpp)
target_link_libraries(tes3mp_native_loadout_probe PRIVATE tes3mp_native_loadout)
tes3mp_verify_target_dependencies(tes3mp_native_loadout_probe ALLOWED tes3mp_native_loadout)

# Standalone, filtered synthetic checks, not the upstream openmw-tests target.
add_executable(tes3mp_native_loadout_tests loadout_tests.cpp)
target_link_libraries(tes3mp_native_loadout_tests PRIVATE tes3mp_native_loadout)
tes3mp_verify_target_dependencies(tes3mp_native_loadout_tests ALLOWED tes3mp_native_loadout)
set_target_properties(tes3mp_native_loadout tes3mp_native_loadout_probe tes3mp_native_loadout_tests
    PROPERTIES FOLDER "TES3MP/NativeProbe")
