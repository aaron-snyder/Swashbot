# Swashbot container image.
# Builds on x86-64 and ARM64 (e.g. Oracle Cloud's free Ampere VMs) - Docker picks the right D++ package automatically.

ARG DPP_VERSION=10.1.7

# ---------- build stage ----------
FROM ubuntu:24.04 AS build
ARG DPP_VERSION
ARG TARGETARCH
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake ca-certificates curl nlohmann-json3-dev libssl-dev zlib1g-dev libopus-dev \
    && case "$TARGETARCH" in \
         arm64) DPP_PKG="linux-rpi-arm64" ;; \
         *)     DPP_PKG="linux-x64" ;; \
       esac \
    && curl -fsSL -o /tmp/libdpp.deb \
         "https://github.com/brainboxdotcc/DPP/releases/download/v${DPP_VERSION}/libdpp-${DPP_VERSION}-${DPP_PKG}.deb" \
    && apt-get install -y --no-install-recommends /tmp/libdpp.deb \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY CMakeLists.txt ./
COPY src ./src
COPY tests ./tests
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build -j"$(nproc)" \
    && ctest --test-dir build --output-on-failure

# ---------- runtime stage ----------
FROM ubuntu:24.04
ARG DPP_VERSION
ARG TARGETARCH
ENV DEBIAN_FRONTEND=noninteractive
COPY --from=build /tmp/libdpp.deb /tmp/libdpp.deb
RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates /tmp/libdpp.deb \
    && rm -rf /var/lib/apt/lists/* /tmp/libdpp.deb \
    && useradd --create-home swashbot \
    && mkdir -p /data && chown swashbot /data
COPY --from=build /src/build/swashbot /usr/local/bin/swashbot
USER swashbot
ENV SWASHBOT_DATA=/data/swashbot.json
VOLUME ["/data"]
CMD ["swashbot"]
