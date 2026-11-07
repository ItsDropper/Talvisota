add_executable(talvisota_core_tests
    core/GameSessionTests.cpp
)

target_link_libraries(talvisota_core_tests PRIVATE Talvisota::Core)
target_compile_features(talvisota_core_tests PRIVATE cxx_std_23)
talvisota_apply_compiler_options(talvisota_core_tests)

add_test(NAME Talvisota.Core.GameSession COMMAND talvisota_core_tests)
