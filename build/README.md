# DICOM Imaging Pipeline

Part of the Tynovate Studio 2026 internship, Track B: a high-performance
C++20 pipeline that reads raw medical scanner files (DICOM/.dcm) and
processes them faster than typical hospital software, while flagging
regions that look anomalous.

## What this project does
A CT/MRI scan is made of many flat 2D image slices. This pipeline:
1. Reads the raw scan files and pulls out the image data (**ingestion**)
2. Stacks the 2D slices back into one 3D volume (**reconstruction**)
3. Cleans up and sharpens the image (**processing**)
4. Scans for unusually dense regions that may indicate a tumor or
   calcification (**detection**)
5. Exports the results as an annotated file + report (**output**)

## Current status
- ✅ Ingestion layer: custom binary DICOM parser (no external DICOM
  library used for parsing logic) that reads tag/VR/length/value
  entries and extracts Modality, Rows, Columns, BitsAllocated, and
  PixelData. Verified against a real sample CT file — extracted pixel
  data size matches the expected `Rows × Columns × (BitsAllocated / 8)`
  calculation exactly.
- ⏳ Reconstruction, processing, detection, output — not started yet.

## Layout
- `src/ingestion/` — DICOM tag/pixel parser
- `src/reconstruction/` — 2D slices → 3D volume (upcoming)
- `src/processing/` — SIMD filters + thread pool (upcoming)
- `src/detection/` — region growing anomaly detection (upcoming)
- `src/output/` — annotated DICOM/JSON/PNG export (upcoming)
- `samples/` — sample `.dcm` test file(s)

## Build & run
```bash
mkdir -p build && cd build
cmake ..
make
cd ..
./build/ingestion_test
```

## Tech stack
C++20, CMake. (DCMTK used only as a reference tool via `dcmdump` to
validate parser output — not linked into the build.)
