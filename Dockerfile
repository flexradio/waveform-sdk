ARG ARCH=amd64
FROM debian:buster AS build-arm64

FROM ubuntu:18.04 AS build-amd64

FROM build-${ARCH}
ARG ARCH

ENV DEBIAN_FRONTEND=noninteractive
RUN ln -sf /usr/share/zoneinfo/America/Los_Angeles /etc/localtime && \
  dpkg --add-architecture ${ARCH} && \
  apt-get update && apt-get install --no-install-recommends -y \
    devscripts \
    debhelper \
    dh-systemd \
    equivs \
    wget \
    ca-certificates \
    git \
    python3-debian

RUN wget -O /tmp/cmake.sh https://github.com/Kitware/CMake/releases/download/v3.20.1/cmake-3.20.1-Linux-x86_64.sh && \
    sh /tmp/cmake.sh --skip-license --prefix=/usr && \
    rm /tmp/cmake.sh

COPY debian/control /tmp/wfsdk-control
COPY scripts/dpkg-deb-toolchain.py /tmp/dpkg-deb-toolchain.py

COPY --from=flexradio/smoothlake-bamboo /opt/x-tools/rasfire/ /opt/x-tools/rasfire/
RUN /tmp/dpkg-deb-toolchain.py --install /opt/x-tools/rasfire/aarch64-linux-gnu/sysroot --control /tmp/wfsdk-control
COPY --from=flexradio/smoothlake-bamboo /opt/x-tools/dragonfire/ /opt/x-tools/dragonfire/
RUN /tmp/dpkg-deb-toolchain.py --install /opt/x-tools/dragonfire/aarch64-linux-gnu/sysroot --control /tmp/wfsdk-control

RUN mk-build-deps --host-arch=${ARCH} --build-arch=amd64 /tmp/wfsdk-control && \
    dpkg-deb -x libwaveform-cross-build-deps_*_${ARCH}.deb /opt/x-tools/rasfire/aarch64-linux-gnu/sysroot && \
    dpkg-deb -x libwaveform-cross-build-deps_*_${ARCH}.deb /opt/x-tools/dragonfire/aarch64-linux-gnu/sysroot && \
    rm /tmp/wfsdk-control

RUN rm /tmp/dpkg-deb-toolchain.py

RUN useradd -c "Bamboo User" -m -u 999 -U bamboo
USER bamboo:bamboo