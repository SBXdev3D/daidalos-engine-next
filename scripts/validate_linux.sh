#!/usr/bin/env bash
# Builds, tests and packages the engine on Linux, the whole of Phase 14's checks (ADR-0074).
#
#   scripts/validate_linux.sh [preset]      # preset: linux-clang-release (default), linux-clang-asan
#
# Run it on Ubuntu 24.04 or later, natively or under WSL2, from the repository root, with VCPKG_ROOT set.
# It installs the packages CI installs, configures and builds the preset, runs every test (the GPU tests
# and the scene smoke tests need a Vulkan 1.4 driver with ray queries: the GPU's own, or lavapipe on a
# virtual display), checks the asset paths' case, and builds the release archive. Its report goes to
# build/linux-validation.txt.
set -euo pipefail

preset="${1:-linux-clang-release}"
report="build/linux-validation.txt"
mkdir -p build

if command -v apt-get >/dev/null && [ "${DAI_SKIP_PACKAGES:-0}" != "1" ]; then
    sudo apt-get update
    sudo apt-get install -y ninja-build pkg-config clang python3 \
        libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev libxss-dev libxtst-dev \
        libxkbcommon-dev libwayland-dev libdecor-0-dev libegl1-mesa-dev libgl1-mesa-dev libdrm-dev libgbm-dev \
        libasound2-dev libpulse-dev mesa-vulkan-drivers vulkan-validationlayers vulkan-tools xvfb
fi
if [ -z "${VCPKG_ROOT:-}" ]; then
    echo "Set VCPKG_ROOT to a vcpkg checkout (git clone https://github.com/microsoft/vcpkg && ./vcpkg/bootstrap-vcpkg.sh)" >&2
    exit 1
fi

{
    echo "Daidalos Engine Next, Linux validation, $(date -u +%Y-%m-%dT%H:%MZ)"
    echo "System: $(uname -srm)"
    grep PRETTY_NAME /etc/os-release || true
    if grep -qi microsoft /proc/version; then echo "Under WSL2"; fi
    echo "Compiler: $(clang++ --version | head -1)"
    echo "Vulkan:"
    vulkaninfo --summary 2>/dev/null | grep -E "deviceName|driverName|apiVersion" || echo "  no Vulkan device"
} | tee "$report"

cmake --preset "$preset"
cmake --build --preset "$preset"

# The GPU tests need a window: a virtual display when there is no real one.
run_tests() {
    if [ -n "${DISPLAY:-}" ] || [ -n "${WAYLAND_DISPLAY:-}" ]; then
        ctest --preset "$preset" "$@"
    else
        xvfb-run --auto-servernum --server-args="-screen 0 1280x720x24 -noreset" ctest --preset "$preset" "$@"
    fi
}
set +e
run_tests -LE smoke --output-on-failure 2>&1 | tee build/linux-tests.txt
tests=${PIPESTATUS[0]}
run_tests -L smoke --output-on-failure 2>&1 | tee build/linux-smoke.txt
smoke=${PIPESTATUS[0]}
set -e
{
    echo "Tests: $(grep -E 'tests passed' build/linux-tests.txt || echo 'did not finish')"
    grep -E "\(Failed\)|\(SEGFAULT\)|\(Timeout\)|Not Run" build/linux-tests.txt || true
    echo "Scene smoke tests: $(grep -E 'tests passed' build/linux-smoke.txt || echo 'did not finish')"
    grep -E "\(Failed\)|\(SEGFAULT\)|\(Timeout\)" build/linux-smoke.txt || true
} | tee -a "$report"

if [ "$preset" = "linux-clang-release" ]; then
    cmake --preset linux-clang-shipping
    cmake --build --preset linux-clang-shipping
    cpack --preset linux-clang-shipping | tee -a "$report"
fi
echo "Report: $report"
exit $(( tests != 0 || smoke != 0 ))
