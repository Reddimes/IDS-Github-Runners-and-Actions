#!/bin/bash

# Start llama-server
$HOME/.unsloth/llama.cpp/llama-server \
  --hf-repo unsloth/gemma-4-26B-A4B-it-qat-GGUF:UD-Q4_K_XL \
  --no-ui \
  --ctx-size 49152 \
  --gpu-layers all \
  --no-mmap \
  --threads $(nproc) \
  --host 0.0.0.0 \
  --port 8888 \
  > /tmp/llama-server.log 2>&1 &

LLAMA_PID=$!

# Wait for llama to be ready
sleep 5

# Start supermemory-server
# OPENAI_BASE_URL=http://127.0.0.1:8888/v1
# OPENAI_API_KEY=local
# OPENAI_MODEL=Qwen3.6-27B-UD-Q4_K_XL
# SUPERMEMORY_DATA_DIR=$HOME/.supermemory/data
# $HOME/.supermemory/bin/supermemory-server \
#   > /tmp/supermemory-server.log 2>&1 &

# SUPERMEMORY_PID=$!

# # Wait for supermemory to be ready
# sleep 5

# Start opencode
opencode --continue

# Kill servers when opencode exits
# kill $SUPERMEMORY_PID 2>/dev/null
kill $LLAMA_PID 2>/dev/null
