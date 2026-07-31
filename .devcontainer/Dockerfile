FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

# === Layer 1: System packages & Irvine32 stubs ===
RUN dpkg --add-architecture i386 && \
    apt-get update && \
    apt-get install -y --no-install-recommends \
        wine wine64 wine32 python3 msitools ca-certificates \
        curl unzip git && \
    curl -fsSL https://deb.nodesource.com/setup_20.x | bash - && \
    apt-get install -y nodejs && \
    curl -sL -o /tmp/Irvine.zip "https://github.com/surferkip/asmbook/raw/main/Irvine.zip" && \
    unzip /tmp/Irvine.zip -d /tmp/ && \
    sleep 2 && \
    mv /tmp/Irvine /opt/Irvine32 \
    cp /opt/Irvine32/User32.Lib /opt/Irvine32/user32.lib && \
    cp /opt/Irvine32/Kernel32.Lib /opt/Irvine32/kernel32.lib && \
    rm -rf /tmp/Irvine.zip /tmp/Irvine && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

# === Layer 2: MSVC + Wine ===
WORKDIR /opt/msvc
RUN git clone --depth 1 https://github.com/mstorsjo/msvc-wine.git src
RUN PYTHONUNBUFFERED=1 ./src/vsdownload.py --accept-license --dest /opt/msvc --architecture x86 --with-atl no --with-asan no --with-msbuild no --with-dia no
RUN ./src/install.sh /opt/msvc
RUN wineboot --init && \
    while pgrep wineserver > /dev/null; do sleep 1; done && \
    sleep 10

# Patch the MSVC environment to include Irvine32 paths
RUN sed -i \
    -e 's|^export INCLUDE="|export INCLUDE="z:\\opt\\Irvine32;|' \
    -e 's|^export LIB="|export LIB="z:\\opt\\Irvine32;|' \
    -e 's|^export LIBPATH="|export LIBPATH="z:\\opt\\Irvine32;|' \
    /opt/msvc/bin/x86/msvcenv.sh

# === Layer 3: VS Code Extension (trailofbits/vscode-masm) ===
COPY .devcontainer/extensions/vscode-masm-0.6.1.vsix /opt/vscode-masm.vsix

# === Final Setup ===
WORKDIR /app
