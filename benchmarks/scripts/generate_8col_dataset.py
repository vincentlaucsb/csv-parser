import argparse
import csv
from pathlib import Path


def standard_city(city: str, i: int) -> str:
    del i
    return city


def wide_city(city: str, i: int) -> str:
    corridor = ["north", "south", "east", "west"][i % 4]
    region = ["metro", "distribution", "regional", "express"][i % 4]
    return f"{city}-{corridor}-{region}-hub-{(i % 13) + 1}"


def standard_state(state: str, i: int) -> str:
    del i
    return state


def wide_state(state: str, i: int) -> str:
    division = ["ops", "sales", "field", "intake"][i % 4]
    return f"{state}-{division}-{(i % 9) + 1}"


def standard_category(category: str, i: int) -> str:
    del i
    return category


def wide_category(category: str, i: int) -> str:
    flavor = ["priority", "standard", "backlog", "rework"][i % 4]
    return f"{category}-{flavor}-segment-{(i % 17) + 1}"


def clean_note(i: int) -> str:
    return f"row-{i}-value-{(i * 31) % 7919}"


def quoted_note(i: int, city: str, category: str) -> str:
    phase = i % 5

    if phase == 0:
        return f'{city} "{category}" intake, batch {i % 17}'

    if phase == 1:
        return f'Customer said "ship ASAP", priority lane {i % 23}'

    if phase == 2:
        return f'comma-heavy "{city}, {category}" note, seq {(i * 19) % 101}'

    if phase == 3:
        return f'embedded ""style"" quote marker for {category}, id {i}'

    return f'plain text with embedded "quotes" and comma, id {i}'


def multiline_note(i: int, city: str, category: str) -> str:
    phase = i % 6

    if phase == 0:
        return f'{city} "{category}" intake\nfollow-up batch {i % 17}'

    if phase == 1:
        return f'Customer said "ship ASAP", priority lane {i % 23}'

    if phase == 2:
        return f"line one for {city}\nline two with comma, batch {(i * 19) % 101}"

    if phase == 3:
        return f'multi-line "{category}" summary\nquoted detail "{i % 29}"\ncloseout'

    if phase == 4:
        return f"notes for {city}, zone {(i % 8) + 1}\nwindow seat requested"

    return f'plain text with embedded "quotes" and comma, id {i}'


def widen_note(note: str, i: int, city: str, category: str) -> str:
    detail = (
        f" | region={city} category={category} batch={(i * 7) % 113} "
        f"window={(i % 5) + 1} owner=ops-{(i % 11) + 1}"
    )
    tail = " | narrative=" + ("detail-block-" + str(i % 23) + " ") * 6
    return note + detail + tail.strip()


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate an 8-column benchmark CSV dataset.")
    parser.add_argument("output", type=Path)
    size_or_rows = parser.add_mutually_exclusive_group()
    size_or_rows.add_argument("--rows", type=int, default=500_000)
    size_or_rows.add_argument("--target-size-gb", type=float, help="Generate rows until the file reaches approximately this size.")
    parser.add_argument(
        "--profile",
        choices=("clean", "quoted", "multiline", "realistic"),
        default="clean",
        help="Choose a benchmark payload profile.",
    )
    parser.add_argument(
        "--row-shape",
        choices=("standard", "wide"),
        default="standard",
        help="Choose whether to generate standard-width or wider rows.",
    )
    parser.add_argument("--force", action="store_true", help="Regenerate the file even if it already exists.")
    args = parser.parse_args()

    if args.output.exists() and not args.force:
        print(f"Keeping existing dataset: {args.output}")
        return 0

    args.output.parent.mkdir(parents=True, exist_ok=True)

    cities = ["Phoenix", "Seattle", "Austin", "Raleigh", "Denver", "Chicago", "Boston", "Fresno"]
    states = ["AZ", "WA", "TX", "NC", "CO", "IL", "MA", "CA"]
    categories = ["alpha", "beta", "gamma", "delta"]
    profile = "multiline" if args.profile == "realistic" else args.profile
    city_builder = wide_city if args.row_shape == "wide" else standard_city
    state_builder = wide_state if args.row_shape == "wide" else standard_state
    category_builder = wide_category if args.row_shape == "wide" else standard_category
    target_size_bytes = None

    if args.target_size_gb is not None:
        target_size_bytes = int(args.target_size_gb * 1024 * 1024 * 1024)
        if target_size_bytes <= 0:
            raise ValueError("--target-size-gb must be greater than zero.")

    with args.output.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(["id", "city", "state", "category", "amount", "quantity", "flag", "note"])

        i = 0
        while True:
            city = city_builder(cities[i % len(cities)], i)
            state = state_builder(states[i % len(states)], i)
            category = category_builder(categories[i % len(categories)], i)
            if profile == "clean":
                note = clean_note(i)
            elif profile == "quoted":
                note = quoted_note(i, city, category)
            else:
                note = multiline_note(i, city, category)

            if args.row_shape == "wide":
                note = widen_note(note, i, city, category)

            writer.writerow(
                [
                    i + 1,
                    city,
                    state,
                    category,
                    (i * 17) % 1_000_000,
                    (i % 97) + 1,
                    "Y" if i % 2 == 0 else "N",
                    note,
                ]
            )

            i += 1

            if target_size_bytes is None:
                if i >= args.rows:
                    break
            elif handle.tell() >= target_size_bytes:
                break

    if target_size_bytes is None:
        print(f"Wrote {profile} {args.row_shape} dataset with {i} rows: {args.output}")
    else:
        print(f"Wrote {profile} {args.row_shape} dataset with {i} rows to approximately {args.target_size_gb} GiB: {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
