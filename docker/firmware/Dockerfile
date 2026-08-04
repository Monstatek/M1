FROM debian:bookworm-slim@sha256:63a496b5d3b99214b39f5ed70eb71a61e590a77979c79cbee4faf991f8c0783e

ARG TARGETARCH
ARG VCS_REF=unknown
ARG ARM_GNU_VERSION=14.2.rel1
ARG ARM_GNU_ARCHIVE=arm-gnu-toolchain-14.2.rel1-x86_64-arm-none-eabi.tar.xz
ARG ARM_GNU_SHA256=62a63b981fe391a9cbad7ef51b17e49aeaa3e7b0d029b36ca1e9c3b2a9b78823
ARG ARM_GNU_BASE_URL=https://developer.arm.com/-/media/Files/downloads/gnu/14.2.rel1/binrel

LABEL org.opencontainers.image.title="Monstatek M1 firmware build environment"
LABEL org.opencontainers.image.description="Pinned Linux amd64 build environment for M1 firmware"
LABEL org.opencontainers.image.source="https://github.com/Monstatek/M1"
LABEL org.opencontainers.image.revision="${VCS_REF}"
LABEL org.opencontainers.image.vendor="Monstatek M1 engineering review"
LABEL org.opencontainers.image.licenses="GPL-3.0-or-later"

ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=UTC
ENV LANG=C.UTF-8
ENV LC_ALL=C.UTF-8
ENV ARM_GNU_HOME=/opt/arm-gnu-toolchain-14.2.rel1
ENV PATH=/opt/arm-gnu-toolchain-14.2.rel1/bin:${PATH}

RUN test "${TARGETARCH}" = "amd64"

RUN apt-get update \
    && apt-get install --yes --no-install-recommends \
        ca-certificates \
        coreutils \
        curl \
        file \
        git \
        make \
        cmake \
        ninja-build \
        python3 \
        srecord \
        xz-utils \
    && rm -rf /var/lib/apt/lists/*

RUN curl \
        --fail \
        --location \
        --silent \
        --show-error \
        --retry 5 \
        --output "/tmp/${ARM_GNU_ARCHIVE}" \
        "${ARM_GNU_BASE_URL}/${ARM_GNU_ARCHIVE}" \
    && printf '%s  %s\n' \
        "${ARM_GNU_SHA256}" \
        "/tmp/${ARM_GNU_ARCHIVE}" \
        | sha256sum --check --strict - \
    && mkdir -p "${ARM_GNU_HOME}" \
    && tar \
        --extract \
        --xz \
        --file "/tmp/${ARM_GNU_ARCHIVE}" \
        --directory "${ARM_GNU_HOME}" \
        --strip-components=1 \
    && rm -f "/tmp/${ARM_GNU_ARCHIVE}"

RUN arm-none-eabi-gcc --version \
        | head -n 1 \
        | grep --fixed-strings "14.2.1 20241119" \
    && arm-none-eabi-ld --version \
        | head -n 1 \
        | grep --fixed-strings "2.43.1.20241119" \
    && command -v arm-none-eabi-objcopy \
    && command -v arm-none-eabi-objdump \
    && command -v arm-none-eabi-size \
    && command -v srec_cat \
    && command -v cmake \
    && command -v ninja

WORKDIR /workspace

CMD ["make"]
