# Single Header Automation Plan

## Goal

Remove the maintainer burden of manually updating the amalgamated `csv.hpp` while preserving a simple way to validate and distribute the single-header build.

## Current Problems

1. The generated single header is checked into git and must be manually refreshed.
2. `single_include_test` depends on a tracked/generated file rather than the current source tree.
3. PRs can be correct in source files but still appear incomplete if `single_include/csv.hpp` is stale.

## Proposed Direction

### Phase 1: Build-Generated Header for Testing (Done)

1. Generate the amalgamated header into the build directory via CMake.
2. Update `single_include_test` to compile against the build-generated header.
3. Keep the tracked `single_include/csv.hpp` temporarily during transition.

This makes the compile smoke test validate the header produced from the current source tree.

Status:

- Implemented in CMake and `single_header.py`
- `single_include_test` now builds against the generated build-tree header
- Validated locally by building `single_include_test` successfully against the generated artifact

`single_include_test` is sufficient as the automated validation layer for the single header:

- It is intended as a smoke test, not a second full behavioral test suite.
- It verifies that the generated `csv.hpp` compiles cleanly.
- It verifies inclusion across multiple translation units and catches common amalgamation issues (missing includes, duplicate definitions, ODR/link problems).

### Phase 2: CI Artifact on Push to `master`

1. Add a GitHub Actions workflow step that generates `csv.hpp` on every push to `master`.
2. Upload the generated header as a workflow artifact.
3. Optionally upload the same artifact on PRs for inspection/debugging.
4. Run `single_include_test` in CI against the freshly generated header as a smoke test.

This removes the need to rely on a committed artifact for day-to-day development.

### Phase 3: Decide on Repository Distribution Policy

Choose one of these models:

1. Keep `single_include/csv.hpp` in git for convenience, but stop using it as the source of truth for tests.
2. Remove tracked single-header artifacts entirely and treat the generated header as a CI/release artifact.

## Recommendation

Start with Phase 1 and Phase 2.

That gives immediate workflow relief without forcing a user-facing distribution change at the same time.

## Key Principle

If the single header is a test/build intermediate, it should be generated in the build.

If the single header is a distribution format, it should be published as an artifact, not manually maintained as source.
