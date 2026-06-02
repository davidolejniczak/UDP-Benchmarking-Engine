#!/usr/bin/env bash
set -e

CLIENT_DURATION="${1:-5}"
SERVER_DURATION=$((CLIENT_DURATION + 3))

docker compose down -v
CLIENT_DURATION="$CLIENT_DURATION" SERVER_DURATION="$SERVER_DURATION" docker compose up --build --exit-code-from benchmark benchmark
