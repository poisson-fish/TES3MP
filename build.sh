#!/usr/bin/env bash
# Builds the small TES3MP product surface by default on macOS and Linux. Broader verification is opt-in.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TARGET="product"
CONFIG="RelWithDebInfo"
CLEAN=0
DOCTOR=0
VERBOSE_OUTPUT=0
TAIL_LINES=12

while [[ $# -gt 0 ]]; do
    case "$1" in
        -t|--target|-Target)
            TARGET="$2"
            shift 2
            ;;
        -c|--config|-Config)
            CONFIG="$2"
            shift 2
            ;;
        --clean|-Clean)
            CLEAN=1
            shift
            ;;
        --doctor|-Doctor)
            DOCTOR=1
            shift
            ;;
        -v|--verbose|-VerboseOutput)
            VERBOSE_OUTPUT=1
            shift
            ;;
        --tail-lines|-TailLines)
            TAIL_LINES="$2"
            shift 2
            ;;
        product|client|server|headless|checks|contracts|protocol|server-logic|adapter-tests|desktop-evidence|baseline|doctor|full|standalone)
            TARGET="$1"
            shift
            ;;
        -h|--help)
            echo "Usage: $0 [target] [--target <name>] [--config <cfg>] [--clean] [--doctor] [--verbose]"
            echo "Targets: product, client, server, headless, checks, contracts, protocol, server-logic, adapter-tests, desktop-evidence, baseline, doctor, full, standalone"
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            exit 2
            ;;
    esac
done

case "$(uname -s)" in
    Darwin) PLATFORM="macos" ;;
    Linux)  PLATFORM="linux" ;;
    *)
        echo "Unsupported POSIX host OS: $(uname -s)" >&2
        exit 2
        ;;
esac

LOG_DIR="${REPO_ROOT}/build/logs"
mkdir -p "${LOG_DIR}"
TIMESTAMP="$(date +%Y%m%d-%H%M%S)"
BUILD_LOG="${LOG_DIR}/${TIMESTAMP}-${TARGET}.log"
printf "TES3MP build target: %s (%s)\nStarted: %s\n" "${TARGET}" "${PLATFORM}" "$(date -u +%Y-%m-%dT%H:%M:%SZ)" > "${BUILD_LOG}"

invoke_logged_stage() {
    local name="$1"
    shift
    printf "[stage] %s\n" "${name}"
    printf "\n===== %s =====\n" "${name}" >> "${BUILD_LOG}"
    local exit_code=0
    if [[ "${VERBOSE_OUTPUT}" -eq 1 ]]; then
        "$@" 2>&1 | tee -a "${BUILD_LOG}" || exit_code=$?
    else
        "$@" >> "${BUILD_LOG}" 2>&1 || exit_code=$?
    fi
    if [[ "${exit_code}" -ne 0 ]]; then
        printf "[failed] %s (exit %d)\n" "${name}" "${exit_code}" >&2
        printf "Last %d log lines:\n" "${TAIL_LINES}" >&2
        tail -n "${TAIL_LINES}" "${BUILD_LOG}" >&2
        printf "%s failed. Full log: %s\n" "${name}" "${BUILD_LOG}" >&2
        exit "${exit_code}"
    fi
}

complete_logged_build() {
    printf "[done] Full build log: %s\n" "${BUILD_LOG}"
}

PYTHON_BIN="${VNEXT_PYTHON:-}"
if [[ -z "${PYTHON_BIN}" ]]; then
    for candidate in "$(command -v python3 2>/dev/null || true)" /opt/homebrew/bin/python3 /usr/local/bin/python3 /usr/bin/python3; do
        if [[ -n "${candidate}" && -x "${candidate}" ]] && "${candidate}" -c "pass" >/dev/null 2>&1; then
            PYTHON_BIN="${candidate}"
            break
        fi
    done
fi
if [[ -z "${PYTHON_BIN}" ]]; then
    echo "Could not find a runnable python3 interpreter." >&2
    exit 1
fi

if [[ "${DOCTOR}" -eq 1 || "${TARGET}" == "doctor" ]]; then
    invoke_logged_stage "Baseline doctor" "${PYTHON_BIN}" "${REPO_ROOT}/scripts/run_vnext_baseline.py" doctor --vcpkg-only
    complete_logged_build
    exit 0
fi

if [[ "${TARGET}" == "baseline" ]]; then
    invoke_logged_stage "Full baseline suite" "${PYTHON_BIN}" "${REPO_ROOT}/scripts/run_vnext_baseline.py" all
    complete_logged_build
    exit 0
fi

case "${TARGET}" in
    contracts|standalone|protocol|server-logic|adapter-tests)
        case "${TARGET}" in
            contracts|standalone) STANDALONE_PRESET="tes3mp-standalone" ;;
            protocol)             STANDALONE_PRESET="tes3mp-protocol-contracts" ;;
            server-logic)         STANDALONE_PRESET="tes3mp-server-logic" ;;
            adapter-tests)        STANDALONE_PRESET="tes3mp-adapter-tests" ;;
        esac
        pushd "${REPO_ROOT}/components/tes3mp" >/dev/null
        CONFIGURE_ARGS=(--preset tes3mp-standalone "-DCMAKE_BUILD_TYPE=${CONFIG}")
        if [[ "${CLEAN}" -eq 1 ]]; then
            CONFIGURE_ARGS+=(--fresh)
        fi
        invoke_logged_stage "Configure engine-independent TES3MP graph" cmake "${CONFIGURE_ARGS[@]}"
        invoke_logged_stage "Build and run ${TARGET}" cmake --build --preset "${STANDALONE_PRESET}" --parallel
        popd >/dev/null
        complete_logged_build
        exit 0
        ;;
esac

if [[ "${PLATFORM}" == "macos" ]]; then
    if [[ ! -f "${REPO_ROOT}/deps/openmw-deps/scripts/buildsystems/vcpkg.cmake" && ! -f "/tmp/openmw-deps/scripts/buildsystems/vcpkg.cmake" ]]; then
        invoke_logged_stage "Provision macOS OpenMW vcpkg dependencies" "${PYTHON_BIN}" "${REPO_ROOT}/scripts/run_vnext_baseline.py" provision --vcpkg-only
    fi
fi

MANIFEST="${REPO_ROOT}/build/vnext-transport-dependencies/manifest.json"
invoke_logged_stage "Check verified transport dependencies" "${PYTHON_BIN}" "${REPO_ROOT}/scripts/provision_vnext_transport.py"
if [[ ! -f "${MANIFEST}" ]]; then
    echo "TES3MP transport dependency manifest was not created." >&2
    exit 1
fi

case "${TARGET}" in
    product|full)     BUILD_PRESET="vnext-product-${PLATFORM}" ;;
    client)           BUILD_PRESET="vnext-product-client-${PLATFORM}" ;;
    server)           BUILD_PRESET="vnext-product-server-${PLATFORM}" ;;
    headless)         BUILD_PRESET="vnext-product-headless-${PLATFORM}" ;;
    checks)           BUILD_PRESET="vnext-product-checks-${PLATFORM}" ;;
    desktop-evidence) BUILD_PRESET="vnext-desktop-evidence-${PLATFORM}" ;;
    *)
        echo "Unsupported build target: ${TARGET}" >&2
        exit 2
        ;;
esac

if [[ "${TARGET}" == "desktop-evidence" ]]; then
    CONFIGURE_PRESET="vnext-desktop-evidence-${PLATFORM}"
else
    CONFIGURE_PRESET="vnext-product-${PLATFORM}"
fi

CONFIGURE_ARGS=(--preset "${CONFIGURE_PRESET}" "-DCMAKE_BUILD_TYPE=${CONFIG}")
if [[ "${CLEAN}" -eq 1 ]]; then
    CONFIGURE_ARGS+=(--fresh)
fi
invoke_logged_stage "Configure ${CONFIGURE_PRESET}" cmake "${CONFIGURE_ARGS[@]}"
invoke_logged_stage "Build ${BUILD_PRESET}" cmake --build --preset "${BUILD_PRESET}" --parallel

complete_logged_build

