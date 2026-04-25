# Architecture Index

This file is a top-level index for architecture documentation.

Primary architecture document:
- include/internal/ARCHITECTURE.md

Subsystem deep-dive:
- include/internal/THREADSAFE_DEQUE_DESIGN.md

Operational/testing guidance:
- AGENTS.md
- tests/AGENTS.md

Notes:
- Internal architecture content lives under include/internal to stay close to implementation.
- Detailed file map, parser data flow, and component relationship diagrams are maintained in include/internal/ARCHITECTURE.md.
- Queue synchronization details are maintained only in THREADSAFE_DEQUE_DESIGN.md to avoid duplication.
- Always update or remove incorrect comments.
- Public API comments should remain user-facing and avoid references to internal helper/function details.
- Public API docs belong on declarations in `.hpp` files; keep `.cpp` comments focused on implementation notes, concurrency invariants, performance rationale, and bug history.
- When editing a function, remove `@param` and `@return` descriptions that merely restate the function name or signature.
- Private member naming should prefer trailing underscores; when editing mixed-style code, normalize the touched region toward that convention.
- Prefer LF (`\n`) line endings for tracked source, test, CMake, and Markdown files; when touching a file with mixed endings, normalize it to LF unless there is a file-specific reason not to.
- Compatibility macros defined in `common.hpp` must only be referenced after including `common.hpp`. See AGENTS.md and CLAUDE.md for details.
- API constraints should be user-friendly: do not over-constrain templates unless needed for correctness, safety, or a measured performance win.
- `CSVReader` is intentionally non-copyable and move-enabled; use explicit ownership transfer patterns (`std::move`, `std::unique_ptr`) at API boundaries.
- Respect existing compile-time compatibility macros (`IF_CONSTEXPR`, `CONSTEXPR_VALUE`, etc.) unless correctness requires change.
- Avoid semantic rewrites to silence compiler warnings; prefer precise scoped suppression where appropriate.
- Opportunistic rewrites are acceptable when safe/justified, but should be kept separate from urgent compiler triage unless requested.
- When changing compile-time behavior, explicitly document tradeoffs (codegen, performance, portability, readability).
- If a build fix appears to require more than ~3 files or ~60 changed lines, pause and confirm scope first.
- Apply the 5/2 anti-duplication rule: if equivalent behavior exists in 2+ code paths and each copy is ~5+ meaningful lines, extract a shared helper; if duplication remains, document why and keep regression coverage for each path.

