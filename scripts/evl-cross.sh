#!/usr/bin/env bash

set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_dir=$(cd -- "${script_dir}/.." && pwd)
sdk_source=${1:-${EVL_SDK_DIR:-}}

if [[ -z "${sdk_source}" ]]; then
    echo "Usage: $0 <SDK directory or .tar.xz archive>" >&2
    exit 2
fi

sdk_source=$(realpath "${sdk_source}")
if [[ -f "${sdk_source}" ]]; then
    sdk_dir="${repo_dir}/.evl-cache/sdk"
    rm -rf "${sdk_dir}"
    mkdir -p "${sdk_dir}"
    tar -xJf "${sdk_source}" -C "${sdk_dir}" --strip-components=1 \
        --exclude='*/dev/*'
elif [[ -d "${sdk_source}" ]]; then
    sdk_dir=${sdk_source}
else
    echo "SDK path does not exist: ${sdk_source}" >&2
    exit 2
fi

if [[ ! -f "${sdk_dir}/usr/include/evl/evl.h" ]]; then
    echo "Invalid RaTOS SDK: ${sdk_dir}" >&2
    exit 1
fi

if [[ -x "${sdk_dir}/relocate-sdk.sh" ]]; then
    "${sdk_dir}/relocate-sdk.sh"
fi

export EVL_SDK_DIR=${sdk_dir}
cd "${repo_dir}"
cmake --fresh --preset evl-cross
cmake --build --preset evl-cross --parallel "${MUSICRAT_BUILD_JOBS:-4}"