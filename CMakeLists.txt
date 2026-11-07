cmake_minimum_required(VERSION 3.25)

project(Talvisota
    VERSION 0.1.0
    DESCRIPTION "A historically grounded first-person game set during the Winter War"
    LANGUAGES CXX
)

include(CTest)

option(TALVISOTA_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
option(TALVISOTA_ENABLE_VANTA_ENGINE "Build the optional VantaEngine renderer adapter (MSVC x64)" OFF)

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

add_library(talvisota_content STATIC
    src/talvisota/content/ContentRegistry.cpp
    src/talvisota/content/MaterialDefinition.cpp
    src/talvisota/content/WorldDefinition.cpp
    src/talvisota/content/AuthoredDefinition.cpp
)
add_library(Talvisota::Content ALIAS talvisota_content)
target_compile_features(talvisota_content PUBLIC cxx_std_23)
target_include_directories(talvisota_content PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>")
talvisota_apply_compiler_options(talvisota_content)

add_library(talvisota_world STATIC src/talvisota/world/WorldRegion.cpp)
add_library(Talvisota::World ALIAS talvisota_world)
target_compile_features(talvisota_world PUBLIC cxx_std_23)
target_include_directories(talvisota_world PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>")
target_link_libraries(talvisota_world PUBLIC Talvisota::Content)
talvisota_apply_compiler_options(talvisota_world)

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

add_executable(talvisota_content_validate tools/validation/main.cpp)
target_compile_features(talvisota_content_validate PRIVATE cxx_std_23)
target_link_libraries(talvisota_content_validate PRIVATE Talvisota::Content Talvisota::World)
talvisota_apply_compiler_options(talvisota_content_validate)

# Procedural environment textures are generated deterministically from source code.
# The generated BMPs are build products consumed by the manifest, MTL importer and packager.
add_executable(talvisota_environment_texture_gen
    tools/asset_pipeline/EnvironmentTextureGenerator.cpp
)
target_compile_features(talvisota_environment_texture_gen PRIVATE cxx_std_23)
talvisota_apply_compiler_options(talvisota_environment_texture_gen)

add_custom_target(talvisota_environment_textures ALL
    COMMAND $<TARGET_FILE:talvisota_environment_texture_gen>
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/textures/environment"
    DEPENDS talvisota_environment_texture_gen
    VERBATIM
)
add_dependencies(talvisota_content_validate talvisota_environment_textures)

add_library(talvisota_renderer_contract STATIC
    src/talvisota/presentation/SceneRenderer.cpp
)
add_library(Talvisota::RendererContract ALIAS talvisota_renderer_contract)
target_compile_features(talvisota_renderer_contract PUBLIC cxx_std_23)
target_include_directories(talvisota_renderer_contract PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>"
)
talvisota_apply_compiler_options(talvisota_renderer_contract)

if(TALVISOTA_ENABLE_VANTA_ENGINE)
    if(NOT WIN32 OR NOT MSVC)
        message(FATAL_ERROR "TALVISOTA_ENABLE_VANTA_ENGINE requires Windows x64 and MSVC because VantaEngine uses the Windows SDK and WRL.")
    endif()

    include(FetchContent)
    set(TALVISOTA_VANTA_ENGINE_REVISION "284862a057eaa01b2f1542ded4dd1ad0d26b8e1d")
    set(_talvisota_build_testing "${BUILD_TESTING}")
    set(BUILD_TESTING OFF)
    FetchContent_Declare(vantaengine
        GIT_REPOSITORY https://github.com/ItsDropper/VantaEngine.git
        GIT_TAG "${TALVISOTA_VANTA_ENGINE_REVISION}"
        GIT_SHALLOW FALSE
    )
    FetchContent_MakeAvailable(vantaengine)
    set(BUILD_TESTING "${_talvisota_build_testing}")


    add_library(talvisota_engine_asset_validation STATIC
        src/talvisota/content/AudioDecode.cpp
        src/talvisota/content/VantaAssetValidation.cpp
        src/talvisota/content/VantaAssetPackage.cpp
    )
    add_library(Talvisota::EngineAssetValidation ALIAS talvisota_engine_asset_validation)
    target_compile_features(talvisota_engine_asset_validation PUBLIC cxx_std_23)
    target_compile_definitions(talvisota_engine_asset_validation PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
    target_include_directories(talvisota_engine_asset_validation PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>"
    )
    target_link_libraries(talvisota_engine_asset_validation PUBLIC
        Talvisota::Content
        Talvisota::World
        VantaEngine::Runtime
        bcrypt
    )
    talvisota_apply_compiler_options(talvisota_engine_asset_validation)

    add_executable(talvisota_asset_validate tools/validation/engine_assets_main.cpp)
    target_compile_features(talvisota_asset_validate PRIVATE cxx_std_23)
    target_compile_definitions(talvisota_asset_validate PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
    target_link_libraries(talvisota_asset_validate PRIVATE Talvisota::EngineAssetValidation)
    talvisota_apply_compiler_options(talvisota_asset_validate)

    add_executable(talvisota_content_package tools/validation/package_main.cpp)
    target_compile_features(talvisota_content_package PRIVATE cxx_std_23)
    target_compile_definitions(talvisota_content_package PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
    target_link_libraries(talvisota_content_package PRIVATE Talvisota::EngineAssetValidation)
    talvisota_apply_compiler_options(talvisota_content_package)

    add_executable(talvisota_texture_process tools/asset_pipeline/TextureProcess.cpp)
    target_compile_features(talvisota_texture_process PRIVATE cxx_std_23)
    target_compile_definitions(talvisota_texture_process PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
    target_link_libraries(talvisota_texture_process PRIVATE VantaEngine::Runtime)
    talvisota_apply_compiler_options(talvisota_texture_process)

    add_library(talvisota_renderer_adapter STATIC
        src/talvisota/presentation/VantaSceneRenderer.cpp
    )
    add_library(Talvisota::RendererAdapter ALIAS talvisota_renderer_adapter)
    target_compile_features(talvisota_renderer_adapter PUBLIC cxx_std_23)
    target_compile_definitions(talvisota_renderer_adapter PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
    target_include_directories(talvisota_renderer_adapter PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>"
    )
    target_link_libraries(talvisota_renderer_adapter PUBLIC
        Talvisota::RendererContract
        VantaEngine::Runtime
    )
    talvisota_apply_compiler_options(talvisota_renderer_adapter)

    add_executable(talvisota_renderer_sample WIN32
        samples/renderer_adapter/SceneSubmissionSample.cpp
    )
    target_compile_features(talvisota_renderer_sample PRIVATE cxx_std_23)
    target_compile_definitions(talvisota_renderer_sample PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
    target_link_libraries(talvisota_renderer_sample PRIVATE Talvisota::RendererAdapter)
    talvisota_apply_compiler_options(talvisota_renderer_sample)
endif()

if(TARGET talvisota_asset_validate)
    add_dependencies(talvisota_asset_validate talvisota_environment_textures)
endif()
if(TARGET talvisota_content_package)
    add_dependencies(talvisota_content_package talvisota_environment_textures)
endif()

add_executable(talvisota src/talvisota/app/main.cpp)
target_link_libraries(talvisota PRIVATE Talvisota::Application)
target_compile_features(talvisota PRIVATE cxx_std_23)
talvisota_apply_compiler_options(talvisota)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
