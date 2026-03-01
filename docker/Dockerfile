# =============================================================================
# Dockerfile — ReportServer
#
# Multi-stage build. Builds natively for the host architecture — no QEMU.
#
# TARGETARCH is set automatically by Docker:
#   "amd64"  on x86_64 hosts (Linux servers, Intel Macs)
#   "arm64"  on Apple Silicon Macs (M1/M2/M3)
#
# The correct Qt toolchain is selected for each architecture:
#   amd64  → gcc_64     (Qt's x86_64 Linux toolchain)
#   arm64  → gcc_arm64  (Qt's ARM64 Linux toolchain)
#
# Build:
#   docker build -f docker/Dockerfile -t reportserver .
#
# Run standalone:
#   docker run -p 8080:8080 -v $(pwd)/templates:/app/templates reportserver
# =============================================================================


# =============================================================================
# Stage 1: Builder
# =============================================================================
FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive
ENV QT_VERSION=6.6.3

# Docker automatically sets TARGETARCH to the platform being built for.
# We use it below to choose the matching Qt toolchain.
ARG TARGETARCH

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    ninja-build \
    git \
    ca-certificates \
    python3 \
    python3-pip \
    # libGL is required when linking against Qt on Linux
    libgl1-mesa-dev \
    # zlib is required by QuaZip
    zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

# --break-system-packages is required on Ubuntu 24.04+ (Python 3.12 enforces PEP 668)
RUN pip3 install aqtinstall --break-system-packages

# ---------------------------------------------------------------------------
# Install Qt using aqtinstall.
#
# The Qt arch string depends on the build platform:
#   amd64  → gcc_64      (x86_64 Linux)
#   arm64  → gcc_arm64   (ARM64 Linux — native on Apple Silicon)
#
# After installing, we copy the toolchain to /opt/Qt/current so the rest
# of this Dockerfile can use a fixed path regardless of architecture.
# (COPY --from in the runtime stage requires a static path — no variables.)
# ---------------------------------------------------------------------------
RUN case "${TARGETARCH}" in \
      arm64) QT_ARCH="gcc_arm64" ;; \
      *)     QT_ARCH="gcc_64"    ;; \
    esac \
    && aqt install-qt linux desktop ${QT_VERSION} ${QT_ARCH} -O /opt/Qt -m qt5compat \
    # Copy to a fixed, arch-neutral directory so downstream steps use one known path
    && cp -r /opt/Qt/${QT_VERSION}/${QT_ARCH} /opt/Qt/current

# All subsequent CMake invocations find Qt here
ENV QT_DIR=/opt/Qt/current

# ---------------------------------------------------------------------------
# Build QuaZip from source
#
# QuaZip is a Qt-friendly ZIP library. We build from source to guarantee it
# links against the exact Qt version installed above.
# v1.4 requires Qt5Compat (installed via -m qt5compat above).
# ---------------------------------------------------------------------------
RUN git clone --depth=1 --branch v1.4 \
        https://github.com/stachenov/quazip.git /tmp/quazip \
    && cmake -S /tmp/quazip -B /tmp/quazip-build \
        -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_PREFIX_PATH=${QT_DIR} \
        -DQUAZIP_QT_MAJOR_VERSION=6 \
    && cmake --build /tmp/quazip-build \
    && cmake --install /tmp/quazip-build \
    && rm -rf /tmp/quazip /tmp/quazip-build

# ---------------------------------------------------------------------------
# Build ReportServer
# ---------------------------------------------------------------------------
WORKDIR /src
COPY . .

RUN cmake -S . -B /build \
        -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_PREFIX_PATH="${QT_DIR};/usr/local" \
    && cmake --build /build


# =============================================================================
# Stage 2: Runtime
# =============================================================================
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    # LibreOffice converts the filled DOCX to PDF
    libreoffice \
    # Standard Qt runtime dependencies available in Ubuntu 24.04
    libdouble-conversion3 \
    libpcre2-16-0 \
    libzstd1 \
    libglib2.0-0 \
    # curl is used by the Docker health check
    curl \
    && rm -rf /var/lib/apt/lists/*

# ---------------------------------------------------------------------------
# Copy Qt shared libraries.
# /opt/Qt/current is a real directory (created with cp -r in the builder),
# so Docker resolves it correctly without any symlink ambiguity.
# The lib/ directory contains all Qt .so files plus bundled ICU libraries.
# ---------------------------------------------------------------------------
COPY --from=builder /opt/Qt/current/lib /opt/Qt/lib

# Copy the QuaZip shared library built in Stage 1
COPY --from=builder /usr/local/lib/libQuaZip* /usr/local/lib/

# Register all copied libraries with the dynamic linker
RUN ldconfig

# Tell the runtime linker where to find Qt and QuaZip
ENV LD_LIBRARY_PATH=/opt/Qt/lib:/usr/local/lib

# ---------------------------------------------------------------------------
# Application setup
# ---------------------------------------------------------------------------
COPY --from=builder /build/ReportServer /app/ReportServer
COPY docker/config.json /app/config.json
RUN mkdir -p /app/templates /app/temp

WORKDIR /app
EXPOSE 8080
CMD ["/app/ReportServer"]
