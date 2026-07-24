# DICOM Imaging Pipeline (Tynovate Track B)

## Layout
- `src/ingestion/` — DICOM tag/pixel parser (Week 5)
- `src/reconstruction/` — 2D slices → 3D volume (Week 6)
- `src/processing/` — SIMD filters + thread pool (Weeks 6–7)
- `src/detection/` — region growing anomaly detection (Week 7)
- `src/output/` — annotated DICOM/JSON/PNG export (Week 8)
- `include/` — shared headers
- `tests/` — unit tests
- `samples/` — sample `.dcm` files (e.g. `ct_small.dcm`)

## Build
```bash
mkdir -p build && cd build
cmake ..
make
./ingestion_test
```
(Run from the project root so the relative path to `samples/ct_small.dcm` resolves — or `cd` into `build` and adjust, we'll fix this properly with a CMake-copied samples path soon.)
