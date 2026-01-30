#!/usr/bin/env python3
import json
import os
import glob
import matplotlib.pyplot as plt

def load_results(results_dir):
    data = []
    for path in sorted(glob.glob(os.path.join(results_dir, "*.json"))):
        with open(path) as f:
            d = json.load(f)
            data.append(d)
    return data

def main():
    results_dir = "results"
    output_dir = "graphs"
    os.makedirs(output_dir, exist_ok=True)

    data = load_results(results_dir)
    if not data:
        print("No results found")
        return

    pk_types = [r["type"] for r in data[0]["results"]]
    record_counts = [d["params"]["records"] for d in data]

    # INSERT performance
    fig, ax = plt.subplots(figsize=(10, 6))
    for pk_type in pk_types:
        values = []
        for d in data:
            for r in d["results"]:
                if r["type"] == pk_type:
                    values.append(r["insert"])
                    break
        ax.plot(record_counts, values, marker="o", label=pk_type)
    ax.set_xlabel("Record Count")
    ax.set_ylabel("INSERT/sec")
    ax.set_title("INSERT Performance by PK Type")
    ax.legend()
    ax.grid(True, alpha=0.3)
    ax.set_xscale("log")
    fig.savefig(os.path.join(output_dir, "insert.png"), dpi=150, bbox_inches="tight")
    plt.close(fig)

    # SELECT performance
    fig, ax = plt.subplots(figsize=(10, 6))
    for pk_type in pk_types:
        values = []
        for d in data:
            for r in d["results"]:
                if r["type"] == pk_type:
                    values.append(r["select"])
                    break
        ax.plot(record_counts, values, marker="o", label=pk_type)
    ax.set_xlabel("Record Count")
    ax.set_ylabel("SELECT/sec (QPS)")
    ax.set_title("SELECT Performance by PK Type")
    ax.legend()
    ax.grid(True, alpha=0.3)
    ax.set_xscale("log")
    fig.savefig(os.path.join(output_dir, "select.png"), dpi=150, bbox_inches="tight")
    plt.close(fig)

    # Bar chart for latest run
    latest = data[-1]
    fig, ax = plt.subplots(figsize=(10, 6))
    x = range(len(pk_types))
    width = 0.35
    insert_vals = [r["insert"] for r in latest["results"]]
    select_vals = [r["select"] for r in latest["results"]]
    ax.bar([i - width/2 for i in x], insert_vals, width, label="INSERT/sec")
    ax.bar([i + width/2 for i in x], select_vals, width, label="SELECT/sec")
    ax.set_xlabel("PK Type")
    ax.set_ylabel("Operations/sec")
    ax.set_title(f"Performance Comparison (n={latest['params']['records']})")
    ax.set_xticks(x)
    ax.set_xticklabels(pk_types)
    ax.legend()
    ax.grid(True, alpha=0.3, axis="y")
    fig.savefig(os.path.join(output_dir, "comparison.png"), dpi=150, bbox_inches="tight")
    plt.close(fig)

    print(f"Graphs saved to {output_dir}/")

if __name__ == "__main__":
    main()
