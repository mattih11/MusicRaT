#!/usr/bin/env bash

set -euo pipefail

if [[ $# -ne 3 || ( "$1" != "std" && "$1" != "evl" ) ]]; then
    echo "Usage: $0 <std|evl> <binary-directory> <alsa-null-config>" >&2
    exit 2
fi

platform=$1
binary_dir=$(realpath "$2")
null_config=$(realpath "$3")
if [[ ! -f "$null_config" ]]; then
    echo "ALSA null configuration not found: $null_config" >&2
    exit 1
fi
for name in musicrat_audio_output_probe musicrat_playback_preroll_test \
    musicrat_audio_output_backend_alsa_test musicrat_audio_device_sink_test; do
    if [[ ! -x "$binary_dir/$name" ]]; then
        echo "Required runtime binary missing: $binary_dir/$name" >&2
        exit 1
    fi
done

echo "Kernel: $(uname -srvm)"
echo "Runtime binary checksums:"
sha256sum "$binary_dir/musicrat_audio_output_probe" \
    "$binary_dir/musicrat_playback_preroll_test" \
    "$binary_dir/musicrat_audio_output_backend_alsa_test" \
    "$binary_dir/musicrat_audio_device_sink_test"
if [[ "$platform" == "evl" ]]; then
    if [[ ! -e /dev/evl/control ]]; then
        echo "EVL kernel control device is unavailable" >&2
        exit 1
    fi
    evl test clock-timer-periodic simple-clone detach-self
fi

export ALSA_CONFIG_PATH="$null_config"
"$binary_dir/musicrat_playback_preroll_test"
"$binary_dir/musicrat_audio_output_backend_alsa_test"
"$binary_dir/musicrat_audio_device_sink_test"
if result=$("$binary_dir/musicrat_audio_output_probe" --device null --blocks 10); then
    :
else
    printf '%s\n' "$result" >&2
    echo "Null-device playback probe failed" >&2
    exit 1
fi
printf '%s\n' "$result"
if ! grep -q "\"platform\":\"$platform\"" <<< "$result"; then
    echo "Probe platform does not match requested validation platform: $platform" >&2
    exit 1
fi
echo "Audio runtime validation passed ($platform, ALSA null device; no hardware performance claim)."
