function(talvisota_apply_compiler_options target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4
            /permissive-
            /Zc:__cplusplus
            /utf-8
            /EHsc
            /GR
            $<$<BOOL:${TALVISOTA_WARNINGS_AS_ERRORS}>:/WX>
        )
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wsign-conversion
            -Wshadow
            $<$<BOOL:${TALVISOTA_WARNINGS_AS_ERRORS}>:-Werror>
        )
    endif()
endfunction()
