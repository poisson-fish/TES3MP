# Resolves default dependency paths and triplets when CMake presets are
# activated directly in an IDE (such as VSCode) without pre-populated VNEXT_*
# environment variables. Must be included before project(OpenMW).

if(CMAKE_HOST_APPLE)
    set(_tes3mp_vcpkg_triplet "$ENV{VNEXT_VCPKG_TRIPLET}")
    if(NOT _tes3mp_vcpkg_triplet AND VCPKG_TARGET_TRIPLET)
        set(_tes3mp_vcpkg_triplet "${VCPKG_TARGET_TRIPLET}")
    endif()
    if(NOT _tes3mp_vcpkg_triplet)
        if(CMAKE_OSX_ARCHITECTURES STREQUAL "x86_64")
            set(_tes3mp_vcpkg_triplet "x64-osx-dynamic")
        elseif(CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
            set(_tes3mp_vcpkg_triplet "arm64-osx-dynamic")
        else()
            execute_process(
                COMMAND uname -m
                OUTPUT_VARIABLE _tes3mp_host_arch
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
            )
            if(_tes3mp_host_arch STREQUAL "arm64")
                set(_tes3mp_vcpkg_triplet "arm64-osx-dynamic")
            else()
                set(_tes3mp_vcpkg_triplet "x64-osx-dynamic")
            endif()
            unset(_tes3mp_host_arch)
        endif()
    endif()

    set(_tes3mp_vcpkg_root "$ENV{VNEXT_VCPKG_ROOT}")
    if(NOT _tes3mp_vcpkg_root)
        if(EXISTS "${CMAKE_SOURCE_DIR}/deps/openmw-deps/scripts/buildsystems/vcpkg.cmake")
            set(_tes3mp_vcpkg_root "${CMAKE_SOURCE_DIR}/deps/openmw-deps")
        elseif(EXISTS "${CMAKE_SOURCE_DIR}/deps/scripts/buildsystems/vcpkg.cmake")
            set(_tes3mp_vcpkg_root "${CMAKE_SOURCE_DIR}/deps")
        else()
            set(_tes3mp_vcpkg_root "/tmp/openmw-deps")
        endif()
    endif()

    set(_tes3mp_qt_root "$ENV{VNEXT_QT_ROOT}")
    if(NOT _tes3mp_qt_root)
        if(_tes3mp_vcpkg_triplet STREQUAL "arm64-osx-dynamic")
            set(_tes3mp_qt_root "/opt/homebrew/opt/qt")
        else()
            set(_tes3mp_qt_root "/usr/local/opt/qt")
        endif()
    endif()

    if(CMAKE_TOOLCHAIN_FILE STREQUAL "/scripts/buildsystems/vcpkg.cmake")
        set(CMAKE_TOOLCHAIN_FILE "${_tes3mp_vcpkg_root}/scripts/buildsystems/vcpkg.cmake"
            CACHE FILEPATH "Vcpkg toolchain file" FORCE)
    endif()
    if(DEFINED VCPKG_HOST_TRIPLET AND VCPKG_HOST_TRIPLET STREQUAL "")
        set(VCPKG_HOST_TRIPLET "${_tes3mp_vcpkg_triplet}"
            CACHE STRING "Vcpkg host triplet" FORCE)
    endif()
    if(DEFINED VCPKG_TARGET_TRIPLET AND VCPKG_TARGET_TRIPLET STREQUAL "")
        set(VCPKG_TARGET_TRIPLET "${_tes3mp_vcpkg_triplet}"
            CACHE STRING "Vcpkg target triplet" FORCE)
    endif()
    if(CMAKE_PREFIX_PATH STREQUAL "/installed/;" OR CMAKE_PREFIX_PATH MATCHES "^/installed/")
        set(_tes3mp_prefix_paths "${_tes3mp_vcpkg_root}/installed/${_tes3mp_vcpkg_triplet}")
        if(EXISTS "${_tes3mp_qt_root}")
            list(APPEND _tes3mp_prefix_paths "${_tes3mp_qt_root}")
        endif()
        set(CMAKE_PREFIX_PATH "${_tes3mp_prefix_paths}"
            CACHE STRING "CMake prefix search paths" FORCE)
        unset(_tes3mp_prefix_paths)
    endif()

    unset(_tes3mp_vcpkg_triplet)
    unset(_tes3mp_vcpkg_root)
    unset(_tes3mp_qt_root)
elseif(CMAKE_HOST_WIN32)
    set(_tes3mp_vcpkg_root "$ENV{VNEXT_VCPKG_ROOT}")
    if(NOT _tes3mp_vcpkg_root)
        set(_tes3mp_vcpkg_root "${CMAKE_SOURCE_DIR}/deps")
    endif()

    set(_tes3mp_qt_root "$ENV{VNEXT_QT_ROOT}")
    if(NOT _tes3mp_qt_root)
        set(_tes3mp_qt_root "${_tes3mp_vcpkg_root}/Qt/6.6.3/msvc2019_64")
    endif()

    if(CMAKE_TOOLCHAIN_FILE STREQUAL "/scripts/buildsystems/vcpkg.cmake")
        set(CMAKE_TOOLCHAIN_FILE "${_tes3mp_vcpkg_root}/scripts/buildsystems/vcpkg.cmake"
            CACHE FILEPATH "Vcpkg toolchain file" FORCE)
    endif()
    if(DEFINED CMAKE_PREFIX_PATH AND CMAKE_PREFIX_PATH STREQUAL "" AND EXISTS "${_tes3mp_qt_root}")
        set(CMAKE_PREFIX_PATH "${_tes3mp_qt_root}"
            CACHE STRING "CMake prefix search paths" FORCE)
    endif()
    if(LuaJit_INCLUDE_DIR STREQUAL "/installed/x64-windows/include/luajit")
        set(LuaJit_INCLUDE_DIR "${_tes3mp_vcpkg_root}/installed/x64-windows/include/luajit"
            CACHE PATH "LuaJIT include directory" FORCE)
    endif()
    if(LuaJit_LIBRARY STREQUAL "/installed/x64-windows/lib/lua51.lib")
        set(LuaJit_LIBRARY "${_tes3mp_vcpkg_root}/installed/x64-windows/lib/lua51.lib"
            CACHE FILEPATH "LuaJIT library" FORCE)
    endif()

    unset(_tes3mp_vcpkg_root)
    unset(_tes3mp_qt_root)
endif()

if(NOT FETCHCONTENT_SOURCE_DIR_RECASTNAVIGATION AND EXISTS "${CMAKE_SOURCE_DIR}/deps/recastnavigation/CMakeLists.txt")
    set(FETCHCONTENT_SOURCE_DIR_RECASTNAVIGATION "${CMAKE_SOURCE_DIR}/deps/recastnavigation"
        CACHE PATH "Cached RecastNavigation source directory" FORCE)
endif()
if(NOT FETCHCONTENT_SOURCE_DIR_YAML-CPP AND EXISTS "${CMAKE_SOURCE_DIR}/deps/yaml-cpp/CMakeLists.txt")
    set(FETCHCONTENT_SOURCE_DIR_YAML-CPP "${CMAKE_SOURCE_DIR}/deps/yaml-cpp"
        CACHE PATH "Cached yaml-cpp source directory" FORCE)
endif()
