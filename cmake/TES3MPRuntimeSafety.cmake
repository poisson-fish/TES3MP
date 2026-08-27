include_guard(GLOBAL)

option(TES3MP_ENABLE_ASAN_UBSAN "Instrument selected TES3MP targets with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(TES3MP_ENABLE_TSAN "Instrument selected TES3MP targets with ThreadSanitizer" OFF)
option(TES3MP_BUILD_FUZZERS "Build Clang libFuzzer targets for bounded TES3MP parsers" OFF)

function(tes3mp_validate_runtime_safety)
    if(TES3MP_ENABLE_ASAN_UBSAN AND TES3MP_ENABLE_TSAN)
        message(FATAL_ERROR "TES3MP ASan+UBSan and ThreadSanitizer profiles require separate build directories")
    endif()

    if(TES3MP_BUILD_FUZZERS AND NOT TES3MP_ENABLE_ASAN_UBSAN)
        message(FATAL_ERROR "TES3MP fuzz targets require the ASan+UBSan profile")
    endif()

    if(TES3MP_ENABLE_ASAN_UBSAN OR TES3MP_ENABLE_TSAN OR TES3MP_BUILD_FUZZERS)
        if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
            message(FATAL_ERROR "TES3MP runtime-safety profiles currently require Linux")
        endif()
        if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR NOT CMAKE_CXX_COMPILER_VERSION MATCHES "^18\\.")
            message(FATAL_ERROR "TES3MP runtime-safety profiles require Clang 18 exactly")
        endif()
    endif()
endfunction()

function(tes3mp_enable_runtime_safety target)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "TES3MP runtime-safety registration references missing target '${target}'")
    endif()

    if(TES3MP_ENABLE_ASAN_UBSAN)
        target_compile_options(${target} PRIVATE
            -fno-omit-frame-pointer
            -fno-sanitize-recover=all
            -fsanitize=address,undefined
        )
        target_link_options(${target} PRIVATE
            -fno-sanitize-recover=all
            -fsanitize=address,undefined
        )
    elseif(TES3MP_ENABLE_TSAN)
        target_compile_options(${target} PRIVATE
            -fno-omit-frame-pointer
            -fsanitize=thread
        )
        target_link_options(${target} PRIVATE -fsanitize=thread)
    endif()
endfunction()

function(tes3mp_enable_libfuzzer target)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "TES3MP libFuzzer registration references missing target '${target}'")
    endif()
    if(NOT TES3MP_BUILD_FUZZERS OR NOT TES3MP_ENABLE_ASAN_UBSAN)
        message(FATAL_ERROR "TES3MP libFuzzer targets require fuzzing and ASan+UBSan to be enabled")
    endif()

    target_compile_options(${target} PRIVATE -fsanitize=fuzzer-no-link)
    target_link_options(${target} PRIVATE -fsanitize=fuzzer)
endfunction()
