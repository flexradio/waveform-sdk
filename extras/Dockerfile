FROM ubuntu:latest AS ct-base
# LABEL maintainer="nh6z@nh6z.net"
ARG CT_VERSION="1.24.0"

RUN apt-get update && apt-get install -y \
    wget \
    build-essential \
    libtool-bin \
    bison \
    flex \
    gperf \
    libncurses5-dev \
    texinfo \
    help2man \
    cmake \
    unzip \
    gawk \
    autoconf \
    git \
    zip \
    wixl

ENV CT_PREFIX="/usr/x-tools"
RUN wget http://crosstool-ng.org/download/crosstool-ng/crosstool-ng-${CT_VERSION}.tar.bz2 -O /tmp/ct.tar.bz2 && \
    tar -xvf /tmp/ct.tar.bz2 && \
    cd crosstool-ng-${CT_VERSION} && ./configure && make && make install && \
    cd .. && rm -rf /tmp/ct.tar.bz2 crosstool-ng-${CT_VERSION}

COPY ct-ng.config toolchain/.config

FROM ct-base AS flex6k-toolchain
RUN cd toolchain && \
    ct-ng oldconfig && \
    ct-ng build.4 && \
    cd .. && \
    rm -rf toolchain
ENV PATH=${CT_PREFIX}/flex6k/bin:$PATH