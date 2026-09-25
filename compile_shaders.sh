#!/usr/bin/env bash

set -euo pipefail

# Compiles every shader under each vfs mount into OUTPUT_DIR, mirroring the
# layout of the spirv mount:
#
#   <mount root>/<rel>  ->  OUTPUT_DIR/<mount>/<rel>.spv
#
# which the engine looks up as spirv://<mount>/<rel>.spv.
#
# Mounts come from the same environment variables the engine reads:
#   EV2_CORE_MOUNT   path of the core mount (default: <repo>/resource)
#   EV2_MOUNTS       name:path[,name:path...] (optional)
#
# Usage: ./compile_shaders.sh [-f] [OUTPUT_DIR]
#   -f           recompile everything
#   OUTPUT_DIR   default: <repo>/build/spirv

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CORE_MOUNT_NAME="core"

FORCE_COMPILE=0

while getopts "f" opt; do
    case "$opt" in
		f) FORCE_COMPILE=1 ;;
        \?) echo "Invalid option" >&2; exit 1 ;;
    esac
done
shift $((OPTIND - 1))

OUTPUT_DIR="${1:-$SCRIPT_DIR/build/spirv}"
mkdir -p "$OUTPUT_DIR"
OUTPUT_DIR="$(realpath "$OUTPUT_DIR")"

#------------------------------------------------------------------------------
# Mounts

# Editted to support macOS bash (ver 3.2)
# names and absolute root paths share the same index
MOUNT_ORDER=()
MOUNT_PATHS=()
CORE_MOUNT_FOUND=0

add_mount() {
	local name="$1" path="$2"
	local i

	if [[ -z "$name" || -z "$path" ]]; then
		echo "warning: ignoring malformed mount '$name:$path'" >&2
		return
	fi

	if [[ ! -d "$path" ]]; then
		echo "warning: mount '$name' does not exist: $path" >&2
		return
	fi

	path="$(realpath "$path")"

	# the engine refuses duplicate mount names, so only the first one counts
	for i in "${!MOUNT_ORDER[@]}"; do
		if [[ "${MOUNT_ORDER[$i]}" != "$name" ]]; then
			continue
		fi
		if [[ "${MOUNT_PATHS[$i]}" != "$path" ]]; then
			echo "warning: mount '$name' already maps to ${MOUNT_PATHS[$i]}; ignoring $path" >&2
		fi
		return
	done

	MOUNT_ORDER+=("$name")
	MOUNT_PATHS+=("$path")
	if [[ "$name" == "$CORE_MOUNT_NAME" ]]; then
		CORE_MOUNT_FOUND=1
	fi
}

add_mount "$CORE_MOUNT_NAME" "${EV2_CORE_MOUNT:-$SCRIPT_DIR/resource}"

# EV2_MOUNTS: comma separated name:path pairs; empty entries (e.g. a trailing
# comma) are skipped. Split on the first ':' only, so paths may contain ':'.
if [[ -n "${EV2_MOUNTS:-}" ]]; then
	IFS=',' read -ra MOUNT_ENTRIES <<< "$EV2_MOUNTS"
	for entry in "${MOUNT_ENTRIES[@]}"; do
		[[ -z "$entry" ]] && continue
		if [[ "$entry" != *:* ]]; then
			echo "warning: ignoring malformed mount '$entry' (expected name:path)" >&2
			continue
		fi
		add_mount "${entry%%:*}" "${entry#*:}"
	done
fi

if [[ "$CORE_MOUNT_FOUND" -eq 0 ]]; then
	echo "error: no core mount; set EV2_CORE_MOUNT" >&2
	exit 1
fi

echo "Output : $OUTPUT_DIR"
for i in "${!MOUNT_ORDER[@]}"; do
	echo "Mount  : ${MOUNT_ORDER[$i]} -> ${MOUNT_PATHS[$i]}"
done

#------------------------------------------------------------------------------
# Include tree
#
# INCLUDE_ROOT/<mount> links to each mount root, so shaders can include by vfs
# path: #include "core/shader/frame.glsl". It sits next to OUTPUT_DIR rather
# than inside it, so it isn't part of the (monitored) spirv mount. CMake also
# creates it at configure time for editor tooling, so only this run's mounts
# are (re)linked here; links for other mounts are left alone.

INCLUDE_ROOT="$(dirname "$OUTPUT_DIR")/shader_include"
mkdir -p "$INCLUDE_ROOT"
for i in "${!MOUNT_ORDER[@]}"; do
	ln -sfn "${MOUNT_PATHS[$i]}" "$INCLUDE_ROOT/${MOUNT_ORDER[$i]}"
done

#------------------------------------------------------------------------------
# Compilation

EXTENSIONS=(vert frag comp geom tesc tese)

compiled=0
failed=()

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

compile_mount() {
	local name="$1" root="$2"

	# Shaders include and import by vfs path only ("core/shader/frame.glsl",
	# import "core/shader/sort/sort.slang"), so the include tree is the only
	# search path.
	INCLUDE_FLAGS=("-I$INCLUDE_ROOT")

	local ext src rel out depfile

	for ext in "${EXTENSIONS[@]}"; do
		while IFS= read -r -d '' src; do
			rel="${src#"$root"/}"
			out="$OUTPUT_DIR/$name/${rel}.spv"
			depfile="$OUTPUT_DIR/$name/${rel}.d"
			mkdir -p "$(dirname "$out")"

			if [ ${FORCE_COMPILE} -eq 1 ] || needs_recompile "$src" "$out" "$depfile"; then
				echo "Compiling $name://$rel → $out"
				if glslc -c -O -g -fpreserve-bindings --target-env=vulkan1.3 \
					-MD -MF "$depfile" "${INCLUDE_FLAGS[@]}" "$src" -o "$out"; then
					compiled=$((compiled + 1))
				else
					failed+=("$name://$rel")
				fi
			fi
		done < <(find "$root" -type f -name "*.${ext}" -print0)
	done

	# Slang (and HLSL, which slangc also reads). slangc emits every
	# [shader()]-annotated entry point into one module when no -entry flags are
	# given, and -depfile lists transitive imports in the same format
	# needs_recompile() already parses.
	while IFS= read -r -d '' src; do
		# library modules declare no entry points; they arrive via `import`
		grep -q '\[shader(' "$src" || continue

		rel="${src#"$root"/}"
		out="$OUTPUT_DIR/$name/${rel}.spv"
		depfile="$OUTPUT_DIR/$name/${rel}.d"
		mkdir -p "$(dirname "$out")"

		if [ ${FORCE_COMPILE} -eq 1 ] || needs_recompile "$src" "$out" "$depfile"; then
			echo "Compiling $name://$rel → $out"
			# -fvk-use-entrypoint-name keeps entry point names, which #entry
			# in shader paths relies on (otherwise they are all renamed "main")
			if slangc -fvk-use-entrypoint-name "${INCLUDE_FLAGS[@]}" -target spirv \
				-depfile "$depfile" "$src" -o "$out"; then
				compiled=$((compiled + 1))
			else
				failed+=("$name://$rel")
			fi
		fi
	done < <(find "$root" -type f \( -name '*.slang' -o -name '*.hlsl' \) -print0)
}

for i in "${!MOUNT_ORDER[@]}"; do
	compile_mount "${MOUNT_ORDER[$i]}" "${MOUNT_PATHS[$i]}"
done

if ((compiled == 0)); then
	echo "No shaders to compile"
else
	echo "Compiled ${compiled} shaders"
fi

if ((${#failed[@]} > 0)); then
	echo "Failed to compile ${#failed[@]} shaders:" >&2
	printf '\t%s\n' "${failed[@]}" >&2
	exit 1
fi
