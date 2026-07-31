# DICOM Imaging Pipeline

**Tynovate Studio 2026 Internship — Track B**

A C++20 pipeline that reads raw medical scanner files (DICOM/.dcm),
reconstructs them into a 3D volume, and accelerates image processing with
hand-written AVX2 SIMD intrinsics — built without relying on an external
DICOM library for the core parsing logic.

> **Status:** Weeks 5–6 complete and tested. See [`HANDOFF.md`](HANDOFF.md)
> for a detailed, honest breakdown of exactly what's built vs. remaining —
> read that before assuming anything here is further along than it is.

## What it does

A CT/MRI scan is made of many flat 2D slices. This pipeline:

| Stage | Status | What it does |
|---|---|---|
| **Ingestion** | ✅ Done | Custom binary parser extracts metadata + pixel data from raw `.dcm` files |
| **Reconstruction** | ✅ Done | Stacks 2D slices into a 3D volume, with Hounsfield Unit normalization and spacing-aware resampling |
| **Processing** | ✅ Filters done, thread pool pending | AVX2-accelerated Gaussian blur & Sobel edge detection, benchmarked vs. scalar |
| **Detection** | ⏳ Not started | Region-growing anomaly detection |
| **Output** | ⏳ Not started | Annotated DICOM / JSON / PNG export |

## Highlights

- **Hand-rolled DICOM parser** — supports all 3 uncompressed transfer
  syntaxes (Explicit VR Little Endian, Implicit VR Little Endian, Explicit
  VR Big Endian), auto-detected per file. Cleanly rejects compressed
  syntaxes instead of misreading them.
- **Spacing-aware 3D reconstruction** — resamples unevenly-spaced slices
  onto a uniform grid via linear interpolation, rather than assuming
  perfectly even input.
- **AVX2 SIMD filters, correctness-verified against scalar** — Gaussian
  blur and Sobel edge detection both produce output that exactly matches
  their scalar reference implementation (max abs difference: 0), with
  real measured speedups (build with `-O2`; see below for why that matters).
- **Terminal visual previews** — both the ingestion and reconstruction
  layers render ASCII-art previews of the actual parsed/reconstructed
  data, so correctness is visually checkable, not just numeric.

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

> **Note on benchmarks:** `CMakeLists.txt` defaults to a `Release` build
> (`-O2`) specifically because SIMD speedup numbers are meaningless without
> optimization enabled on the scalar baseline too — an unoptimized build
> understates both paths and distorts the comparison.

## Repository layout

```
include/
  dicom_parser.h     — shared binary DICOM parsing core (used by every module)
  simd_filters.h      — generic scalar/AVX2 convolution engine + named filters
src/
  ingestion/          — single-slice metadata/pixel extraction + preview
  reconstruction/      — multi-slice loading, sorting, HU conversion, 3D stacking
  processing/           — Gaussian blur + Sobel edges (scalar + AVX2), histogram equalization
  detection/            — region-growing anomaly detection (upcoming)
  output/                — annotated DICOM/JSON/PNG export (upcoming)
samples/                — test files, including a synthetic multi-slice series with a deliberate spacing gap
ARCHITECTURE.md        — full design doc: all 5 layers, data structures, concurrency plan, roadmap
HANDOFF.md              — authoritative, honest status: what's tested vs. what's left
```

## Tech stack

C++20, CMake, AVX2 intrinsics. DCMTK is used only as an external reference
tool (`dcmdump`) to validate parser output during development — it is not
linked into the build.
