#!/bin/bash

# Start llama-server
$HOME/.unsloth/llama.cpp/llama-server \
  -m /home/jeff/.cache/huggingface/hub/models--unsloth--Qwen3.6-27B-MTP-GGUF/snapshots/5cb35eb3dcbf52dbce5f87dbc64df6aaffadcace/Qwen3.6-27B-UD-Q4_K_XL.gguf \
  --ctx-size 49152 \
  --gpu-layers all \
  --no-mmap \
  --threads $(nproc) \
  --host 0.0.0.0 \
  --port 8888 \
  > /tmp/llama-server.log 2>&1 &

LLAMA_PID=$!

# Wait for server to start
sleep 5

# Start opencode
opencode --continue

# Kill llama-server when opencode exits
kill $LLAMA_PID 2>/dev/null
