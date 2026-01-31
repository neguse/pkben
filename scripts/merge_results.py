#!/usr/bin/env python3
"""Merge individual benchmark results into combined JSON files per record count."""
import json
import os
import sys
from collections import defaultdict


def main():
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <artifacts_dir> <output_dir>")
        sys.exit(1)

    artifacts_dir = sys.argv[1]
    output_dir = sys.argv[2]
    os.makedirs(output_dir, exist_ok=True)

    # Group results by record count
    results_by_records = defaultdict(list)
    params_by_records = {}

    for dirname in os.listdir(artifacts_dir):
        dirpath = os.path.join(artifacts_dir, dirname)
        if not os.path.isdir(dirpath):
            continue

        for filename in os.listdir(dirpath):
            if not filename.endswith(".json"):
                continue

            filepath = os.path.join(dirpath, filename)
            with open(filepath) as f:
                data = json.load(f)

            records = data["params"]["records"]
            params_by_records[records] = data["params"]
            results_by_records[records].extend(data["results"])

    # Define the expected order of PK types
    pk_order = [
        "INT32", "SNOWFLAKE", "UUIDV4", "UUIDV7", "INT64RAND",
        "INT32_NR", "SNOWFLAKE_NR", "UUIDV4_NR", "UUIDV7_NR", "INT64RAND_NR"
    ]

    # Write merged results
    for records, results in results_by_records.items():
        # Sort results by PK type order
        results_dict = {r["type"]: r for r in results}
        sorted_results = [results_dict[pk] for pk in pk_order if pk in results_dict]

        merged = {
            "params": params_by_records[records],
            "results": sorted_results
        }

        output_path = os.path.join(output_dir, f"n{records}.json")
        with open(output_path, "w") as f:
            json.dump(merged, f)
        print(f"Wrote {output_path} with {len(sorted_results)} results")


if __name__ == "__main__":
    main()
