#!/usr/bin/env bash
set -e

redis-server --bind 127.0.0.1 --port 6379 --daemonize yes
