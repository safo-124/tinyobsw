#!/usr/bin/env bash
# Start the flight software simulator and the ground server together,
# then open http://localhost:8000 in your browser. Ctrl+C stops both.
#
# The console served here is the last `npm run build`. While editing the
# console, run `npm run dev` in console/ instead and open http://localhost:5173.
set -euo pipefail
cd "$(dirname "$0")/.."

if [ ! -x build/obsw_sim ]; then
  cmake -S obsw -B build -G Ninja && cmake --build build --target obsw_sim
fi
if [ ! -d console/dist ]; then
  (cd console && npm run build)
fi
# shellcheck disable=SC1091
source .venv/bin/activate

./build/obsw_sim -q &
SIM=$!
trap 'kill "$SIM" 2>/dev/null' EXIT

echo "Console: http://localhost:8000"
cd ground && uvicorn tinyground.api:app --port 8000
