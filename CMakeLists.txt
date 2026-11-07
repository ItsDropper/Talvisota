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
    src/talvisota/core/Config.cpp
    src/talvisota/core/GameSession.cpp
    src/talvisota/core/Logger.cpp
)
add_library(Talvisota::Core ALIAS talvisota_core)
target_compile_features(talvisota_core PUBLIC cxx_std_23)
target_include_directories(talvisota_core PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>")
talvisota_apply_compiler_options(talvisota_core)

add_library(talvisota_platform STATIC
    src/talvisota/platform/CrashDiagnostics.cpp
    src/talvisota/platform/UserPaths.cpp
)
add_library(Talvisota::Platform ALIAS talvisota_platform)
target_compile_features(talvisota_platform PUBLIC cxx_std_23)
target_include_directories(talvisota_platform PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>")
talvisota_apply_compiler_options(talvisota_platform)

add_library(talvisota_application STATIC src/talvisota/app/Application.cpp)
add_library(Talvisota::Application ALIAS talvisota_application)
target_compile_features(talvisota_application PUBLIC cxx_std_23)
target_include_directories(talvisota_application PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>")
target_link_libraries(talvisota_application PUBLIC Talvisota::Core Talvisota::Platform)
talvisota_apply_compiler_options(talvisota_application)

add_executable(talvisota src/talvisota/app/main.cpp)
target_link_libraries(talvisota PRIVATE Talvisota::Application)
target_compile_features(talvisota PRIVATE cxx_std_23)
talvisota_apply_compiler_options(talvisota)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
