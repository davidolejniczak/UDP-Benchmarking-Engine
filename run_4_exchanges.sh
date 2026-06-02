#!/usr/bin/env bash
set -e

CLIENT_DURATION="${1:-5}" PAYLOAD_SIZE="${2:-64}" docker compose up exchange0 exchange1 exchange2 exchange3
