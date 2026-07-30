FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

# === Layer 1: System packages ===
RUN dpkg --add-architecture i386 && \
    apt-get update && \
    apt-get install -y --no-install-recommends \
        wine wine64 wine32 python3 msitools ca-certificates \
        curl unzip git && \
    curl -fsSL https://deb.nodesource.com/setup_20.x | bash - && \
    apt-get install -y nodejs && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

# === Layer 2: MSVC + Wine ===
WORKDIR /opt/msvc
RUN git clone --depth 1 https://github.com/mstorsjo/msvc-wine.git src
RUN PYTHONUNBUFFERED=1 ./src/vsdownload.py --accept-license --dest /opt/msvc --architecture x86
RUN ./src/install.sh /opt/msvc
RUN wineboot --init
RUN while pgrep wineserver > /dev/null; do sleep 1; done

# Patch the MSVC environment to include Irvine32 paths (matches workflow)
RUN sed -i \
    -e 's|^export INCLUDE="|export INCLUDE="z:\\opt\\Irvine32;|' \
    -e 's|^export LIB="|export LIB="z:\\opt\\Irvine32;|' \
    -e 's|^export LIBPATH="|export LIBPATH="z:\\opt\\Irvine32;|' \
    /opt/msvc/bin/x86/msvcenv.sh

# === Layer 3: Irvine32 ===
RUN mkdir -p /opt/Irvine32 && \
    curl -sL -o /tmp/Irvine.zip "https://github.com/surferkip/asmbook/raw/main/Irvine.zip" && \
    unzip -o -q /tmp/Irvine.zip -d /tmp/ && \
    mv /tmp/Irvine/* /opt/Irvine32/ && \
    rm -rf /tmp/Irvine.zip /tmp/Irvine

# === Layer 4: VS Code Extension (trailofbits/vscode-masm) ===
WORKDIR /opt
RUN git clone --depth 1 https://github.com/trailofbits/vscode-masm.git
RUN cd vscode-masm && npm install && npx @vscode/vsce package && mv *.vsix /opt/vscode-masm.vsix

# === Final Setup ===
WORKDIR /app
