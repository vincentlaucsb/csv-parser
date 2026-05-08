@page internal_deep_dives Internal / Deep Dives

# Internal / Deep Dives

These notes document parser internals, concurrency invariants, and refactor
plans. They are useful when debugging or changing csv-parser itself, but most
users can stay with the README and public API reference.

- @subpage internal_architecture
- @subpage csv_field_journey
- @subpage threadsafe_deque_design
- @subpage bom_stripping_refactor
- @subpage thread_pool_refactor
