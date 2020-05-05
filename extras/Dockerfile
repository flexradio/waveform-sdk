FROM ubuntu:latest AS ct-base
ARG CT_VERSION="1.24.0"

ENV DEBIAN_FRONTEND=noninteractive
RUN ln -sf /usr/share/zoneinfo/America/Los_Angeles /etc/localtime && \
    apt-get update && apt-get install -y \
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
    wixl \
    openssh-server \
    rsync

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

RUN [ "useradd", "-c", "Bob the Builder", "-m", "-s", "/bin/bash", "-p", "", "builder" ]
RUN mkdir -p /var/run/sshd && \
    chown root:root /var/run/sshd && \
    chmod 755 /var/run/sshd && \
    echo "PermitEmptyPasswords yes" >> /etc/ssh/sshd_config

EXPOSE 22/tcp
CMD [ "/usr/sbin/sshd", "-4", "-e", "-D" ]