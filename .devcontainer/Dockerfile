###
# THIS FILE WAS GENERATED AUTOMATICALLY. DO NOT CHANGE IT.
###

ARG DOCKER_IMAGE_ROOT=ubuntu:24.04
FROM ${DOCKER_IMAGE_ROOT} AS base

ENV DEBIAN_FRONTEND noninteractive

RUN apt update --fix-missing && \
    apt -y install --no-install-recommends python3 python3-pip python3-venv\
    git build-essential pkg-config graphviz && \
    apt -y install sudo && \
    rm -rf /var/lib/apt/lists/* && \
    update-alternatives --install /usr/bin/python python /usr/bin/python3 1

FROM base AS cpp

RUN apt update --fix-missing && apt -y --no-install-recommends install cppcheck lib32z1 \
    make cmake clang clang-format clang-tidy doxygen libcmocka0 libcmocka-dev gdb screen wget curl && \
    rm -rf /var/lib/apt/lists/*


FROM cpp AS stm32
WORKDIR /opt/


RUN apt-get update -q -y && \
    apt-get install -y --no-install-recommends curl wget openocd gdb-multiarch stlink-tools && \
    rm -rf /var/lib/apt/lists/*

# pull the gcc-arm-none-eabi tarball, add path to tar file
RUN wget https://developer.arm.com/-/media/Files/downloads/gnu-rm/10.3-2021.10/gcc-arm-none-eabi-10.3-2021.10-x86_64-linux.tar.bz2 && \
    tar -xvf gcc-arm-none-eabi-10.3-2021.10-x86_64-linux.tar.bz2 && \
    rm gcc-arm-none-eabi-10.3-2021.10-x86_64-linux.tar.bz2 && \
    mv gcc-arm-none-eabi-10.3-2021.10 gcc-arm-none-eabi

# Set up the compiler path
ENV PATH $PATH:/opt/gcc-arm-none-eabi/bin

ENV LC_ALL=C
WORKDIR /opt

FROM stm32 AS custom_bootloader
# user can add his docker code below which will be integrated with
# main docker file and not overwritten by pwrforge update
FROM custom_bootloader AS bootloader_dev

ARG UID_NUMBER=1000
ARG GID_NUMBER=1000
ARG SSH_PORT=2000
ARG CONAN_LOGIN_USERNAME=""
ARG CONAN_PASSWORD=""
ARG DEV_USER=ubuntu

ENV CONAN_LOGIN_USERNAME=${CONAN_LOGIN_USERNAME}
ENV CONAN_PASSWORD=${CONAN_PASSWORD}

WORKDIR /opt

# configure ssh
RUN apt update --fix-missing && apt install -y --no-install-recommends openssh-server && \
    rm -rf /var/lib/apt/lists/* && \
    ssh-keygen -A && mkdir -p /run/sshd && \
    echo "Port $SSH_PORT" >> /etc/ssh/sshd_config

EXPOSE $SSH_PORT

RUN printf "\n\nCONFIGURING USER $DEV_USER - DEV ENV ONLY!\n\n" >&2 && \
    apt -y install sudo && \
    if ! getent group "$GID_NUMBER" >/dev/null; then \
        if getent group "$DEV_USER" >/dev/null; then \
            groupmod -g "$GID_NUMBER" "$DEV_USER"; \
        else \
            groupadd -g "$GID_NUMBER" "$DEV_USER"; \
        fi; \
    fi && \
    if ! id -u "$DEV_USER" >/dev/null 2>&1; then \
        useradd -m -s /bin/bash -N -o -u "$UID_NUMBER" -g "$GID_NUMBER" "$DEV_USER"; \
    else \
        usermod -o -u "$UID_NUMBER" -g "$GID_NUMBER" "$DEV_USER"; \
    fi && \
    mkdir -p /etc/sudoers.d && \
    echo "$DEV_USER ALL=(ALL) NOPASSWD:ALL" > "/etc/sudoers.d/90-$DEV_USER" && \
    chmod 0440 "/etc/sudoers.d/90-$DEV_USER" && \
    passwd -d "$DEV_USER" || true
RUN if ! getent group "4" >/dev/null; then \
        groupadd -g "4" "hostgrp_4"; \
    fi
RUN if ! getent group "20" >/dev/null; then \
        groupadd -g "20" "hostgrp_20"; \
    fi
RUN if ! getent group "24" >/dev/null; then \
        groupadd -g "24" "hostgrp_24"; \
    fi
RUN if ! getent group "27" >/dev/null; then \
        groupadd -g "27" "hostgrp_27"; \
    fi
RUN if ! getent group "30" >/dev/null; then \
        groupadd -g "30" "hostgrp_30"; \
    fi
RUN if ! getent group "46" >/dev/null; then \
        groupadd -g "46" "hostgrp_46"; \
    fi
RUN if ! getent group "100" >/dev/null; then \
        groupadd -g "100" "hostgrp_100"; \
    fi
RUN if ! getent group "114" >/dev/null; then \
        groupadd -g "114" "hostgrp_114"; \
    fi
RUN if ! getent group "124" >/dev/null; then \
        groupadd -g "124" "hostgrp_124"; \
    fi
RUN if ! getent group "984" >/dev/null; then \
        groupadd -g "984" "hostgrp_984"; \
    fi

COPY requirements.txt requirements.txt
RUN python3 -m venv /opt/venv && \
    /opt/venv/bin/pip install --upgrade pip && \
    /opt/venv/bin/pip install -r requirements.txt && \
    rm requirements.txt
ENV PATH="/opt/venv/bin:${PATH}"

COPY pwrforge-*-py3-none-any.whl /opt/pwrforge-0-py3-none-any.whl
RUN /opt/venv/bin/pip install /opt/pwrforge-0-py3-none-any.whl

RUN usermod -a -G dialout ${DEV_USER}
RUN install -d -m 0775 /opt/pwrforge-cache/stm32cube && \
    if [ ! -d "/opt/pwrforge-cache/stm32cube/stm32cubef1-src/.git" ]; then \
        git clone --depth 1 --recurse-submodules --shallow-submodules \
            https://github.com/STMicroelectronics/STM32CubeF1.git \
            /opt/pwrforge-cache/stm32cube/stm32cubef1-src; \
    fi && \
    install -d -m 0775 /home/ubuntu/.cache/pwrforge/stm32cube && \
    chown -R ${DEV_USER}:$GID_NUMBER /home/ubuntu/.cache/pwrforge/stm32cube /opt/pwrforge-cache/stm32cube
ENV HOME=/home/${DEV_USER}
USER ${DEV_USER}
WORKDIR /workspace