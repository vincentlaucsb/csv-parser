# Codecov Agent Notes

Use these notes when reviewing coverage with Codecov. This is internal
AI-agent workflow guidance, not public project documentation.

## API Version

These instructions reference the **Codecov API v2**.

Base URL used here:

```text
https://api.codecov.io/api/v2/
```

If Codecov introduces a v3 API or changes v2 response shapes, verify the
current API docs before reusing these endpoints.

## Coverage Review Workflow

Prefer the Codecov API over screenshots or visual inspection.

1. Query the branch-aware directory rollup first.

   Example:

   ```text
   https://api.codecov.io/api/v2/github/{owner}/repos/{repo}/report/tree?branch={branch}&path={path}&depth=2
   ```

2. Drill into specific file reports for low-coverage or high-risk files.

   Example:

   ```text
   https://api.codecov.io/api/v2/github/{owner}/repos/{repo}/file_report/{url_encoded_path}?branch={branch}
   ```

3. Record the `commit_sha` from file reports.

   Codecov may lag behind the local workspace or the latest pushed branch tip.
   Do not assume Codecov line data includes unpushed or very recent local edits.

4. Map Codecov line states back to local source before recommending tests.

   The file report `line_coverage` entries are shaped like:

   ```text
   [line_number, state]
   ```

   In the v2 responses observed for this repo:

   - `0` means covered
   - `1` means missed
   - `2` means partial

5. Classify uncovered lines before proposing work.

   Useful categories:

   - meaningful behavioral paths
   - public API error paths
   - private defensive guards
   - debug-only assertions
   - template/generated/noise lines

6. Do not chase 100% coverage mechanically.

   Recommend new tests only when they improve release confidence, protect a
   non-obvious invariant, or cover a realistic user-visible behavior.

