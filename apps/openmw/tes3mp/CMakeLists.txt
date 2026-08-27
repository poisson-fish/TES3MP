include(${CMAKE_CURRENT_LIST_DIR}/../../../cmake/TES3MPVerifyTargetBoundaries.cmake)

add_library(openmw_tes3mp_adapter STATIC adapter.cpp)
add_library(TES3MP::OpenMWAdapter ALIAS openmw_tes3mp_adapter)

target_compile_features(openmw_tes3mp_adapter PUBLIC cxx_std_20)
target_link_libraries(openmw_tes3mp_adapter
    PUBLIC tes3mp_client_session
    PRIVATE openmw-lib
)
set_target_properties(openmw_tes3mp_adapter PROPERTIES FOLDER "TES3MP")

tes3mp_verify_target_dependencies(openmw_tes3mp_adapter
    ALLOWED tes3mp_client_session openmw-lib)
