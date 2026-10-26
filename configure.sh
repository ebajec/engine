#!/bin/bash

git submodule update --init

# Detect Ubuntu
if [ -f /etc/os-release ]; then
    . /etc/os-release
    if [[ "$ID" == "ubuntu" ]]; then
        sudo apt update
        sudo apt install -y \
            build-essential \
            libglfw3-dev \
            libyaml-cpp-dev \
            libglm-dev \
            libfftw3-dev \
            pkg-config \
            glslang-tools
    fi
fi

# Detect Apple
if [ "$(uname -s)" = "Darwin" ]; then
    if ! command -v brew >/dev/null 2>&1; then
        echo "Homebrew is required. Install from https://brew.sh and re-run."
        exit 1
    fi

    brew install \
        glm \
        yaml-cpp \
        cmake \
        glfw \
        fftw \
        pkgconf \
        glslang \
        coreutils
fi

