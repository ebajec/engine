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

mkdir -p "$OUTPUT_DIR"

EXTENSIONS=(vert frag comp geom tesc tese)

i=0

INCLUDE_FLAGS=""

while IFS= read -r dir; do
	INCLUDE_FLAGS+=" -I${dir}"
done < <(find "${INPUT_DIR}" -type d)

echo "Include flags : ${INCLUDE_FLAGS}"

# Returns 0 (needs recompile) if out is missing/stale relative to src or any
# dependency listed in its .d file, or if that .d file doesn't exist yet.
needs_recompile() {
	local src="$1" out="$2" depfile="$3"

	[[ "$out" -ot "$src" ]] && return 0
	[[ -e "$depfile" ]] || return 0

	local dep
	# .d file format: "target: dep1 dep2 \\\n dep3 ..."
	# strip the "target:" prefix and line-continuation backslashes, then
	# walk each whitespace-separated dependency path.
	for dep in $(sed -e 's/^[^:]*://' -e 's/\\$//' "$depfile"); do
		[[ -e "$dep" ]] || continue
		[[ "$out" -ot "$dep" ]] && return 0
	done

	return 1
}

for ext in "${EXTENSIONS[@]}"; do
  for src in "$INPUT_DIR"/*/*."$ext"; do
    # skip if no files match
    [[ -e "$src" ]] || continue

    filename="$(basename "$src")"
    out="$OUTPUT_DIR/${filename}.spv"
    depfile="$OUTPUT_DIR/${filename}.d"

	if needs_recompile "$src" "$out" "$depfile" || [ ${FORCE_COMPILE} -eq 1 ]; then
		echo "Compiling $src → $out"
		glslc -c -O -g -fpreserve-bindings --target-env=vulkan1.3 -MD -MF "$depfile" ${INCLUDE_FLAGS} "$src" -o "$out"
		i=$((i + 1))
	fi
  done
done

if ((i == 0)); then
	echo "No shaders to compile"
else
	echo "Compiled ${i} shaders"
fi
