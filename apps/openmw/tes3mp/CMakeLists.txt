include(${CMAKE_CURRENT_LIST_DIR}/../../../cmake/TES3MPVerifyTargetBoundaries.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../../../cmake/TES3MPRuntimeSafety.cmake)

tes3mp_validate_runtime_safety()

add_library(openmw_tes3mp_adapter STATIC
    adapter.cpp
    adapter.hpp
    desktop_connection.cpp
    desktop_connection.hpp
    engine_coordinator.hpp
    movement_mapping.cpp
    movement_mapping.hpp
    providers.hpp
    remote_motion.cpp
    remote_motion.hpp
)
if(TES3MP_OPENMW_DESKTOP_PROVIDERS)
    target_sources(openmw_tes3mp_adapter PRIVATE desktop_providers.cpp desktop_providers.hpp)
endif()
add_library(TES3MP::OpenMWAdapter ALIAS openmw_tes3mp_adapter)

target_compile_features(openmw_tes3mp_adapter PUBLIC cxx_std_20)
target_link_libraries(openmw_tes3mp_adapter
    PUBLIC tes3mp_client_session
    PRIVATE openmw-lib
)
if(TARGET TES3MP::GameNetworkingSocketsTransport)
    target_link_libraries(openmw_tes3mp_adapter PRIVATE TES3MP::GameNetworkingSocketsTransport)
    target_compile_definitions(openmw_tes3mp_adapter PRIVATE TES3MP_OPENMW_HAS_GNS)
endif()
set_target_properties(openmw_tes3mp_adapter PROPERTIES FOLDER "TES3MP")
tes3mp_enable_runtime_safety(openmw_tes3mp_adapter)

if(TARGET TES3MP::GameNetworkingSocketsTransport)
    tes3mp_verify_target_dependencies(openmw_tes3mp_adapter
        ALLOWED tes3mp_client_session openmw-lib TES3MP::GameNetworkingSocketsTransport)
else()
    tes3mp_verify_target_dependencies(openmw_tes3mp_adapter
        ALLOWED tes3mp_client_session openmw-lib)
endif()
