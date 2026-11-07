add_executable(talvisota_core_tests core/GameSessionTests.cpp)
target_link_libraries(talvisota_core_tests PRIVATE Talvisota::Core)
target_compile_features(talvisota_core_tests PRIVATE cxx_std_23)
talvisota_apply_compiler_options(talvisota_core_tests)
add_test(NAME Talvisota.Core.GameSession COMMAND talvisota_core_tests)

add_executable(talvisota_config_tests core/ConfigTests.cpp)
target_link_libraries(talvisota_config_tests PRIVATE Talvisota::Core)
target_compile_features(talvisota_config_tests PRIVATE cxx_std_23)
talvisota_apply_compiler_options(talvisota_config_tests)
add_test(NAME Talvisota.Core.Configuration COMMAND talvisota_config_tests)

add_executable(talvisota_application_tests app/ApplicationTests.cpp)
target_link_libraries(talvisota_application_tests PRIVATE Talvisota::Application)
target_compile_features(talvisota_application_tests PRIVATE cxx_std_23)
talvisota_apply_compiler_options(talvisota_application_tests)
add_test(NAME Talvisota.Application.Lifecycle COMMAND talvisota_application_tests)

add_executable(talvisota_renderer_contract_tests presentation/SceneRendererContractTests.cpp)
target_link_libraries(talvisota_renderer_contract_tests PRIVATE Talvisota::RendererContract)
target_compile_features(talvisota_renderer_contract_tests PRIVATE cxx_std_23)
talvisota_apply_compiler_options(talvisota_renderer_contract_tests)
add_test(NAME Talvisota.Presentation.SceneRendererContract COMMAND talvisota_renderer_contract_tests)

add_executable(talvisota_content_tests content/ContentRegistryTests.cpp)
target_link_libraries(talvisota_content_tests PRIVATE Talvisota::Content)
target_compile_features(talvisota_content_tests PRIVATE cxx_std_23)
talvisota_apply_compiler_options(talvisota_content_tests)
add_test(NAME Talvisota.Content.Registry COMMAND talvisota_content_tests)


if(TALVISOTA_ENABLE_VANTA_ENGINE)
    add_executable(talvisota_engine_asset_pipeline_tests
        content/EngineAssetPipelineTests.cpp
    )
    target_link_libraries(talvisota_engine_asset_pipeline_tests PRIVATE Talvisota::EngineAssetValidation)
    target_compile_features(talvisota_engine_asset_pipeline_tests PRIVATE cxx_std_23)
    target_compile_definitions(talvisota_engine_asset_pipeline_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
    talvisota_apply_compiler_options(talvisota_engine_asset_pipeline_tests)
    add_test(NAME Talvisota.Content.EngineAssetPipeline COMMAND talvisota_engine_asset_pipeline_tests)

    add_executable(talvisota_engine_asset_package_tests
        content/EngineAssetPackageTests.cpp
    )
    target_link_libraries(talvisota_engine_asset_package_tests PRIVATE Talvisota::EngineAssetValidation)
    target_compile_features(talvisota_engine_asset_package_tests PRIVATE cxx_std_23)
    target_compile_definitions(talvisota_engine_asset_package_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
    talvisota_apply_compiler_options(talvisota_engine_asset_package_tests)
    add_test(NAME Talvisota.Content.EngineAssetPackage COMMAND talvisota_engine_asset_package_tests)

    add_executable(talvisota_engine_asset_validation_tests
        content/EngineAssetValidationTests.cpp
    )
    target_link_libraries(talvisota_engine_asset_validation_tests PRIVATE Talvisota::EngineAssetValidation)
    target_compile_features(talvisota_engine_asset_validation_tests PRIVATE cxx_std_23)
    target_compile_definitions(talvisota_engine_asset_validation_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
    talvisota_apply_compiler_options(talvisota_engine_asset_validation_tests)
    add_test(NAME Talvisota.Content.EngineAssetValidation COMMAND talvisota_engine_asset_validation_tests)
endif()

add_executable(talvisota_world_tests world/WorldRegionTests.cpp)
target_link_libraries(talvisota_world_tests PRIVATE Talvisota::World)
target_compile_definitions(talvisota_world_tests PRIVATE TALVISOTA_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
target_compile_features(talvisota_world_tests PRIVATE cxx_std_23)
talvisota_apply_compiler_options(talvisota_world_tests)
add_test(NAME Talvisota.World.RegionLifecycle COMMAND talvisota_world_tests)
