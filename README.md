# Native Globe (Phase 1–3 MVP)

## Proje Hedefi (Genel)

Bu doküman, `AGENTS.md` hedefleriyle uyumludur: **öncelik** `globe-web-html/libs/webglobe.js` API/behavior parity; **ikincil hedef** Google Earth benzeri core globe mimarisine (tile pyramid, SSE LOD, tile state machine, async elevation vb.) yakınsamaktır. Mimari dönüşümler parity'yi bozmadan ilerler.

Minimal OpenGL globe demo with XYZ raster tiles and orbit/zoom controls.

## Features (MVP)
- OpenGL 3.3 core (Windows/Linux/macOS)
- Globe mesh built from XYZ tiles (Web Mercator)
- Raster tile download via libcurl (default: OpenStreetMap)
- Mouse drag = orbit, scroll = zoom

## Build

### macOS
```bash
brew install cmake curl
cmake -S . -B build
cmake --build build -j
./build/native_globe
```

### Linux (Ubuntu/Debian)
```bash
sudo apt-get install -y cmake libcurl4-openssl-dev xorg-dev
cmake -S . -B build
cmake --build build -j
./build/native_globe
```

### Windows (Visual Studio + vcpkg recommended)
1. Install vcpkg and run:
```powershell
vcpkg install curl
```
2. Configure/build:
```powershell
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=C:/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
./build/Release/native_globe.exe
```

## Runtime options
```bash
./native_globe --zoom 2 --segments 16 --tile-radius 2 --tile-url https://tile.openstreetmap.org/{z}/{x}/{y}.png
```

## Notes
- `--zoom` locks a fixed zoom. If omitted, zoom is auto-selected from camera distance.
- `--min-zoom` / `--max-zoom` control auto-zoom bounds (defaults: 2..22).
- `--tile-radius` controls how many tiles around the center are loaded (default: 2 → 5x5).
- Tile disk cache is enabled by default in `tile_cache/`. Use `--cache-dir` or `--no-cache`.
- Vector tiles (optional): `--vector-url` and optional `--vector-layer` (draws point/line + polygon fill via earcut).
- Tile downloads run in a background thread; rendering uses placeholders until data arrives.
- Visible tiles are culled using view direction + FOV margin to reduce overdraw.
- A simple in-app UI (ImGui) provides live controls (zoom, tile radius, cache, vector toggle, camera).
- Tile server usage policies apply. For production, use your own tile service.
