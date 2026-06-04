#!/usr/bin/env bash
#
# tests.sh - Start server and run all chat broadcast tests.
#
# Usage:
#   ./tests.sh                  # run tests (no build)
#   ./tests.sh --rebuild        # rebuild via build.sh, then run tests
#   ./tests.sh --clients 5      # custom client count (default 3)
#   ./tests.sh --messages 4     # custom message count (default 2)
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
TESTS_DIR="$ROOT_DIR/tests"
SERVER_BIN="$BUILD_DIR/gameserver"
LOG_DIR="$BUILD_DIR/logs"
SERVER_LOG="$LOG_DIR/server_test.log"

HOST="127.0.0.1"
BINARY_PORT=9999
WEBSOCKET_PORT=8080
NUM_CLIENTS=3
NUM_MESSAGES=2
SERVER_PID=0
DO_REBUILD=false
SERVER_TIMEOUT=10
PASS=0
FAIL=0
TESTS_RUN=0

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

usage() {
    sed -n '2,/^$/s/^#//p' "$0"
    exit 0
}

log()  { echo -e "${CYAN}[TEST]${NC} $*"; }
pass() { echo -e "${GREEN}[PASS]${NC} $*"; PASS=$((PASS+1)); TESTS_RUN=$((TESTS_RUN+1)); }
fail() { echo -e "${RED}[FAIL]${NC} $*"; FAIL=$((FAIL+1)); TESTS_RUN=$((TESTS_RUN+1)); }
warn() { echo -e "${YELLOW}[WARN]${NC} $*"; }

cleanup() {
    if [ "$SERVER_PID" -gt 0 ] && kill -0 "$SERVER_PID" 2>/dev/null; then
        log "Stopping server (PID $SERVER_PID)..."
        kill -TERM "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
        sleep 1
        if kill -0 "$SERVER_PID" 2>/dev/null; then
            kill -9 "$SERVER_PID" 2>/dev/null || true
        fi
        SERVER_PID=0
    fi
}

trap cleanup EXIT INT TERM

parse_args() {
    while [ $# -gt 0 ]; do
        case "$1" in
            --rebuild)   DO_REBUILD=true; shift ;;
            --clients)   NUM_CLIENTS="$2"; shift 2 ;;
            --messages)  NUM_MESSAGES="$2"; shift 2 ;;
            -h|--help)   usage ;;
            *)           echo "Unknown option: $1"; usage ;;
        esac
    done
}

do_rebuild() {
    log "Rebuilding gameserver via build.sh..."
    "$ROOT_DIR/build.sh" --with-sqlite
    if [ ! -x "$SERVER_BIN" ]; then
        fail "Build failed: $SERVER_BIN not found"
        exit 1
    fi
    pass "Build succeeded"
}

copy_tests_to_build() {
    mkdir -p "$BUILD_DIR"
    cp "$TESTS_DIR"/client_binary.py "$BUILD_DIR/"
    cp "$TESTS_DIR"/client_websocket.py "$BUILD_DIR/"
}

start_server() {
    log "Starting gameserver from $BUILD_DIR..."
    mkdir -p "$LOG_DIR"
    rsync -a --delete "$ROOT_DIR/config/" "$BUILD_DIR/config/"
    (cd "$BUILD_DIR" && "./gameserver") > "$SERVER_LOG" 2>&1 &
    SERVER_PID=$!

    for i in $(seq 1 "$SERVER_TIMEOUT"); do
        if ! kill -0 "$SERVER_PID" 2>/dev/null; then
            fail "Server crashed on startup (see $SERVER_LOG)"
            tail -20 "$SERVER_LOG"
            exit 1
        fi
        if ss -tlnp 2>/dev/null | grep -q ":${BINARY_PORT} " && \
           ss -tlnp 2>/dev/null | grep -q ":${WEBSOCKET_PORT} "; then
            pass "Server started (PID $SERVER_PID)"
            return 0
        fi
        sleep 1
    done

    fail "Server did not start within ${SERVER_TIMEOUT}s"
    tail -20 "$SERVER_LOG"
    exit 1
}

wait_for_server_ready() {
    log "Waiting for server readiness..."
    for i in $(seq 1 5); do
        if nc -z "$HOST" "$BINARY_PORT" 2>/dev/null; then
            return 0
        fi
        sleep 0.5
    done
    warn "Port check failed, proceeding anyway..."
}

run_binary_test() {
    log "Running binary protocol chat test (${NUM_CLIENTS} clients, ${NUM_MESSAGES} msgs)..."
    local output
    output=$(cd "$BUILD_DIR" && python3 client_binary.py "$HOST" "$BINARY_PORT" "$NUM_CLIENTS" "$NUM_MESSAGES" 2>&1) || true
    echo "$output"

    local sent_count recv_count
    sent_count=$(echo "$output" | grep -c "\[Client.*Sent:" || true)
    recv_count=$(echo "$output" | grep -c "\[Client.*Broadcast received:" || true)

    if [ "$sent_count" -gt 0 ] && [ "$recv_count" -gt 0 ]; then
        pass "Binary chat: sent=$sent_count received=$recv_count"
    elif [ "$sent_count" -gt 0 ]; then
        warn "Binary chat: sent=$sent_count but no broadcasts received"
    else
        fail "Binary chat: no messages sent"
    fi
}

run_websocket_test() {
    log "Running WebSocket protocol chat test (${NUM_CLIENTS} clients, ${NUM_MESSAGES} msgs)..."
    local output
    output=$(cd "$BUILD_DIR" && python3 client_websocket.py "$HOST" "$WEBSOCKET_PORT" "$NUM_CLIENTS" "$NUM_MESSAGES" 2>&1) || true
    echo "$output"

    local sent_count recv_count
    sent_count=$(echo "$output" | grep -c "\[Client.*Sent:" || true)
    recv_count=$(echo "$output" | grep -c "\[Client.*Broadcast received:" || true)

    if [ "$sent_count" -gt 0 ] && [ "$recv_count" -gt 0 ]; then
        pass "WebSocket chat: sent=$sent_count received=$recv_count"
    elif [ "$sent_count" -gt 0 ]; then
        warn "WebSocket chat: sent=$sent_count but no broadcasts received"
    else
        fail "WebSocket chat: no messages sent"
    fi
}

print_summary() {
    echo ""
    echo "========================================="
    echo "  TEST SUMMARY"
    echo "========================================="
    echo -e "  Total:  ${TESTS_RUN}"
    echo -e "  ${GREEN}Passed: ${PASS}${NC}"
    echo -e "  ${RED}Failed: ${FAIL}${NC}"
    echo "========================================="
    if [ "$FAIL" -gt 0 ]; then
        echo -e "  ${RED}SOME TESTS FAILED${NC}"
        return 1
    else
        echo -e "  ${GREEN}ALL TESTS PASSED${NC}"
        return 0
    fi
}

# --- Main ---

parse_args "$@"

echo ""
echo "========================================="
echo "  GAMESERVER TEST SUITE"
echo "========================================="
echo "  Host:        $HOST"
echo "  Binary:      port $BINARY_PORT"
echo "  WebSocket:   port $WEBSOCKET_PORT"
echo "  Clients:     $NUM_CLIENTS"
echo "  Messages:    $NUM_MESSAGES"
echo "  Rebuild:     $DO_REBUILD"
echo "========================================="
echo ""

if [ "$DO_REBUILD" = true ]; then
    do_rebuild
fi

if [ ! -x "$SERVER_BIN" ]; then
    fail "Server binary not found: $SERVER_BIN"
    echo "  Run with --rebuild to build, or run build.sh first"
    exit 1
fi

copy_tests_to_build
start_server
wait_for_server_ready
sleep 1

run_binary_test
echo ""
run_websocket_test

echo ""
log "Server log: $SERVER_LOG"

print_summary
