#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
coding_root="${DUDU_DOGFOOD_ROOT:-$(cd "$repo_root/../.." && pwd)}"
dudu_bin="$repo_root/build/dudu"
timeout_seconds="${DUDU_DOGFOOD_TIMEOUT:-120}"

"$repo_root/scripts/build.sh" >/dev/null

if [[ ! -x "$dudu_bin" ]]; then
    echo "missing built dudu binary: $dudu_bin" >&2
    exit 1
fi

have_timeout=0
if command -v timeout >/dev/null 2>&1; then
    have_timeout=1
fi

run_with_timeout() {
    if [[ "$have_timeout" == 1 ]]; then
        timeout "$timeout_seconds" "$@"
    else
        "$@"
    fi
}

build_project() {
    local name="$1"
    local path="$2"
    if [[ ! -d "$path" ]]; then
        echo "skip $name: missing $path"
        return 0
    fi

    echo "dogfood $name: build"
    (
        cd "$path"
        run_with_timeout "$dudu_bin" build --timings
    )
}

smoke_webserver() {
    local path="$1"
    local port="${DUDU_DOGFOOD_WEBSERVER_PORT:-18080}"
    local log
    log="$(mktemp)"
    local pid=""

    cleanup() {
        if [[ -n "$pid" ]] && kill -0 "$pid" >/dev/null 2>&1; then
            kill "$pid" >/dev/null 2>&1 || true
            wait "$pid" >/dev/null 2>&1 || true
        fi
        rm -f "$log"
    }
    trap cleanup RETURN

    echo "dogfood dudu-webserver: run smoke"
    (
        cd "$path"
        PORT="$port" "$dudu_bin" run --quiet >"$log" 2>&1
    ) &
    pid="$!"

    local ready=0
    for _ in {1..50}; do
        if curl -fsS --max-time 1 "http://127.0.0.1:$port/health" >/dev/null 2>&1; then
            ready=1
            break
        fi
        if ! kill -0 "$pid" >/dev/null 2>&1; then
            echo "dudu-webserver exited before smoke was ready" >&2
            cat "$log" >&2
            exit 1
        fi
        sleep 0.1
    done

    if [[ "$ready" != 1 ]]; then
        echo "dudu-webserver did not become ready" >&2
        cat "$log" >&2
        exit 1
    fi

    curl -fsS --max-time 2 "http://127.0.0.1:$port/" >/dev/null
    curl -fsS --max-time 2 "http://127.0.0.1:$port/health" | grep -Fq '"ok":true'
    curl -fsS --max-time 2 "http://127.0.0.1:$port/echo?name=dogfood" |
        grep -Fq '"message":"hello, dogfood"'
    curl -fsS --max-time 2 "http://127.0.0.1:$port/routes" |
        grep -Fq '"GET /health"'
}

raymarch_repo="$coding_root/Graphics/raymarch-dd"
webserver_repo="$coding_root/Web/dudu-webserver"
datascience_repo="$coding_root/ML/dudu-datascience"

build_project "raymarch-dd" "$raymarch_repo"

if [[ -d "$webserver_repo" ]]; then
    build_project "dudu-webserver" "$webserver_repo"
    if command -v curl >/dev/null 2>&1; then
        smoke_webserver "$webserver_repo"
    else
        echo "skip dudu-webserver smoke: curl not found"
    fi
else
    echo "skip dudu-webserver: missing $webserver_repo"
fi

if [[ -d "$datascience_repo" ]]; then
    build_project "dudu-datascience" "$datascience_repo"
    if [[ -x "$datascience_repo/scripts/check_target_api.sh" ]]; then
        echo "dogfood dudu-datascience: target API"
        (
            cd "$datascience_repo"
            PATH="$repo_root/build:$PATH" run_with_timeout ./scripts/check_target_api.sh
        )
    else
        echo "skip dudu-datascience target API: checker not found"
    fi
else
    echo "skip dudu-datascience: missing $datascience_repo"
fi

echo "dogfood ok"
