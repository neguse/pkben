# pkben

SQLite Primary Key Benchmark - Compare SELECT performance across different PK types.

## PK Types

| Type | Size | Characteristics |
|------|------|-----------------|
| INT32 | 4B | Sequential integers |
| SNOWFLAKE | 8B | 64-bit timestamp-based ID |
| UUIDV4 | 36B | Random UUID (TEXT) |
| UUIDV7 | 36B | Timestamp-based UUID (TEXT), sortable |
| INT64RAND | 8B | Random 64-bit integers |

### WITHOUT ROWID Variants

Each type above also has a `_NR` (WITHOUT ROWID) variant that uses SQLite's `WITHOUT ROWID` optimization. This stores the table as a clustered index on the primary key, which can improve performance for certain access patterns.

| Type | Description |
|------|-------------|
| INT32_NR | INT32 with WITHOUT ROWID |
| SNOWFLAKE_NR | SNOWFLAKE with WITHOUT ROWID |
| UUIDV4_NR | UUIDV4 with WITHOUT ROWID |
| UUIDV7_NR | UUIDV7 with WITHOUT ROWID |
| INT64RAND_NR | INT64RAND with WITHOUT ROWID |

## Build

```bash
cmake -B build
cmake --build build --config Release
```

Windows (Visual Studio):
```bat
build.bat
```

## Usage

```
pkben [options]
  -n NUM    Total records (default: 1000000)
  -t SEC    Benchmark duration in seconds (default: 10)
  -b SIZE   Batch size for inserts (default: 10000)
  -d SIZE   Data blob size in bytes (default: 64)
  -h        Show help
```

Example:
```bash
./build/pkben -n 10000000 -t 30
```

## Output

```
=== Primary Key Benchmark ===
Records: 1000000, Benchmark: 10s, Batch: 10000, Data: 64B

--- INSERT Phase ---
Creating db/int32.db...
  Completed in 1.2 seconds (833333 inserts/sec)
...

--- SELECT Phase ---
Benchmarking INT32...
  Loaded 10 samples
  QPS: 234567
...

=== Results ===
PK Type         INSERT/sec      SELECT/sec
--------        ----------      ----------
INT32               833333          234567
SNOWFLAKE           812345          223456
UUIDV4              654321          187654
UUIDV7              678901          198765
```

## Results

![Performance Comparison](https://neguse.github.io/pkben/graphs/comparison.png)

![INSERT Performance](https://neguse.github.io/pkben/graphs/insert.png)

![SELECT Performance](https://neguse.github.io/pkben/graphs/select.png)

## Dependencies

- CMake
- C compiler (MSVC/GCC/Clang)
- SQLite3 amalgamation (included)
