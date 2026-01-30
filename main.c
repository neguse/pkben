#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include "sqlite3.h"

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define mkdir_p(dir) _mkdir(dir)
#else
#include <sys/time.h>
#include <sys/stat.h>
#define mkdir_p(dir) mkdir(dir, 0755)
#endif

// Default parameters (can be overridden via command line)
static int64_t g_total_records = 1000000;
static int g_benchmark_seconds = 10;
static int g_batch_size = 10000;
static int g_data_size = 64;
static int g_sample_interval = 100000;
static int g_json_output = 0;

static int get_sample_count(void) { return (int)(g_total_records / g_sample_interval); }

typedef enum
{
    PK_INT32,
    PK_SNOWFLAKE,
    PK_UUIDV4,
    PK_UUIDV7
} pk_type_t;

static double get_time(void)
{
#ifdef _WIN32
    LARGE_INTEGER freq, counter;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart / (double)freq.QuadPart;
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1000000.0;
#endif
}

static uint32_t xorshift32(uint32_t *state)
{
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

// UUIDv4: xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx
// 4 = version, y = variant (8,9,a,b)
static void generate_uuid(char *buf, uint32_t *rng)
{
    static const char hex[] = "0123456789abcdef";
    static const char variant[] = "89ab";
    int pos = 0;
    for (int i = 0; i < 36; i++)
    {
        if (i == 8 || i == 13 || i == 18 || i == 23)
        {
            buf[pos++] = '-';
        }
        else if (i == 14)
        {
            buf[pos++] = '4';
        }
        else if (i == 19)
        {
            buf[pos++] = variant[xorshift32(rng) & 0x3];
        }
        else
        {
            buf[pos++] = hex[xorshift32(rng) & 0xF];
        }
    }
    buf[pos] = '\0';
}

// UUIDv7: timestamp-based, sortable
// Format: tttttttt-tttt-7ttt-Vrxx-xxxxxxxxxxxx
// t=timestamp, V=variant(8-b), r=random, x=random
static void generate_uuidv7(char *buf, int64_t *timestamp_ms, uint32_t *rng)
{
    static const char hex[] = "0123456789abcdef";
    int64_t ts = *timestamp_ms;
    int i;

    *timestamp_ms += 1 + (xorshift32(rng) % 3);

    // bytes 0-5: timestamp (48 bits = 6 bytes)
    // byte 6: version (4 bits) + rand_a high (4 bits)
    // byte 7: rand_a low (8 bits)
    // byte 8: variant (2 bits) + rand_b (6 bits)
    // bytes 9-15: rand_b (56 bits)

    uint8_t bytes[16];
    bytes[0] = (ts >> 40) & 0xFF;
    bytes[1] = (ts >> 32) & 0xFF;
    bytes[2] = (ts >> 24) & 0xFF;
    bytes[3] = (ts >> 16) & 0xFF;
    bytes[4] = (ts >> 8) & 0xFF;
    bytes[5] = ts & 0xFF;
    bytes[6] = 0x70 | (xorshift32(rng) & 0x0F);
    bytes[7] = xorshift32(rng) & 0xFF;
    bytes[8] = 0x80 | (xorshift32(rng) & 0x3F);
    for (i = 9; i < 16; i++)
    {
        bytes[i] = xorshift32(rng) & 0xFF;
    }

    // Format as UUID string
    int pos = 0;
    for (i = 0; i < 16; i++)
    {
        if (i == 4 || i == 6 || i == 8 || i == 10)
        {
            buf[pos++] = '-';
        }
        buf[pos++] = hex[(bytes[i] >> 4) & 0x0F];
        buf[pos++] = hex[bytes[i] & 0x0F];
    }
    buf[pos] = '\0';
}

// Snowflake ID: 1bit sign + 41bit timestamp + 10bit machine + 12bit sequence
#define SNOWFLAKE_EPOCH 1288834974657LL // Twitter epoch (2010-11-04)
#define SNOWFLAKE_MACHINE_BITS 10
#define SNOWFLAKE_SEQUENCE_BITS 12
#define SNOWFLAKE_SEQUENCE_MASK ((1 << SNOWFLAKE_SEQUENCE_BITS) - 1)

typedef struct
{
    int64_t last_timestamp;
    int32_t sequence;
    int32_t machine_id;
} snowflake_gen_t;

static int64_t generate_snowflake(snowflake_gen_t *gen, uint32_t *rng)
{
    int64_t advance = 1 + (xorshift32(rng) % 5);
    gen->last_timestamp += advance;

    if (xorshift32(rng) % 10 == 0)
    {
        gen->sequence = (gen->sequence + 1) & SNOWFLAKE_SEQUENCE_MASK;
    }
    else
    {
        gen->sequence = xorshift32(rng) & SNOWFLAKE_SEQUENCE_MASK;
    }

    if (xorshift32(rng) % 1000 == 0)
    {
        gen->machine_id = xorshift32(rng) & ((1 << SNOWFLAKE_MACHINE_BITS) - 1);
    }

    return ((gen->last_timestamp - SNOWFLAKE_EPOCH) << (SNOWFLAKE_MACHINE_BITS + SNOWFLAKE_SEQUENCE_BITS)) |
           ((int64_t)gen->machine_id << SNOWFLAKE_SEQUENCE_BITS) |
           gen->sequence;
}

static void exec_sql(sqlite3 *db, const char *sql)
{
    char *err = NULL;
    if (sqlite3_exec(db, sql, NULL, NULL, &err) != SQLITE_OK)
    {
        fprintf(stderr, "SQL error: %s\n%s\n", err, sql);
        sqlite3_free(err);
        exit(1);
    }
}

static double create_database(const char *filename, pk_type_t pk_type)
{
    sqlite3 *db;
    if (!g_json_output)
        printf("Creating %s...\n", filename);

    if (sqlite3_open(filename, &db) != SQLITE_OK)
    {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        exit(1);
    }

    exec_sql(db, "PRAGMA journal_mode=WAL");
    exec_sql(db, "PRAGMA synchronous=OFF");
    exec_sql(db, "PRAGMA cache_size=-2000000");

    const char *pk_def;
    switch (pk_type)
    {
    case PK_INT32:
    case PK_SNOWFLAKE:
        pk_def = "INTEGER PRIMARY KEY";
        break;
    case PK_UUIDV4:
    case PK_UUIDV7:
        pk_def = "TEXT PRIMARY KEY";
        break;
    }

    char sql[256];
    snprintf(sql, sizeof(sql), "CREATE TABLE IF NOT EXISTS items (id %s, data BLOB)", pk_def);
    exec_sql(db, sql);
    exec_sql(db, "CREATE TABLE IF NOT EXISTS pk_samples (id INTEGER PRIMARY KEY, pk_value)");

    int64_t existing = 0;
    sqlite3_stmt *count_stmt;
    sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM items", -1, &count_stmt, NULL);
    if (sqlite3_step(count_stmt) == SQLITE_ROW)
    {
        existing = sqlite3_column_int64(count_stmt, 0);
    }
    sqlite3_finalize(count_stmt);

    if (existing >= g_total_records)
    {
        if (!g_json_output)
            printf("  Already has %lld records, skipping.\n", (long long)existing);
        sqlite3_close(db);
        return -1.0;
    }

    sqlite3_stmt *insert_stmt;
    sqlite3_stmt *sample_stmt;
    sqlite3_prepare_v2(db, "INSERT INTO items (id, data) VALUES (?, ?)", -1, &insert_stmt, NULL);
    sqlite3_prepare_v2(db, "INSERT OR REPLACE INTO pk_samples (id, pk_value) VALUES (?, ?)", -1, &sample_stmt, NULL);

    uint8_t *dummy_data = (uint8_t *)malloc(g_data_size);
    memset(dummy_data, 0xAB, g_data_size);

    uint32_t rng_state = 12345;
    char uuid_buf[37];
    snowflake_gen_t snowflake_gen = {
        .last_timestamp = 1700000000000LL,
        .sequence = 0,
        .machine_id = 1};
    int64_t current_snowflake = 0;
    int64_t uuidv7_timestamp = 1700000000000LL;
    double start_time = get_time();
    int64_t last_report = 0;

    for (int64_t batch_start = existing; batch_start < g_total_records; batch_start += g_batch_size)
    {
        exec_sql(db, "BEGIN");

        int64_t batch_end = batch_start + g_batch_size;
        if (batch_end > g_total_records)
            batch_end = g_total_records;

        for (int64_t i = batch_start; i < batch_end; i++)
        {
            switch (pk_type)
            {
            case PK_INT32:
                sqlite3_bind_int(insert_stmt, 1, (int32_t)i);
                break;
            case PK_SNOWFLAKE:
                current_snowflake = generate_snowflake(&snowflake_gen, &rng_state);
                sqlite3_bind_int64(insert_stmt, 1, current_snowflake);
                break;
            case PK_UUIDV4:
                generate_uuid(uuid_buf, &rng_state);
                sqlite3_bind_text(insert_stmt, 1, uuid_buf, -1, SQLITE_TRANSIENT);
                break;
            case PK_UUIDV7:
                generate_uuidv7(uuid_buf, &uuidv7_timestamp, &rng_state);
                sqlite3_bind_text(insert_stmt, 1, uuid_buf, -1, SQLITE_TRANSIENT);
                break;
            }
            sqlite3_bind_blob(insert_stmt, 2, dummy_data, g_data_size, SQLITE_STATIC);
            sqlite3_step(insert_stmt);
            sqlite3_reset(insert_stmt);

            if (i % g_sample_interval == 0)
            {
                int sample_idx = (int)(i / g_sample_interval);
                sqlite3_bind_int(sample_stmt, 1, sample_idx);
                switch (pk_type)
                {
                case PK_INT32:
                    sqlite3_bind_int(sample_stmt, 2, (int32_t)i);
                    break;
                case PK_SNOWFLAKE:
                    sqlite3_bind_int64(sample_stmt, 2, current_snowflake);
                    break;
                case PK_UUIDV4:
                case PK_UUIDV7:
                    sqlite3_bind_text(sample_stmt, 2, uuid_buf, -1, SQLITE_TRANSIENT);
                    break;
                }
                sqlite3_step(sample_stmt);
                sqlite3_reset(sample_stmt);
            }
        }

        exec_sql(db, "COMMIT");

        if (!g_json_output && batch_end - last_report >= 10000000)
        {
            double elapsed = get_time() - start_time;
            double rate = (batch_end - existing) / elapsed / 1000000.0;
            printf("  %lld / %lld records (%.2f M/sec)\n", (long long)batch_end, (long long)g_total_records, rate);
            last_report = batch_end;
        }
    }

    sqlite3_finalize(insert_stmt);
    sqlite3_finalize(sample_stmt);
    free(dummy_data);

    double elapsed = get_time() - start_time;
    double insert_rate = (g_total_records - existing) / elapsed;
    if (!g_json_output)
        printf("  Completed in %.1f seconds (%.0f inserts/sec)\n", elapsed, insert_rate);

    sqlite3_close(db);
    return insert_rate;
}

typedef struct
{
    int64_t *int_samples;
    char (*uuid_samples)[37];
    int count;
} sample_data_t;

static void load_samples(const char *filename, pk_type_t pk_type, sample_data_t *samples)
{
    sqlite3 *db;
    sqlite3_open(filename, &db);

    samples->count = get_sample_count();
    if (pk_type == PK_UUIDV4 || pk_type == PK_UUIDV7)
    {
        samples->uuid_samples = malloc(get_sample_count() * 37);
        samples->int_samples = NULL;
    }
    else
    {
        samples->int_samples = malloc(get_sample_count() * sizeof(int64_t));
        samples->uuid_samples = NULL;
    }

    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, "SELECT pk_value FROM pk_samples ORDER BY id", -1, &stmt, NULL);

    int idx = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && idx < get_sample_count())
    {
        if (pk_type == PK_UUIDV4 || pk_type == PK_UUIDV7)
        {
            const char *text = (const char *)sqlite3_column_text(stmt, 0);
            strncpy(samples->uuid_samples[idx], text, 36);
            samples->uuid_samples[idx][36] = '\0';
        }
        else
        {
            samples->int_samples[idx] = sqlite3_column_int64(stmt, 0);
        }
        idx++;
    }
    samples->count = idx;

    sqlite3_finalize(stmt);
    sqlite3_close(db);
}

static void free_samples(sample_data_t *samples)
{
    free(samples->int_samples);
    free(samples->uuid_samples);
}

static double run_benchmark(const char *filename, pk_type_t pk_type, sample_data_t *samples)
{
    sqlite3 *db;
    sqlite3_open(filename, &db);

    exec_sql(db, "PRAGMA cache_size=-2000000");

    sqlite3_stmt *select_stmt;
    sqlite3_prepare_v2(db, "SELECT data FROM items WHERE id = ?", -1, &select_stmt, NULL);

    uint32_t rng = (uint32_t)time(NULL);
    int64_t query_count = 0;
    double start = get_time();
    double end_time = start + g_benchmark_seconds;

    while (get_time() < end_time)
    {
        int idx = xorshift32(&rng) % samples->count;

        if (pk_type == PK_UUIDV4 || pk_type == PK_UUIDV7)
        {
            sqlite3_bind_text(select_stmt, 1, samples->uuid_samples[idx], -1, SQLITE_STATIC);
        }
        else
        {
            sqlite3_bind_int64(select_stmt, 1, samples->int_samples[idx]);
        }

        if (sqlite3_step(select_stmt) == SQLITE_ROW)
        {
            query_count++;
        }
        sqlite3_reset(select_stmt);
    }

    double elapsed = get_time() - start;
    double qps = query_count / elapsed;

    sqlite3_finalize(select_stmt);
    sqlite3_close(db);

    return qps;
}

static void print_usage(const char *prog)
{
    printf("Usage: %s [options]\n", prog);
    printf("Options:\n");
    printf("  -n NUM    Total records (default: %lld)\n", (long long)g_total_records);
    printf("  -t SEC    Benchmark duration in seconds (default: %d)\n", g_benchmark_seconds);
    printf("  -b SIZE   Batch size for inserts (default: %d)\n", g_batch_size);
    printf("  -d SIZE   Data blob size in bytes (default: %d)\n", g_data_size);
    printf("  --json    Output results as JSON\n");
    printf("  -h        Show this help\n");
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-n") == 0 && i + 1 < argc)
        {
            g_total_records = atoll(argv[++i]);
        }
        else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc)
        {
            g_benchmark_seconds = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "-b") == 0 && i + 1 < argc)
        {
            g_batch_size = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "-d") == 0 && i + 1 < argc)
        {
            g_data_size = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--json") == 0)
        {
            g_json_output = 1;
        }
        else if (strcmp(argv[i], "-h") == 0)
        {
            print_usage(argv[0]);
            return 0;
        }
        else
        {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    if (!g_json_output)
    {
        printf("=== Primary Key Benchmark ===\n");
        printf("Records: %lld, Benchmark: %ds, Batch: %d, Data: %dB\n\n",
               (long long)g_total_records, g_benchmark_seconds, g_batch_size, g_data_size);
    }

    mkdir_p("db");

    struct
    {
        const char *name;
        const char *filename;
        pk_type_t pk_type;
    } tests[] = {
        {"INT32", "db/int32.db", PK_INT32},
        {"SNOWFLAKE", "db/snowflake.db", PK_SNOWFLAKE},
        {"UUIDV4", "db/uuidv4.db", PK_UUIDV4},
        {"UUIDV7", "db/uuidv7.db", PK_UUIDV7},
    };
    int num_tests = sizeof(tests) / sizeof(tests[0]);

    double insert_results[4];
    double select_results[4];

    if (!g_json_output)
        printf("--- INSERT Phase ---\n");
    for (int i = 0; i < num_tests; i++)
    {
        insert_results[i] = create_database(tests[i].filename, tests[i].pk_type);
    }

    if (!g_json_output)
        printf("\n--- SELECT Phase ---\n");
    for (int i = 0; i < num_tests; i++)
    {
        if (!g_json_output)
            printf("Benchmarking %s...\n", tests[i].name);
        sample_data_t samples = {0};
        load_samples(tests[i].filename, tests[i].pk_type, &samples);
        if (!g_json_output)
            printf("  Loaded %d samples\n", samples.count);
        select_results[i] = run_benchmark(tests[i].filename, tests[i].pk_type, &samples);
        if (!g_json_output)
            printf("  QPS: %.0f\n", select_results[i]);
        free_samples(&samples);
    }

    if (g_json_output)
    {
        printf("{\"params\":{\"records\":%lld,\"benchmark_sec\":%d,\"batch\":%d,\"data_size\":%d},\"results\":[",
               (long long)g_total_records, g_benchmark_seconds, g_batch_size, g_data_size);
        for (int i = 0; i < num_tests; i++)
        {
            printf("%s{\"type\":\"%s\",\"insert\":%.0f,\"select\":%.0f}",
                   i > 0 ? "," : "", tests[i].name, insert_results[i], select_results[i]);
        }
        printf("]}\n");
    }
    else
    {
        printf("\n=== Results ===\n");
        printf("%-12s %15s %15s\n", "PK Type", "INSERT/sec", "SELECT/sec");
        printf("%-12s %15s %15s\n", "--------", "----------", "----------");
        for (int i = 0; i < num_tests; i++)
        {
            if (insert_results[i] > 0)
            {
                printf("%-12s %15.0f %15.0f\n", tests[i].name, insert_results[i], select_results[i]);
            }
            else
            {
                printf("%-12s %15s %15.0f\n", tests[i].name, "(cached)", select_results[i]);
            }
        }
    }

    return 0;
}
