# DICOM Imaging Pipeline

Part of the Tynovate Studio 2026 internship, Track B: a high-performance
C++20 pipeline that reads raw medical scanner files (DICOM/.dcm) and
processes them faster than typical hospital software, while flagging
regions that look anomalous.

**See `HANDOFF.md` for an honest, detailed breakdown of exactly what's
built/tested versus what's still remaining — read that before assuming
anything below is further along than it is.**

## What this project does
A CT/MRI scan is made of many flat 2D image slices. This pipeline:
1. Reads the raw scan files and pulls out the image data (**ingestion**) — done
2. Stacks the 2D slices back into one 3D volume (**reconstruction**) — done
3. Cleans up and sharpens the image (**processing**) — filters done, thread pool not started
4. Scans for unusually dense regions that may indicate a tumor or calcification (**detection**) — not started
5. Exports the results as an annotated file + report (**output**) — not started

## Layout
- `include/dicom_parser.h` — shared binary DICOM parsing core
- `include/simd_filters.h` — generic scalar/AVX2 convolution engine + named filters
- `src/ingestion/` — single-slice metadata/pixel extraction + preview
- `src/reconstruction/` — multi-slice loading, sorting, HU conversion, 3D stacking
- `src/processing/` — Gaussian blur + Sobel edges (scalar + AVX2, benchmarked), histogram equalization
- `src/detection/` — region growing anomaly detection (upcoming)
- `src/output/` — annotated DICOM/JSON/PNG export (upcoming)
- `samples/` — test files, including a synthetic multi-slice series with a deliberate spacing gap

## Build & run
```bash
mkdir -p build && cd build
cmake ..
make
cd ..
./build/ingestion_test samples/ct_small.dcm
./build/reconstruction_test samples/series
./build/processing_test samples/ct_small.dcm
```

## Measured results (not estimates)
- Gaussian blur: AVX2 exactly matches scalar output, **~5.9x** faster (128x128, 200 iterations)
- Sobel edges: AVX2 exactly matches scalar output, **~4.4x** faster

## Tech stack
C++20, CMake, AVX2 intrinsics. (DCMTK used only as a reference tool via
`dcmdump` to validate parser output — not linked into the build.)
