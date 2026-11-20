# Quality gates

Every change intended for main should meet the following gates:

1. **Configure:** CMake configuration succeeds on the supported toolchain.
2. **Compile:** Debug and Release compile with warnings treated as errors.
3. **Unit tests:** deterministic core tests pass without a GPU or window.
4. **Integration tests:** subsystem boundaries are tested with real dependencies, not success-returning mocks.
5. **Content validation:** authored data passes its schema and reference checks.
6. **Runtime smoke test:** startup, shutdown, and failure paths are exercised when application/runtime integration exists.
7. **Documentation:** public contracts, limitations, and build requirements match the implementation.

CI success proves only the jobs that actually ran. It does not prove visual quality, historical accuracy, performance on end-user hardware, or correctness of untested paths.
