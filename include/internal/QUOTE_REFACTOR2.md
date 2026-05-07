Replace the current std::string realized-quote sidecar with an arena-backed quote storage model, and simplify RawCSVField metadata.

Context:
A previous attempt replaced RawCSVData::double_quote_fields with std::string realized_quote_storage. That is directionally correct because quote realization should happen in the parser thread, not lazily through an unordered_map on the caller thread. However, std::string sidecar correctness depends on capacity/reallocation invariants and led to segfault/debug churn. We want an arena/block model instead.

Goal:
Implement parser-time doubled-quote realization using a per-RawCSVData quote arena with stable append-only storage.

Desired RawCSVField shape:
- uint32_t start
- uint32_t length
- small flags byte/bool
- If the realized-quote flag is false:
  - start/length refer to RawCSVData::data, with start relative to the row start as before.
- If the realized-quote flag is true:
  - start/length refer to RawCSVData::quote_arena.
- Keep length for realized fields. Do not use null termination as the length source.

Desired storage:
1. Extract the general stable-block append logic from RawCSVFieldList into an internal reusable block arena structure.
2. Rebuild RawCSVFieldList on top of that structure.
3. Add RawCSVQuoteArena or equivalent on RawCSVData, also backed by stable blocks.
4. Quote arena should support:
   - append a contiguous realized field byte range
   - return a compact uint32_t logical offset/start for RawCSVField
   - view(start, length) -> csv::string_view
   - stable views even as later quoted fields are appended
5. If a realized field does not fit in the remaining block, allocate a new block that fits.
6. If realized fields are consistently larger than the default block size, use a simple runtime growth heuristic. Keep it conservative so one giant field does not permanently explode memory usage.
7. Do not null-terminate realized fields unless there is a compelling internal reason. string_view length is authoritative.

Parser behavior:
1. Remove RawCSVData::double_quote_fields and synchronized lazy lookup logic.
2. Remove std::string realized_quote_storage.
3. During push_field(), if the field contains doubled quotes, compact it into RawCSVData::quote_arena.
4. Store quote-arena start/length in RawCSVField and set the realized flag.
5. Plain quoted fields without doubled quotes should continue to use raw source views.
6. CSVRow::get_field_impl() should only choose between raw backing storage and quote_arena.view(start, length). It should not perform quote realization.

Important cleanup:
- Remove pending_rows_ batching if it only exists to protect std::string sidecar stability.
- Restore direct parser behavior where no output sink means emitted rows are ignored, not buffered.
- Do not replace RawCSVFieldList with std::vector; vector-backed field metadata was previously tried and caused excessive malloc pressure.
- Do not mutate mmap/source buffers.
- Do not add public API.
- Keep mmap and stream behavior identical.

Tests:
Add or update focused tests for:
1. Doubled quotes unescape correctly.
2. Multiple doubled-quote fields in one row.
3. Quoted fields without doubled quotes still return views into raw row storage.
4. Doubled-quote realized fields return views not pointing into raw row storage.
5. Whitespace trimming applies after quote realization.
6. Doubled quotes across chunk boundaries work for both mmap and stream readers.
7. A string_view from an early realized quoted field remains valid after later realized quoted fields from the same chunk are parsed/appended.
8. Direct parser tests that construct parser helpers without output still behave as before.

Validation:
- Run the focused quote/read tests if possible.
- Run full tests if feasible.
- If benchmark execution is not feasible, do not update benchmark numbers.
- Update QUOTE_PERFORMANCE_TODO.md to reflect that unordered_map lazy realization has been replaced by parser-time arena realization, and leave benchmark follow-up items.
