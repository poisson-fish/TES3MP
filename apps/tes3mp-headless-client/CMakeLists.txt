include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/TES3MPVerifyTargetBoundaries.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/TES3MPRuntimeSafety.cmake)

if(TES3MP_ENABLE_GNS_TRANSPORT)
    add_executable(tes3mp_headless_client main.cpp)
    target_compile_features(tes3mp_headless_client PRIVATE cxx_std_20)
    target_link_libraries(tes3mp_headless_client PRIVATE tes3mp_client_session tes3mp_transport_gns)
    set_target_properties(tes3mp_headless_client PROPERTIES FOLDER "TES3MP")
    tes3mp_enable_runtime_safety(tes3mp_headless_client)
    tes3mp_verify_target_dependencies(tes3mp_headless_client
        ALLOWED tes3mp_client_session tes3mp_transport_gns)
endif()
