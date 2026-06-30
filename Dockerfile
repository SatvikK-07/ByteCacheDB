FROM ubuntu:24.04 AS builder

RUN apt-get update \
    && apt-get install -y --no-install-recommends build-essential cmake \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build --parallel

FROM ubuntu:24.04
RUN useradd --create-home --uid 10001 bytecache
COPY --from=builder /src/build/bytecachedb /usr/local/bin/bytecachedb
RUN mkdir -p /data && chown bytecache:bytecache /data
USER bytecache
EXPOSE 6379
VOLUME ["/data"]
ENTRYPOINT ["bytecachedb"]
CMD ["--host", "0.0.0.0", "--port", "6379", "--threads", "8", "--shards", "64", "--enable-aof", "true", "--aof-path", "/data/bytecachedb.aof", "--snapshot-path", "/data/bytecachedb.snapshot", "--fsync", "everysec"]
