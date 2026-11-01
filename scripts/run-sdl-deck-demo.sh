#!/usr/bin/env bash
set -euo pipefail

build_dir=${1:-build-lvgl-sdl-validation}
duration_ms=${2:-1800000}
config=${3:-"$build_dir/playback_status_to_sdl.json"}
fixture=tests/integration/tcp_router_fixture.sh
pid_file="$build_dir/sdl-deck-router-$$.pid"
log_file="$build_dir/sdl-deck-router-$$.log"

if [[ ! -x "$build_dir/musicrat_launcher" || ! -f "$config" ]]; then
    printf 'Build the SDL launcher and modules required by %s first.\n' "$config" >&2
    exit 1
fi

cleanup() {
    bash "$fixture" stop corerat-router-tcp "$pid_file" "$log_file"
}
trap cleanup EXIT
bash "$fixture" start corerat-router-tcp "$pid_file" "$log_file"
"$build_dir/musicrat_launcher" "$config" \
    --duration-ms "$duration_ms"
