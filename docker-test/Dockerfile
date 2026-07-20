FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

# === LAYER 1: System packages ===
RUN dpkg --add-architecture i386 && \
    apt-get update && \
    apt-get install -y --no-install-recommends \
        wine wine64 wine32 python3 msitools ca-certificates \
        curl unzip git \
        && apt-get clean && \
        rm -rf /var/lib/apt/lists/*

# === LAYER 2: Wine prefix init ===
RUN wine wineboot --init && \
    while pgrep wineserver > /dev/null; do sleep 1; done && \
    cp -a /root/.wine /opt/wine-prefix

# === LAYER 3: MSVC + MASM toolchain ===
WORKDIR /opt/msvc
RUN git clone --depth 1 https://github.com/mstorsjo/msvc-wine.git src && \
    PYTHONUNBUFFERED=1 ./src/vsdownload.py --accept-license --dest /opt/msvc && \
    ./src/install.sh /opt/msvc && \
    rm -rf src

# === LAYER 4: Irvine32 + SDK libs + env patch ===
ENV IRVINE_DIR=/opt/irvine32
RUN set -e && \
    mkdir -p "$IRVINE_DIR" && \
    curl -sL -o /tmp/Irvine.zip "https://github.com/surferkip/asmbook/raw/main/Irvine.zip" && \
    unzip -o -q /tmp/Irvine.zip -d /tmp/ && \
    cp /tmp/Irvine/Irvine32.inc "$IRVINE_DIR/" && \
    cp /tmp/Irvine/Irvine32.lib "$IRVINE_DIR/" && \
    cp /tmp/Irvine/*.inc "$IRVINE_DIR/" && \
    cp /tmp/Irvine/Examples/Lib32/kernel32.lib "$IRVINE_DIR/" && \
    cp /tmp/Irvine/Examples/Lib32/user32.lib "$IRVINE_DIR/" && \
    rm -rf /tmp/Irvine.zip /tmp/Irvine && \
    cp "/opt/msvc/Windows Kits/10/Lib/10.0.26100.0/um/x86/kernel32.lib" "$IRVINE_DIR/" && \
    cp "/opt/msvc/Windows Kits/10/Lib/10.0.26100.0/um/x86/user32.lib" "$IRVINE_DIR/" && \
    sed -i \
        -e 's|^export INCLUDE="|export INCLUDE="z:\\opt\\irvine32;|' \
        -e 's|^export LIB="|export LIB="z:\\opt\\irvine32;|' \
        -e 's|^export LIBPATH="|export LIBPATH="z:\\opt\\irvine32;|' \
        /opt/msvc/bin/x86/msvcenv.sh

# === LAYER 5: Runtime config + entrypoint ===
WORKDIR /app
ENV PATH=/opt/msvc/bin/x86:$PATH
COPY docker-test/entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh
ENTRYPOINT ["/entrypoint.sh"]
