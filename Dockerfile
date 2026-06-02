FROM ubuntu:24.04 AS build

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    ca-certificates \
    cmake \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY CMakeLists.txt ./
COPY src ./src
COPY tests ./tests

RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build -j \
    && ctest --test-dir build --output-on-failure

FROM ubuntu:24.04

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    redis-tools \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=build /app/build/marketdata_server /app/marketdata_server
COPY --from=build /app/build/marketdata_client /app/marketdata_client

CMD ["./marketdata_server", "--redis-host", "127.0.0.1"]
