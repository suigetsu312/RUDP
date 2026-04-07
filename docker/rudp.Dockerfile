FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive
ENV VCPKG_ROOT=/opt/vcpkg

RUN apt-get update && apt-get install -y \
    build-essential \
    ca-certificates \
    cmake \
    curl \
    git \
    ninja-build \
    pkg-config \
    tar \
    unzip \
    zip \
    && rm -rf /var/lib/apt/lists/*

RUN git clone https://github.com/microsoft/vcpkg.git "${VCPKG_ROOT}" && \
    "${VCPKG_ROOT}/bootstrap-vcpkg.sh" -disableMetrics

WORKDIR /src
COPY . .


RUN rm -rf build && \
    cmake -S . -B build \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DRUDP_BUILD_TESTS=OFF \
    -DCMAKE_TOOLCHAIN_FILE=${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake && \
    cmake --build build --target rudp_app

FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    ca-certificates \
    iproute2 \
    iputils-ping \
    libstdc++6 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace
COPY --from=builder /src/build/rudp_app /usr/local/bin/rudp_app

ENTRYPOINT ["/usr/local/bin/rudp_app"]
