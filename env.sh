WORKDIR="$(pwd)"

export LSAN_OPTIONS="suppressions=${WORKDIR}/lsan.supp"
export TSAN_OPTIONS="halt-on-error=0,suppressions=${WORKDIR}/tsan.supp"

