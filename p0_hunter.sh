#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build}"
OUT_DIR="${OUT_DIR:-$ROOT_DIR/.p0_hunter}"
APP="${APP:-$BUILD_DIR/native_globe}"
SMOKE_TIMEOUT_SEC="${SMOKE_TIMEOUT_SEC:-240}"
SMOKE_EXTRA_ARGS_STR="${SMOKE_EXTRA_ARGS:-}"

SMOKE_EXTRA_ARGS=()
if [[ -n "$SMOKE_EXTRA_ARGS_STR" ]]; then
  # shellcheck disable=SC2206
  SMOKE_EXTRA_ARGS=($SMOKE_EXTRA_ARGS_STR)
fi

mkdir -p "$OUT_DIR"

if [[ ! -x "$APP" ]]; then
  echo "[p0_hunter] native_globe not found at: $APP"
  echo "[p0_hunter] Build first (example): cmake -S . -B build && cmake --build build -j"
  exit 1
fi

RUN_TS="$(date +%Y%m%d_%H%M%S)"

BASE_ARGS=(
  --smoke
  --smoke-scene aegean
  --dem-provider google-earth
  --quality medium
)

run_case() {
  local name="$1"
  shift
  local csv_file="$OUT_DIR/p0_${name}_${RUN_TS}.csv"
  local log_file="$OUT_DIR/p0_${name}_${RUN_TS}.log"
  local report_src="$ROOT_DIR/smoke/smoke_report.txt"
  local report_file="$OUT_DIR/p0_${name}_${RUN_TS}.report.txt"

  echo "[p0_hunter] Case: $name"
  echo "[p0_hunter] CSV:  $csv_file"
  echo "[p0_hunter] LOG:  $log_file"
  echo "[p0_hunter] TIMEOUT: ${SMOKE_TIMEOUT_SEC}s"

  # Remove previous smoke report to avoid stale copy.
  rm -f "$report_src"

  local timeout_prefix=()
  if command -v gtimeout >/dev/null 2>&1; then
    timeout_prefix=(gtimeout "${SMOKE_TIMEOUT_SEC}s")
  elif command -v timeout >/dev/null 2>&1; then
    timeout_prefix=(timeout "${SMOKE_TIMEOUT_SEC}s")
  fi

  local cmd=("$@" "$APP" "${BASE_ARGS[@]}")
  if [[ ${#SMOKE_EXTRA_ARGS[@]} -gt 0 ]]; then
    cmd+=("${SMOKE_EXTRA_ARGS[@]}")
  fi
  cmd+=(--stats-csv "$csv_file")

  set +e
  if [[ ${#timeout_prefix[@]} -gt 0 ]]; then
    "${timeout_prefix[@]}" "${cmd[@]}" >"$log_file" 2>&1
  else
    "${cmd[@]}" >"$log_file" 2>&1
  fi
  local status=$?
  set -e

  echo "[p0_hunter] Case '$name' exit: $status"
  if [[ "$status" -eq 124 ]]; then
    echo "[p0_hunter] Case '$name' timeout after ${SMOKE_TIMEOUT_SEC}s"
  fi

  if [[ -f "$report_src" ]]; then
    cp "$report_src" "$report_file"
    echo "[p0_hunter] REPORT: $report_file"
    local metrics_line
    metrics_line="$(grep '^Metrics:' "$report_file" | tail -n 1 || true)"
    if [[ -n "$metrics_line" ]]; then
      echo "[p0_hunter] $metrics_line"
    fi
    if grep -q 'FAIL: missingTiles > 0' "$report_file"; then
      echo "[p0_hunter][OWNERSHIP] render-set/quorum divergence: src/engine/globe_engine.cpp + src/rendering/render_frame.cpp"
    fi
    if grep -q 'FAIL: seamGapMax exceeded' "$report_file"; then
      echo "[p0_hunter][OWNERSHIP] DEM edge-pack/stitch continuity: src/engine/globe_engine.cpp + src/rendering/tile_mesh_builder.cpp"
    fi
  else
    echo "[p0_hunter] WARNING: smoke report missing for case '$name'"
  fi

  if [[ -f "$csv_file" ]]; then
    echo "[p0_hunter] Last metrics rows ($name):"
    tail -n 5 "$csv_file"
  else
    echo "[p0_hunter] NOTE: stats CSV not produced by smoke mode (expected with current engine path)"
  fi
  echo
  return "$status"
}

healthy_status=0
fallback_status=0

run_case "ge_healthy" env || healthy_status=$?
run_case "ge_auth_fail_fallback" env NATIVE_GLOBE_GE_TOKEN=__invalid__ || fallback_status=$?

if [[ "$healthy_status" -ne 0 || "$fallback_status" -ne 0 ]]; then
  echo "[p0_hunter] FAIL: healthy=$healthy_status fallback=$fallback_status"
  exit 2
fi

echo "[p0_hunter] PASS: both gates succeeded"
exit 0
