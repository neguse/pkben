#!/usr/bin/env python3
import json
import os
import glob
import matplotlib.pyplot as plt
import numpy as np

def load_results(results_dir):
    data = []
    for path in glob.glob(os.path.join(results_dir, "*.json")):
        with open(path) as f:
            d = json.load(f)
            data.append(d)
    # レコード数で昇順ソート
    data.sort(key=lambda d: d["params"]["records"])
    return data

def format_num(n):
    if n >= 1_000_000:
        return f"{n // 1_000_000}M"
    elif n >= 1_000:
        return f"{n // 1_000}K"
    return str(n)

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
    record_labels = [format_num(n) for n in record_counts]
    n_records = len(record_counts)
    n_types = len(pk_types)

    x = np.arange(n_records)
    width = 0.8 / n_types

    # INSERT performance
    fig, ax = plt.subplots(figsize=(12, 6))
    for i, pk_type in enumerate(pk_types):
        values = []
        for d in data:
            for r in d["results"]:
                if r["type"] == pk_type:
                    values.append(r["insert"])
                    break
        offset = (i - n_types / 2 + 0.5) * width
        ax.bar(x + offset, values, width, label=pk_type)
    ax.set_xlabel("Record Count")
    ax.set_ylabel("INSERT/sec")
    ax.set_title("INSERT Performance by PK Type")
    ax.set_xticks(x)
    ax.set_xticklabels(record_labels)
    ax.legend()
    ax.grid(True, alpha=0.3, axis="y")
    fig.savefig(os.path.join(output_dir, "insert.png"), dpi=150, bbox_inches="tight")
    plt.close(fig)

    # SELECT performance
    fig, ax = plt.subplots(figsize=(12, 6))
    for i, pk_type in enumerate(pk_types):
        values = []
        for d in data:
            for r in d["results"]:
                if r["type"] == pk_type:
                    values.append(r["select"])
                    break
        offset = (i - n_types / 2 + 0.5) * width
        ax.bar(x + offset, values, width, label=pk_type)
    ax.set_xlabel("Record Count")
    ax.set_ylabel("SELECT/sec (QPS)")
    ax.set_title("SELECT Performance by PK Type")
    ax.set_xticks(x)
    ax.set_xticklabels(record_labels)
    ax.legend()
    ax.grid(True, alpha=0.3, axis="y")
    fig.savefig(os.path.join(output_dir, "select.png"), dpi=150, bbox_inches="tight")
    plt.close(fig)

    # Comparison charts for latest run (split into normal and WITHOUT ROWID)
    latest = data[-1]
    results = latest["results"]

    # 通常版とWITHOUT ROWID版に分割
    normal_results = [r for r in results if not r["type"].endswith("_NR")]
    norowid_results = [r for r in results if r["type"].endswith("_NR")]

    def plot_comparison(results_subset, filename, subtitle):
        if not results_subset:
            return
        types = [r["type"] for r in results_subset]
        n = len(types)
        fig, ax = plt.subplots(figsize=(10, 6))
        x = np.arange(n)
        width = 0.35
        insert_vals = [r["insert"] for r in results_subset]
        select_vals = [r["select"] for r in results_subset]
        ax.bar(x - width/2, insert_vals, width, label="INSERT/sec")
        ax.bar(x + width/2, select_vals, width, label="SELECT/sec")
        ax.set_xlabel("PK Type")
        ax.set_ylabel("Operations/sec")
        ax.set_title(f"Performance Comparison - {subtitle} (n={format_num(latest['params']['records'])})")
        ax.set_xticks(x)
        ax.set_xticklabels(types)
        ax.legend()
        ax.grid(True, alpha=0.3, axis="y")
        fig.savefig(os.path.join(output_dir, filename), dpi=150, bbox_inches="tight")
        plt.close(fig)

    plot_comparison(normal_results, "comparison.png", "Normal")
    plot_comparison(norowid_results, "comparison_norowid.png", "WITHOUT ROWID")

    print(f"Graphs saved to {output_dir}/")

if __name__ == "__main__":
    main()
