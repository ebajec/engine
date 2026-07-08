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
            glslang-tools
    fi
fi

