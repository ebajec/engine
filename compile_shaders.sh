#!/usr/bin/env bash

set -euo pipefail

FORCE_COMPILE=0

while getopts "f" opt; do
    case "$opt" in
		f) FORCE_COMPILE=1 ;;
        \?) echo "Invalid option" >&2; exit 1 ;;
    esac
done
shift $((OPTIND - 1))

# Usage: ./compile_shaders.sh [INPUT_DIR] [OUTPUT_DIR]
INPUT_DIR="${1:-./shader}"
OUTPUT_DIR="${2:-./resource/shader}"

# Make sure the output directory exists
mkdir -p "$OUTPUT_DIR"

EXTENSIONS=(vert frag comp geom tesc tese)

i=0

INCLUDE_FLAGS=""

while INCLUDES= read -r dir; do
	INCLUDE_FLAGS+=" -I${dir}"
done < <(find "${INPUT_DIR}" -type d)

echo "Include flags : ${INCLUDE_FLAGS}"

for ext in "${EXTENSIONS[@]}"; do
  for src in "$INPUT_DIR"/*/*."$ext"; do
    # skip if no files match
    [[ -e "$src" ]] || continue

    filename="$(basename "$src")"
    out="$OUTPUT_DIR/${filename}.spv"

	if [ ${out} -ot ${src} ] || [ ${FORCE_COMPILE} -eq 1 ]; then
		echo "Compiling $src → $out"
		# glslangValidator -V ${INCLUDE_FLAGS} "$src" -o "$out"
		glslc -c -O -g -fpreserve-bindings --target-env=vulkan1.3 ${INCLUDE_FLAGS} "$src" -o "$out"
		i=$((i + 1))
	fi
  done
done

if ((i == 0)); then
	echo "No shaders to compile"
else
	echo "Compiled ${i} shaders"
fi
