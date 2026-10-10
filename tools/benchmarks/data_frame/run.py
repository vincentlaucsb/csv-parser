"""Run a warmed, alternating pair of DataFrame investigation executables."""

import argparse
import csv
import json
from pathlib import Path
import statistics
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--pairs", type=int, default=7)
    args = parser.parse_args()
    if args.pairs < 4:
        parser.error("at least four pairs are needed for quartile summaries")
    args.output.mkdir(parents=True, exist_ok=True)
    executables = {"baseline": args.baseline.resolve(), "candidate": args.candidate.resolve()}

    def run(side):
        return subprocess.run(
            [str(executables[side])], check=True, capture_output=True, text=True
        ).stdout

    for side in executables:
        (args.output / ("warmup-" + side + ".csv")).write_text(
            run(side), encoding="utf-8", newline="\n"
        )

    samples = []
    for pair in range(args.pairs):
        order = ("baseline", "candidate") if pair % 2 == 0 else ("candidate", "baseline")
        for side in order:
            for line in run(side).splitlines():
                metric, elapsed, checksum = line.split(",")
                samples.append(dict(pair=pair, side=side, metric=metric,
                                    ms=float(elapsed), checksum=int(checksum)))
            (args.output / "samples.json").write_text(
                json.dumps(samples, indent=2) + "\n", encoding="utf-8", newline="\n"
            )
            print("pair", pair, side, "finished", flush=True)

    summaries = []
    for metric in dict.fromkeys(sample["metric"] for sample in samples):
        selected = [sample for sample in samples if sample["metric"] == metric]
        if len({sample["checksum"] for sample in selected}) != 1:
            raise ValueError("checksum mismatch: " + metric)
        baseline = [sample["ms"] for sample in selected if sample["side"] == "baseline"]
        candidate = [sample["ms"] for sample in selected if sample["side"] == "candidate"]
        if len(baseline) != args.pairs or len(candidate) != args.pairs:
            raise ValueError("missing or duplicated metric: " + metric)
        before, after = statistics.median(baseline), statistics.median(candidate)
        before_quartiles, after_quartiles = statistics.quantiles(baseline), statistics.quantiles(candidate)
        summaries.append(dict(
            metric=metric, baseline_median=before, candidate_median=after,
            change_percent=(after / before - 1) * 100,
            baseline_min=min(baseline), baseline_max=max(baseline),
            candidate_min=min(candidate), candidate_max=max(candidate),
            baseline_iqr=before_quartiles[2] - before_quartiles[0],
            candidate_iqr=after_quartiles[2] - after_quartiles[0],
            checksum=selected[0]["checksum"],
        ))

    with (args.output / "summary.csv").open("w", encoding="utf-8", newline="") as output:
        writer = csv.DictWriter(output, fieldnames=list(summaries[0]), lineterminator="\n")
        writer.writeheader()
        writer.writerows(summaries)
    for summary in summaries:
        print(summary)


if __name__ == "__main__":
    main()
