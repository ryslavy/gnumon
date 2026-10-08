#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IMAGE_NAME="gnumon-builder:ubuntu22.04"

echo "=== Building gnumon in Podman container (${IMAGE_NAME}) ==="

# Build the builder image if not already present
if ! podman image exists "${IMAGE_NAME}"; then
    echo "Building container image ${IMAGE_NAME}..."
    podman build -t "${IMAGE_NAME}" -f "${SCRIPT_DIR}/docker/Containerfile" "${SCRIPT_DIR}/docker"
fi

# Run compilation inside container mounting project dir
podman run --rm \
    --userns=keep-id \
    -v "${SCRIPT_DIR}:/workspace:Z" \
    -w /workspace \
    "${IMAGE_NAME}" \
    bash -c "cmake -B build-container -S . -G Ninja -DCMAKE_BUILD_TYPE=Release && ninja -C build-container"

echo "=== Build finished! Artifacts available in gnumon-linux/build-container/ ==="
