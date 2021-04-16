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
    doxygen

RUN wget -O /tmp/cmake.sh https://github.com/Kitware/CMake/releases/download/v3.20.1/cmake-3.20.1-Linux-x86_64.sh && \
    sh /tmp/cmake.sh --skip-license --prefix=/usr && \
    rm /tmp/cmake.sh

COPY debian/control /tmp/wfsdk-control

RUN mk-build-deps --install --host-arch=${ARCH} --build-arch=amd64 \
      --tool='apt-get -o Debug::pkgProblemResolver=yes --no-install-recommends --yes'  \
      /tmp/wfsdk-control && \
    rm /tmp/wfsdk-control

RUN useradd -c "Bamboo User" -m -u 999 -U bamboo
USER bamboo:bamboo