#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build}"
OUT_DIR="${OUT_DIR:-$ROOT_DIR/.p0_hunter}"
APP="${APP:-$BUILD_DIR/native_globe}"

mkdir -p "$OUT_DIR"

if [[ ! -x "$APP" ]]; then
  echo "[p0_hunter] native_globe not found at: $APP"
  echo "[p0_hunter] Build first (example): cmake -S . -B build && cmake --build build -j"
  exit 1
fi

RUN_TS="$(date +%Y%m%d_%H%M%S)"
CSV_FILE="$OUT_DIR/p0_metrics_${RUN_TS}.csv"
LOG_FILE="$OUT_DIR/p0_run_${RUN_TS}.log"

# West Turkey / Aegean reproducible camera preset.
# Lat/Lon: 39N / 27E, medium altitude, low-mid zoom dynamics.
ARGS=(
  --dem-debug
  --dem-provider google-earth
  --lat 39.0
  --lon 27.0
  --alt 180000
  --heading 245
  --tilt 52
  --quality medium
  --stats-csv "$CSV_FILE"
)

echo "[p0_hunter] Launching scripted parity run"
echo "[p0_hunter] CSV: $CSV_FILE"
echo "[p0_hunter] Log: $LOG_FILE"

set +e
"$APP" "${ARGS[@]}" >"$LOG_FILE" 2>&1
STATUS=$?
set -e

echo "[p0_hunter] Process exit: $STATUS"

if [[ -f "$CSV_FILE" ]]; then
  echo "[p0_hunter] Last metrics rows:"
  tail -n 5 "$CSV_FILE"
fi

echo "[p0_hunter] Done"
exit "$STATUS"
