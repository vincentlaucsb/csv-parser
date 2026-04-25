# CSV Parser - AI Agent Context

Architectural overview for AI assistants working with this codebase.

> **Maintenance rule:** Whenever this file is changed, update both `CLAUDE.md` and `ARCHITECTURE.md` in the same directory to reflect relevant changes. `CLAUDE.md` is a bullet-point summary and `ARCHITECTURE.md` is the top-level architecture index; both must stay in sync with this guidance.

## Critical: single_include/csv.hpp Is A Shim

`single_include/csv.hpp` is intentionally **non-functional** and exists only as a compatibility shim.

- Do **not** compile against `single_include/csv.hpp`
- For single-header validation, generate `build/.../single_include_generated/csv.hpp` via the `generate_single_header` target, then compile that generated file
- For unamalgamated usage, include headers from `include/`

This guard exists to prevent stale-in-repo amalgamated headers and to force use of the canonical generated distribution.

## Critical: Two Independent Code Paths

The `CSVReader` class has **two completely different implementations**:

```cpp
// PATH 1: Memory-mapped I/O (MmapParser)
CSVReader reader("filename.csv");

// PATH 2: Stream-based (StreamParser)
std::ifstream infile("filename.csv", std::ios::binary);
CSVReader reader(infile, format);
```

**Impact:** Bugs can exist in one path but not the other (see issue #281). Any test validating parsing behavior must test BOTH paths using Catch2 `SECTION`.

## Threading: Worker + 10MB Chunks

- Worker thread reads in 10MB chunks (`CSV_CHUNK_SIZE_DEFAULT`)
- Communicates via `ThreadSafeDeque<CSVRow>`
- Exceptions propagate via `std::exception_ptr`
- Critical: Fields spanning chunk boundaries must not corrupt

**Testing requirement:** Use ≥500K rows to cross 10MB boundary.

For detailed file mapping, parser data flow, and component relationships, see `ARCHITECTURE.md` and `include/internal/ARCHITECTURE.md`.

## Common Pitfalls

1. **Don't assume one code path:** Mmap and stream paths are different. Always test both.
2. **Don't write tiny tests:** Need ≥500K rows to cross 10MB chunk boundary.
3. **Don't use uniform values:** Each column needs distinct values to detect corruption.
4. **Don't ignore async:** Worker thread means exceptions must use `exception_ptr`.
5. **Don't change one constructor:** Likely affects both mmap and stream paths.
6. **`CSVReader` is non-copyable and move-enabled.** Prefer explicit ownership transfer (`std::move`) or `std::unique_ptr<CSVReader>` when sharing/handing off parser ownership across APIs.
7. **Prefer user-friendly API constraints.** Do not narrow template constraints unless required for correctness, safety, or a measured performance win. If an implementation already handles common standard-library containers/ranges correctly, keep those inputs accepted instead of over-constraining APIs for aesthetic purity.
8.  **Opportunistic rewrites/refactors are allowed when they are safe and justified.** Keep them separated from build-fix urgency where possible, and avoid bundling unrelated churn with compiler triage unless explicitly requested.
9. **When proposing changes that affect compile-time behavior, explain the tradeoff clearly.** Call out any impact to codegen, performance, portability, and readability before applying the change.
10. **If a build fix appears to require more than ~3 files or ~60 changed lines, pause and confirm scope first.** Provide a short justification before expanding further.

See `tests/AGENTS.md` for test strategy, checklist, and conventions.

### Rules for Coding
1. **Use compatibility macros defined in `common.hpp`** for cross-compiler or cross-standard concerns. If it doesn't exist, consider creating one.
2. **Compatibility macros defined in `common.hpp` MUST be referenced only after including `common.hpp`** to ensure correctness.
3. **Prefer compile time control flow and assertions where possible**. For example, if a branch may be safely written with `if constexpr`, then use the `IF_CONSTEXPR` macro (from `common.hpp`) to ensure C++11 compatibility while ensuring optimal control flow for C++17 and later users.
   1. **If this causes compiler warnings, always silence the compiler. Do not revert to unnecessary runtime flow.**
4. **Prefer trailing underscore for private members** (for example `source_`, `leftover_`). When you touch code with mixed private-member naming styles, normalize the edited region toward trailing underscores instead of introducing more leading-underscore or unsuffixed names.
5. **Apply the 5/2 anti-duplication rule.**
	1. If equivalent behavior exists in 2 or more code paths and each copy is about 5+ meaningful lines, extract a shared helper.
	2. If duplication is intentionally kept, add a brief comment explaining why (for example performance, API boundary, or template constraints).
	3. For behavior-sensitive duplicated logic, keep at least one regression test that exercises each path (for example mmap and stream via separate Catch2 `SECTION`s).
6. If a class has both a `.hpp` and `.cpp` file, put methods inside the `.cpp` and prefix the definition with `CSV_INLINE` to ensure proper single-header compilation (the macro is `inline` in the generated single-header and empty otherwise). Exceptions:
   - **Templates must stay in `.hpp`** — the compiler needs the definition at instantiation time. `init_from_stream` is the standing example.
   - **Trivial one-liner accessors** may be unconditionally `inline` in the header when the call overhead is measurable and the body will never change.
   - **Consolidation:** If a `.cpp` would be under ~100 lines *and* the split causes excessive comment duplication between the two files, prefer a single `.hpp` with definitions marked `inline` (free functions and methods alike). Do not use `CSV_INLINE` for consolidated definitions — `CSV_INLINE` expands to empty in multi-header mode, which would produce ODR violations across TUs. Do not consolidate just for brevity — only when duplication is the dominant cost.
7. **Prefer LF (`\n`) line endings for tracked source, test, CMake, and Markdown files.** When you touch a file with mixed line endings, normalize the edited file to LF unless there is a file-specific reason not to. Avoid introducing mixed CRLF/LF endings in the same file.

### Rules for Comments
1. **Always update or remove incorrect comments.**
2. **Don't reference internal functions in public API comments.** Public API docs should describe user-visible behavior and contracts; internal helper/function details belong in internal docs.
3. **Avoid meaningless @param and @return descriptions.** Do not add comments that could trivially be inferred by the function's name or other existing comments. When editing a function, remove any @param/@return descriptions that merely restate the function name or signature.
4. **Don't delete or simplify comments** unless allowed by other rules in this section. Comments in this codebase frequently encode concurrency invariants, non-obvious design decisions, and hard-won bug context that cannot be recovered from the code alone.
5. **Public API docs belong on declarations in `.hpp` files.** When a class has both a header and implementation file, put user-facing/Doxygen documentation on the declaration in the header. Keep the `.cpp` focused on implementation notes, concurrency invariants, performance rationale, and bug-history comments.
