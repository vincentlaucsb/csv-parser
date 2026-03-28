## Shared Test Utilities Guidance

This folder contains reusable test helpers consumed by multiple test files.

When adding, removing, or renaming a utility in `tests/shared/`, you must also update [tests/CLAUDE.md](../CLAUDE.md):

1. Update the **Shared Test Utilities (`tests/shared/`)** table.
2. Add or adjust usage notes if the utility has required patterns.
3. Keep examples aligned with the current helper names and includes.

This keeps AI agents and maintainers synchronized on available test helpers.
