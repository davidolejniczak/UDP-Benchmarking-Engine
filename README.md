# MarketData Server

C++20 UDP multicast ingest server for simulated market-data feeds. Four exchange clients publish multicast packets; the server receives each stream, pushes packet records through lock-free SPSC rings, and writes batches to Redis Streams.

## Architecture

```text
4 exchange clients -> UDP multicast -> receiver threads -> lock-free rings -> Redis writer -> Redis Streams
```

Default streams:

```text
239.10.0.1:9001 -> md:exchange:0
239.10.0.2:9002 -> md:exchange:1
239.10.0.3:9003 -> md:exchange:2
239.10.0.4:9004 -> md:exchange:3
```

Packet header:

```text
uint32_t exchange_id
uint64_t sequence_id
uint64_t send_timestamp_ns
```

## Build and Run

```bash
./build.sh
./run_redis.sh
./run_benchmark.sh 10
```

Example server output:

```text
received_pps=123456 redis_write_pps=120000
summary total_received=1234567 total_written_to_redis=1200000 avg_received_pps=... avg_redis_write_pps=... dropped_packets=...
```