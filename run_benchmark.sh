#!/usr/bin/env bash
set -e

DURATION="${1:-10}"
SERVER_DURATION=$((DURATION + 3))

./build.sh
./run_redis.sh

redis-cli DEL md:exchange:0 md:exchange:1 md:exchange:2 md:exchange:3

./build/marketdata_server --duration-sec "$SERVER_DURATION" &
server_pid=$!

sleep 1
./run_4_exchanges.sh "$DURATION" 64
wait "$server_pid"
