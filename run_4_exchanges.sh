#!/usr/bin/env bash
set -e

DURATION="${1:-10}"
PAYLOAD_SIZE="${2:-64}"

./build/marketdata_client --exchange-id 0 --group 239.10.0.1 --port 9001 --duration-sec "$DURATION" --payload-size "$PAYLOAD_SIZE" --rate 0 &
./build/marketdata_client --exchange-id 1 --group 239.10.0.2 --port 9002 --duration-sec "$DURATION" --payload-size "$PAYLOAD_SIZE" --rate 0 &
./build/marketdata_client --exchange-id 2 --group 239.10.0.3 --port 9003 --duration-sec "$DURATION" --payload-size "$PAYLOAD_SIZE" --rate 0 &
./build/marketdata_client --exchange-id 3 --group 239.10.0.4 --port 9004 --duration-sec "$DURATION" --payload-size "$PAYLOAD_SIZE" --rate 0 &

wait
