cmake_minimum_required(VERSION 3.25)

project(Talvisota
    VERSION 0.1.0
    DESCRIPTION "A historically grounded first-person game set during the Winter War"
    LANGUAGES CXX
)

include(CTest)

option(TALVISOTA_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(TalvisotaCompilerOptions)

add_library(talvisota_core STATIC
    src/talvisota/core/GameSession.cpp
)
add_library(Talvisota::Core ALIAS talvisota_core)

target_compile_features(talvisota_core PUBLIC cxx_std_23)
target_include_directories(talvisota_core
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>"
)

talvisota_apply_compiler_options(talvisota_core)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
