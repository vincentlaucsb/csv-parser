#!/usr/bin/env python3
"""Generate and verify the optimized distribution using a Clang-enabled tool."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

from header_inputs import source_tree_sha256


def run(*args):
    subprocess.run([str(arg) for arg in args], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--build-dir", type=Path, default=Path("build/optimized-header"))
    parser.add_argument("--compiler", default="clang++-20")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    tool = args.executable.resolve()
    output = args.output.resolve()
    work = args.build_dir.resolve()
    version = subprocess.check_output([str(tool), "--version"], text=True).strip()
    if version != "single_header 1.1.0":
        raise SystemExit(f"Expected single_header 1.1.0, got {version!r}")
    source_hash = source_tree_sha256(root)
    output.parent.mkdir(parents=True, exist_ok=True)
    work.mkdir(parents=True, exist_ok=True)
    analysis = work / "analysis"
    run("cmake", "-S", root, "-B", analysis, "-G", "Ninja",
        "-DCMAKE_BUILD_TYPE=Release", f"-DCMAKE_CXX_COMPILER={args.compiler}",
        "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
        "-DCSV_BUILD_TESTS=OFF", "-DCSV_BUILD_PROGRAMS=OFF",
        "-DCSV_SINGLE_HEADER_PREGENERATED_HEADER=",
        f"-DCSV_SINGLE_HEADER_EXECUTABLE={tool}", "-DCSV_SINGLE_HEADER_EXTRA_ARGS=")
    commands = json.loads((analysis / "compile_commands.json").read_text())
    commands = [entry for entry in commands
                if "CMakeFiles/csv.dir/" in entry.get("output", "").replace("\\", "/")
                or "CMakeFiles/csv.dir/" in entry.get("command", "").replace("\\", "/")]
    sources = {path.resolve() for path in (root / "include").rglob("*.cpp")}
    if len(commands) != len(sources) or {Path(entry["file"]).resolve() for entry in commands} != sources:
        raise SystemExit("Expected exactly one csv target command per implementation source")
    # Cover source declarations in both platform branches and both exception
    # modes. Body-only alternatives share their enclosing inline definition.
    cl_driver = Path(args.compiler).name.lower() in ("cl", "cl.exe", "clang-cl", "clang-cl.exe")
    contexts = []
    for entry in commands:
        for emscripten in (False, True):
            for exceptions in (False, True):
                if cl_driver:
                    flags = ["/EHsc" if exceptions else "/EHs-c-", "/U_HAS_EXCEPTIONS",
                             f"/D_HAS_EXCEPTIONS={int(exceptions)}"]
                else:
                    flags = ["-fexceptions" if exceptions else "-fno-exceptions"]
                if emscripten:
                    flags.append(("/D" if cl_driver else "-D") + "__EMSCRIPTEN__=1")
                context = dict(entry)
                if "arguments" in context:
                    arguments = list(context["arguments"])
                    index = next((i for i, value in enumerate(arguments) if value in ("-c", "/c")), 1)
                    context["arguments"] = arguments[:index] + flags + arguments[index:]
                else:
                    command = context["command"]
                    action = re.search(r"(?<!\S)(?:-c|/c)(?=\s)", command)
                    if not action:
                        raise SystemExit("Cannot identify compiler action in compilation database")
                    context["command"] = command[:action.start()] + " ".join(flags) + " " + command[action.start():]
                contexts.append(context)
    database = work / "contexts"
    database.mkdir(exist_ok=True)
    (database / "compile_commands.json").write_text(json.dumps(contexts, indent=2) + "\n")
    baseline = work / "unoptimized.hpp"
    common = [tool, "--config", root / "single_header.json", "--inline-functions",
              "--compilation-database", database]
    baseline_report_path = work / "unstitched-inline.json"
    run(*common, "--output", baseline, "--function-stitching", "off",
        "--no-deduplicate-includes", "--no-merge-namespaces",
        "--stitching-report", baseline_report_path)
    baseline_report = json.loads(baseline_report_path.read_text(encoding="utf-8"))
    if (baseline_report.get("inlineFunctions") is not True or baseline_report["baseline"] != "valid"
            or baseline_report["work"]["finalParses"] != len(contexts)
            or baseline_report["counts"]["inlineInsertions"] < 1):
        raise SystemExit("Unstitched definitions did not receive complete inline validation")
    report_path = output.with_suffix(output.suffix + ".stitching.json")
    extra = ["--function-stitching", "all", "--inline-functions",
             "--compilation-database", str(database), "--deduplicate-includes",
             "--include-guard", "CSV_HPP", "--merge-namespaces",
             "--stitching-report", str(report_path)]
    run("cmake", "-S", root, "-B", analysis,
        "-DCSV_SINGLE_HEADER_EXTRA_ARGS=" + ";".join(extra))
    generated = analysis / "single_include_generated" / "csv.hpp"
    # Always validate a fresh distribution, even when a compile context changed
    # without changing source bytes or the generator command line.
    generated.unlink(missing_ok=True)
    run("cmake", "--build", analysis, "--target", "generate_single_header")
    report = json.loads(report_path.read_text())
    counts = report["counts"]
    artifact = generated.read_bytes()
    if (report["baseline"] != "valid" or report["mode"] != "all" or report.get("inlineFunctions") is not True
            or counts["acceptedFree"] < 1 or counts["acceptedMethods"] < 1
            or report["work"]["finalParses"] != len(contexts)
            or report["bytes"]["stitched"] != len(artifact)):
        raise SystemExit("Incomplete compiler validation or missing stitching optimizations")
    if not any(candidate["name"] == "csv::CSVReader::read_csv" and candidate["reasonCode"] == "accepted"
               for candidate in report["candidates"]):
        raise SystemExit("Exception-guard macros prevented read_csv from being stitched")
    text = artifact.decode("utf-8")
    for header in ("algorithm", "vector", "string"):
        if len(re.findall(r"^\s*#\s*include\s*<" + header + r">", text, re.MULTILINE)) != 1:
            raise SystemExit(f"Expected one deduplicated <{header}> include")
    namespace_pattern = r"\bnamespace\s+[A-Za-z_]\w*\s*\{"
    before_namespaces = len(re.findall(namespace_pattern, baseline.read_text(encoding="utf-8")))
    after_namespaces = len(re.findall(namespace_pattern, text))
    if after_namespaces >= before_namespaces:
        raise SystemExit("Expected recursive namespace merging to reduce namespace openings")
    if source_tree_sha256(root) != source_hash:
        raise SystemExit("Source inputs changed during generation")
    output.write_bytes(artifact)
    manifest = {
        "generator": version,
        "generatorSHA256": hashlib.sha256(tool.read_bytes()).hexdigest(),
        "optimizations": ["inline-functions", "function-stitching:all", "deduplicate-includes", "merge-namespaces", "cleanup"],
        "sha256": hashlib.sha256(artifact).hexdigest(),
        "sourceTreeSHA256": source_hash,
        "bytesBeforeOptionalOptimizations": baseline.stat().st_size,
        "bytes": len(artifact),
        "namespaceOpeningsBefore": before_namespaces,
        "namespaceOpeningsAfter": after_namespaces,
        "stitching": counts,
        "validatedSourceContexts": len(contexts),
        "unstitchedInlineInsertions": baseline_report["counts"]["inlineInsertions"],
    }
    output.with_suffix(output.suffix + ".manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    output.with_suffix(output.suffix + ".sha256").write_text(f"{manifest['sha256']}  {output.name}\n")
    print(json.dumps(manifest, indent=2))


if __name__ == "__main__":
    main()
