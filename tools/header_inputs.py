#!/usr/bin/env python3
"""Fingerprint distribution inputs and reject stale generated-header artifacts."""

import argparse
import hashlib
import json
from pathlib import Path


def source_tree_sha256(root):
    paths = [path for path in (root / "include").rglob("*")
             if path.is_file() and path.suffix in (".h", ".hpp", ".cpp")]
    paths.append(root / "single_header.json")
    digest = hashlib.sha256()
    for path in sorted(paths, key=lambda item: item.relative_to(root).as_posix()):
        relative = path.relative_to(root).as_posix()
        # Physical checkout newlines do not change these C++/JSON inputs.
        content = path.read_bytes().replace(b"\r\n", b"\n")
        content_hash = hashlib.sha256(content).hexdigest()
        digest.update(f"{relative}:{content_hash}\n".encode("utf-8"))
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--header", type=Path, required=True)
    args = parser.parse_args()
    header = args.header.resolve()
    manifest = json.loads(header.with_suffix(header.suffix + ".manifest.json").read_text(encoding="utf-8"))
    if manifest.get("sourceTreeSHA256") != source_tree_sha256(args.root.resolve()):
        raise SystemExit("Generated header inputs are stale; regenerate csv.hpp from this source tree")
    if manifest.get("sha256") != hashlib.sha256(header.read_bytes()).hexdigest():
        raise SystemExit("Generated header does not match its manifest checksum")
    print("Verified generated header and source input hashes")


if __name__ == "__main__":
    main()
