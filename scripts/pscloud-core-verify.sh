#!/usr/bin/env bash
set -euo pipefail

# PS Cloud core verification (completed-only vs full).
#
# Scope:
# - build chiaki-unit target
# - run unit tests
# - optional docs marker checks
# - optional static source contract checks for E3/loss-recovery surfaces
#
# This script intentionally excludes unfinished live-playback/media stabilization claims.

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build-current"
CHECK_DOCS=0
MODE="completed-only"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --build-dir)
      BUILD_DIR="${2:-}"
      if [[ -z "$BUILD_DIR" ]]; then
        echo "--build-dir requires a path" >&2
        exit 2
      fi
      shift 2
      ;;
    --check-docs)
      CHECK_DOCS=1
      shift
      ;;
    --mode)
      MODE="${2:-}"
      if [[ "$MODE" != "completed-only" && "$MODE" != "full" ]]; then
        echo "--mode must be one of: completed-only, full" >&2
        exit 2
      fi
      shift 2
      ;;
    *)
      echo "Unknown argument: $1" >&2
      exit 2
      ;;
  esac
done

if [[ ! -d "$BUILD_DIR" ]]; then
  echo "Missing build dir: $BUILD_DIR" >&2
  exit 1
fi

echo "== chiaki-unit build =="
cmake --build "$BUILD_DIR" --target chiaki-unit -j 8

echo
echo "== ctest unit =="
ctest --output-on-failure -R unit --test-dir "$BUILD_DIR"

if [[ "$CHECK_DOCS" == "1" ]]; then
  echo
  echo "== docs marker checks =="
  rg -F -q "Status: Active" "${ROOT_DIR}/doc/pscloud_port_prd.md"
  rg -F -q "Verification (this slice)" "${ROOT_DIR}/doc/pscloud_port_prd.md"
  rg -F -q "Status snapshot (completed vs in-progress)" "${ROOT_DIR}/doc/headless_flutter_embed.md"
fi

echo
echo "== static contract checks (${MODE}) =="
# Completed-only: check presence of implemented/gated recovery + E3-related surfaces.
rg -F -q "chiaki_headless_runtime_recover_auto_with_status" "${ROOT_DIR}/lib/src/headless.c"
rg -F -q "chiaki_headless_runtime_get_recovery_status" "${ROOT_DIR}/lib/src/headless.c"
rg -F -q "chiaki_headless_runtime_recovery_config" "${ROOT_DIR}/lib/src/headless.c"

if [[ "$MODE" == "full" ]]; then
  # Full mode adds explicit checks for E2 media-gate and recovery simulation surfaces.
  rg -F -q "CHIAKI_MEDIA_E2_ENABLE" "${ROOT_DIR}/lib/src/headless.c"
  rg -F -q "chiaki_headless_runtime_simulate_recovery_sequence_report" "${ROOT_DIR}/lib/src/headless.c"
fi

if [[ "$MODE" == "completed-only" ]]; then
  # Boundary: completed-only mode must keep the one-call auto-with-status recover path.
  rg -F -q "chiaki_headless_runtime_recover_auto_with_status" "${ROOT_DIR}/lib/src/headless.c"
fi

echo
echo "PASS: pscloud core verification"
