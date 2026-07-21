#!/bin/bash
# Context Size Benchmark: Qwen3.6-27B on RX 7900 XTX + Ryzen 9 9950X
# Manual, foreground execution with live status table.

SERVER="/home/jeff/.unsloth/llama.cpp/llama-server"
MODEL="/home/jeff/.cache/huggingface/hub/models--unsloth--Qwen3.6-27B-MTP-GGUF/snapshots/5cb35eb3dcbf52dbce5f87dbc64df6aaffadcace/Qwen3.6-27B-UD-Q4_K_XL.gguf"
PORT=8888
CTX_SIZES=(49152 48128 40960 32768)
GPU_LAYERS=all
N_ITERATIONS=4
MAX_TOKENS=1500
RESULTS_FILE="/tmp/bench_ctx_summary.txt"
BODY_FILE="/tmp/bench_body.txt"
PROMPT="Write a complete, production-ready implementation of a concurrent priority queue in Rust. Include heap-based ordering with a generic comparator, interior mutability using Arc and Mutex, proper Send+Sync trait bounds, and comprehensive error handling. Include the full struct definition, all trait implementations, and documentation comments."

> "$RESULTS_FILE"

# ─── Display ──────────────────────────────────────────────────

print_header() {
    clear
    echo "╔══════════════════════════════════════════════════════════════════════╗"
    echo "║  Context Size Benchmark: Qwen3.6-27B Q4_K_XL                       ║"
    printf "║  gpu-layers: %-10s |  max_tokens: %-6s  |  iters: %-2s           ║\n" "$GPU_LAYERS" "$MAX_TOKENS" "$N_ITERATIONS"
    echo "╠══════════════════════════════════════════════════════════════════════╣"
    printf "║ %-8s | %16s | %16s | %10s | %6s ║\n" "CTX" "PROMPT t/s" "GEN t/s" "TOK" "STATUS"
    echo "╠══════════════════════════════════════════════════════════════════════╣"
}

print_results() {
    [ ! -s "$RESULTS_FILE" ] && return
    while IFS='|' read -r r_ctx r_p_tps r_g_tps r_g_tok r_status; do
        if [ "$r_status" = "ok" ] && awk "BEGIN{exit !($r_g_tps > 0)}"; then
            printf "║ %-8s | %16s | %16s | %10s | %6s ║\n" "$r_ctx" "$r_p_tps" "$r_g_tps" "$r_g_tok" "OK"
        else
            printf "║ %-8s | %16s | %16s | %10s | %6s ║\n" "$r_ctx" "-" "-" "0" "FAIL"
        fi
    done < "$RESULTS_FILE"
}

print_status() {
    local status_msg=$1
    print_header
    print_results
    echo "╠══════════════════════════════════════════════════════════════════════╣"
    printf "║  >> %-64s ║\n" "$status_msg"
    echo "╚══════════════════════════════════════════════════════════════════════╝"
}

# ─── Server Control ───────────────────────────────────────────

kill_server() {
    local pids
    pids=$(lsof -ti :$PORT 2>/dev/null || true)
    [ -z "$pids" ] && return
    echo "$pids" | xargs kill 2>/dev/null || true
    sleep 2
    pids=$(lsof -ti :$PORT 2>/dev/null || true)
    [ -z "$pids" ] && return
    echo "$pids" | xargs kill -9 2>/dev/null || true
    sleep 2
    pids=$(lsof -ti :$PORT 2>/dev/null || true)
    [ -n "$pids" ] && echo "$pids" | xargs kill -9 2>/dev/null || true
    sleep 1
}

start_server() {
    local ctx=$1
    local logfile="/tmp/llama-bench-ctx-${ctx}.log"
    > "$logfile"
    $SERVER \
      -m "$MODEL" \
      --ctx-size "$ctx" \
      --gpu-layers "$GPU_LAYERS" \
      --no-mmap \
      --threads $(nproc) \
      --host 0.0.0.0 \
      --port $PORT \
      > "$logfile" 2>&1 &
    echo "$!|$logfile"
}

wait_server() {
    local timeout=120 elapsed=0
    while ! curl -sf http://localhost:$PORT/health > /dev/null 2>&1; do
        sleep 1
        elapsed=$((elapsed + 1))
        [ $elapsed -ge $timeout ] && return 1
    done
    sleep 5
    return 0
}

# ─── Timing Parser (from server log) ─────────────────────────

get_latest_timing() {
    local logfile=$1

    local p_line
    p_line=$(grep "prompt eval time" "$logfile" | tail -1)
    local p_tps p_n
    p_tps=$(echo "$p_line" | awk -F'tokens per second' '{print $1}' | awk '{print $NF}')
    p_n=$(echo "$p_line" | awk -F'/' '{print $2}' | awk '{print $1}')

    local g_line
    g_line=$(grep "eval time" "$logfile" | grep -v "prompt eval time" | tail -1)
    local g_tps g_n
    g_tps=$(echo "$g_line" | awk -F'tokens per second' '{print $1}' | awk '{print $NF}')
    g_n=$(echo "$g_line" | awk -F'/' '{print $2}' | awk '{print $1}')

    echo "${p_tps:-0} ${g_tps:-0} ${p_n:-0} ${g_n:-0}"
}

# ─── Benchmark ────────────────────────────────────────────────

do_benchmark() {
    local ctx=$1

    print_status "Starting ctx-size: $ctx ..."

    sleep 3
    local result
    result=$(start_server "$ctx")
    local pid logfile
    pid=$(echo "$result" | cut -d'|' -f1)
    logfile=$(echo "$result" | cut -d'|' -f2)

    if ! wait_server; then
        kill_server
        echo "$ctx|0|0|0|fail" >> "$RESULTS_FILE"
        print_status "ctx-size: $ctx → FAILED to start (OOM?)"
        return 0
    fi

    local sum_p_tps=0 sum_g_tps=0 sum_g_tok=0 ok=0

    for ((i=0; i<N_ITERATIONS; i++)); do
        print_status "ctx-size: $ctx - Iter $((i+1))/$N_ITERATIONS ..."

        curl -s -o "$BODY_FILE" -X POST \
          http://localhost:$PORT/v1/chat/completions \
          -H "Content-Type: application/json" \
          -d "{
            \"model\": \"test\",
            \"messages\": [
              {\"role\": \"system\", \"content\": \"You are a helpful coding assistant.\"},
              {\"role\": \"user\", \"content\": \"$PROMPT\"}
            ],
            \"max_tokens\": $MAX_TOKENS,
            \"temperature\": 0.7,
            \"stream\": false
          }" 2>/dev/null

        sleep 5

        read -r p_tps g_tps p_n g_n <<< "$(get_latest_timing "$logfile")"

        local g_tok=0
        g_tok=$(jq '.usage.completion_tokens // 0' "$BODY_FILE" 2>/dev/null)

        [ "$g_tok" = "0" ] && [ "$g_n" != "0" ] && g_tok=$g_n

        if [ "$(echo "$g_tps > 0" | bc 2>/dev/null || echo 0)" = "1" ]; then
            sum_p_tps=$(echo "$sum_p_tps + $p_tps" | bc)
            sum_g_tps=$(echo "$sum_g_tps + $g_tps" | bc)
            sum_g_tok=$(echo "$sum_g_tok + $g_tok" | bc)
            ok=$((ok + 1))
        fi
    done

    kill_server
    sleep 3

    local avg_p_tps=0 avg_g_tps=0
    [ $ok -gt 0 ] && {
        avg_p_tps=$(printf "%.2f" "$(echo "$sum_p_tps / $ok" | bc -l)")
        avg_g_tps=$(printf "%.2f" "$(echo "$sum_g_tps / $ok" | bc -l)")
    }

    echo "$ctx|$avg_p_tps|$avg_g_tps|$sum_g_tok|ok" >> "$RESULTS_FILE"
    print_status "ctx-size: $ctx → done (avg gen_tps=$avg_g_tps, prompt_tps=$avg_p_tps, from $ok/$N_ITERATIONS)"
}

# ─── Run ────────────────────────────────────────────────────────

kill_server
for ctx in "${CTX_SIZES[@]}"; do
    do_benchmark "$ctx"
done

# ─── Final Results ──────────────────────────────────────────────

print_header
print_results
echo "╚══════════════════════════════════════════════════════════════════════╝"

best_ctx=0 best_g_tps=0 best_p_tps=0
while IFS='|' read -r ctx p_tps g_tps g_tok status; do
    if [ "$status" = "ok" ] && [ "$(echo "$g_tps > 0" | bc 2>/dev/null || echo 0)" = "1" ]; then
        if [ "$(echo "$g_tps > $best_g_tps" | bc 2>/dev/null || echo 0)" = "1" ]; then
            best_ctx=$ctx
            best_g_tps=$g_tps
            best_p_tps=$p_tps
        fi
    fi
done < "$RESULTS_FILE"

echo "========================================"
echo "BEST: ctx-size=$best_ctx"
echo "  prompt_tps: $best_p_tps  |  gen_tps: $best_g_tps"
echo "========================================"
