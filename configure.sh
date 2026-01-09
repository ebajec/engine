#!/bin/bash

git clone git@github.com:KhronosGroup/SPIRV-Reflect.git ./deps/spirv-reflect/spirv-reflect &
git clone git@github.com:ocornut/imgui.git ./deps/imgui/imgui &
git clone git@github.com:epezent/implot.git ./deps/imgui/implot &
git clone git@github.com:nothings/stb.git ./deps/stb/stb & 

wait

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

