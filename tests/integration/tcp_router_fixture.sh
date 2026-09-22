#!/usr/bin/env bash

set -u

action=$1
router=$2
pid_file=$3
log_file=$4

router_ready() {
    (exec 3<>/dev/tcp/127.0.0.1/2000) 2>/dev/null
}

case "$action" in
    start)
        if router_ready; then
            printf '%s\n' external > "$pid_file"
            exit 0
        fi

        "$router" > "$log_file" 2>&1 &
        router_pid=$!
        printf '%s\n' "$router_pid" > "$pid_file"

        for _ in {1..50}; do
            if router_ready; then
                exit 0
            fi
            if ! kill -0 "$router_pid" 2>/dev/null; then
                cat "$log_file" >&2
                exit 1
            fi
            sleep 0.02
        done

        kill "$router_pid" 2>/dev/null || true
        cat "$log_file" >&2
        exit 1
        ;;
    stop)
        if [[ ! -f "$pid_file" ]]; then
            exit 0
        fi

        router_pid=$(<"$pid_file")
        rm -f "$pid_file"
        if [[ "$router_pid" == external ]]; then
            exit 0
        fi

        kill "$router_pid" 2>/dev/null || true
        for _ in {1..50}; do
            if ! kill -0 "$router_pid" 2>/dev/null; then
                exit 0
            fi
            sleep 0.02
        done
        kill -KILL "$router_pid" 2>/dev/null || true
        ;;
    *)
        printf 'usage: %s start|stop router pid-file log-file\n' "$0" >&2
        exit 2
        ;;
esac