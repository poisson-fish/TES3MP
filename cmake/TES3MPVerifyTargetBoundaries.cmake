include_guard(GLOBAL)

function(tes3mp_verify_target_dependencies target)
    if (NOT TARGET ${target})
        message(FATAL_ERROR "TES3MP boundary check references missing target '${target}'")
    endif()

    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "" "ALLOWED")
    if (ARG_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR
            "TES3MP boundary check for '${target}' has unexpected arguments: ${ARG_UNPARSED_ARGUMENTS}")
    endif()

    get_target_property(direct_dependencies ${target} LINK_LIBRARIES)
    if (direct_dependencies STREQUAL "direct_dependencies-NOTFOUND")
        set(direct_dependencies)
    endif()

    foreach(dependency IN LISTS direct_dependencies)
        if (dependency MATCHES "^\\$<")
            message(FATAL_ERROR
                "TES3MP target '${target}' has unsupported generated direct dependency '${dependency}'")
        endif()

        if (NOT dependency IN_LIST ARG_ALLOWED)
            message(FATAL_ERROR
                "TES3MP target '${target}' has forbidden direct dependency '${dependency}'; "
                "allowed direct dependencies: '${ARG_ALLOWED}'")
        endif()
    endforeach()

    set_property(TARGET ${target} PROPERTY
        TES3MP_ALLOWED_DIRECT_DEPENDENCIES "${ARG_ALLOWED}")
endfunction()

function(tes3mp_verify_target_includes target)
    if (NOT TARGET ${target})
        message(FATAL_ERROR "TES3MP include check references missing target '${target}'")
    endif()

    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "" "FORBIDDEN")
    if (ARG_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR
            "TES3MP include check for '${target}' has unexpected arguments: ${ARG_UNPARSED_ARGUMENTS}")
    endif()

    get_target_property(target_sources ${target} SOURCES)
    foreach(source IN LISTS target_sources)
        if (IS_ABSOLUTE "${source}")
            set(source_path "${source}")
        else()
            get_target_property(source_dir ${target} SOURCE_DIR)
            set(source_path "${source_dir}/${source}")
        endif()

        if (NOT EXISTS "${source_path}" OR IS_DIRECTORY "${source_path}")
            continue()
        endif()

        file(READ "${source_path}" source_contents)
        foreach(forbidden IN LISTS ARG_FORBIDDEN)
            string(REGEX MATCH
                "#[ \t]*include[ \t]*[<\"][^>\"\r\n]*${forbidden}"
                forbidden_match "${source_contents}")
            if (forbidden_match)
                message(FATAL_ERROR
                    "TES3MP target '${target}' source '${source_path}' contains forbidden include family "
                    "'${forbidden}'")
            endif()
        endforeach()
    endforeach()
endfunction()
