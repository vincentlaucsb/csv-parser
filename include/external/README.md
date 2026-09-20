# Vendored classify_scalar

`classify_scalar.hpp` is an unchanged copy of the upstream public header.
Do not patch it here. Fix `vincentlaucsb/classify_scalar` first, run its
`python tools/version.py --patch` command, and review/test the upstream change.

Then sync an approved tag or commit from this repository's root:

```sh
python tools/sync_classify_scalar.py --ref <upstream-tag-or-commit>
python tools/sync_classify_scalar.py --check
```

The command resolves tags to a full commit, copies the header verbatim, and
records its version and SHA-256 in `classify_scalar.json`. CI compares both
the local checksum and the upstream bytes. `--check --offline` checks local
metadata without network access. `--source-repo <path>` reads committed bytes
from a local upstream clone instead of downloading them.

The **Sync classify_scalar** GitHub Actions workflow accepts the same ref and
opens an update PR without auto-merging. Enable **Allow GitHub Actions to create
and approve pull requests** in the repository's Actions settings. With the default
workflow token, GitHub may require **Approve workflows to run** on the resulting
PR. For checks that start automatically, optionally add a fine-grained bot token
as `VENDOR_SYNC_TOKEN`, scoped to this repository with Contents and Pull requests
write permissions. See [GitHub's workflow trigger rules](https://docs.github.com/en/actions/how-tos/write-workflows/choose-when-workflows-run/trigger-a-workflow).
No token is needed for the local sync command or the integrity check against
this public upstream repository.

The build-system adapter lives in `include/internal/CMakeLists.txt`: it passes
the CSV CMake probe's boolean result through the upstream-owned
`CLASSIFY_SCALAR_USE_STD_FLOAT_FROM_CHARS` macro. This keeps csv-specific names
out of the upstream header.
