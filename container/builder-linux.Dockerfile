FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        cmake \
        ninja-build \
        build-essential \
        pkg-config \
        git \
        python3 \
        glslang-tools \
        spirv-tools \
        libsdl2-dev \
        libglm-dev \
        libssl-dev \
        zlib1g-dev \
        libavformat-dev \
        libavcodec-dev \
        libswscale-dev \
        libavutil-dev \
        libvulkan-dev \
        vulkan-tools \
        libstorm-dev \
        libunicorn-dev && \
    rm -rf /var/lib/apt/lists/*

COPY build-linux.sh /build-platform.sh
RUN chmod +x /build-platform.sh

ENTRYPOINT ["/build-platform.sh"]
