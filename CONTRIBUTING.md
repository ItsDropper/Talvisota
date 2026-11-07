# Contribution rules

Before contributing, read [LICENSE](LICENSE). The repository is source-available under a restrictive custom licence, not an open-source licence. A public issue, pull request, fork, or contribution does not grant permission to reuse project code or assets.

## Engineering standards

- Keep domain rules independent of graphics and platform APIs.
- Prefer explicit ownership and dependency injection over mutable global state.
- Validate external data at boundaries.
- Make invalid states difficult to represent.
- Use deterministic tests for core rules.
- Add regression tests for bug fixes.
- Treat warnings as errors in CI.
- Do not add placeholder implementations, fake success paths, or unimplemented public APIs.
- Do not claim a feature is complete until its real execution path and failure cases are implemented and tested.
- Do not add dependencies without documenting their purpose, licence, version pin, and maintenance risk.

## Change quality

A change should include:
1. The production implementation.
2. Tests for normal, invalid, and boundary behavior where applicable.
3. Documentation for public contracts and non-obvious constraints.
4. Evidence from the actual build/test workflow.

Keep commits focused and avoid mixing unrelated refactors with feature work.
