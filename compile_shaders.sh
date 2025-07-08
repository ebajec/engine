#!/usr/bin/env bash

set -euo pipefail

# Usage: ./compile_shaders.sh [INPUT_DIR] [OUTPUT_DIR]
INPUT_DIR="${1:-./src/shader}"
OUTPUT_DIR="${2:-./resource/shader}"

# Make sure the output directory exists
mkdir -p "$OUTPUT_DIR"

EXTENSIONS=(vert frag comp geom tesc tese)

for ext in "${EXTENSIONS[@]}"; do
  for src in "$INPUT_DIR"/*."$ext"; do
    # skip if no files match
    [[ -e "$src" ]] || continue

    filename="$(basename "$src")"
    out="$OUTPUT_DIR/${filename}.spv"

    echo "Compiling $src → $out"
    glslangValidator -V -I${INPUT_DIR} "$src" -o "$out"
  done
done
