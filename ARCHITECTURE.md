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
- Queue synchronization details are maintained only in THREADSAFE_DEQUE_DESIGN.md to avoid duplication.
- Public API comments should remain user-facing and avoid references to internal helper/function details.
- Private member naming should prefer trailing underscores; when editing mixed-style code, normalize the touched region toward that convention.

