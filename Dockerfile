FROM ubuntu:24.04 AS build

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        cmake \
        git \
        libcurl4-openssl-dev \
        libsqlite3-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

RUN cmake -S . -B /build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build /build --parallel "$(nproc)"

FROM ubuntu:24.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        ca-certificates \
        libcurl4 \
        libsqlite3-0 \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --uid 10001 --gid nogroup --home-dir /nonexistent --shell /usr/sbin/nologin --no-create-home buch \
    && mkdir -p /data \
    && chown buch:nogroup /data

COPY --from=build /build/buch /usr/local/bin/buch

ENV BUCH_DB_PATH=/data/buchhaltung.sqlite
ENV BUCH_HOST=0.0.0.0
ENV BUCH_PORT=8080

VOLUME ["/data"]
EXPOSE 8080

USER buch
ENTRYPOINT ["buch"]
CMD ["server"]
