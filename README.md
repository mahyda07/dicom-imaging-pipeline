# DICOM Imaging Pipeline

**Tynovate Studio 2026 Internship — Track B**

A C++20 pipeline that reads raw medical scanner files (DICOM/.dcm),
reconstructs them into a 3D volume, accelerates image processing with
hand-written AVX2 SIMD intrinsics and a custom thread pool, and detects
anomalous dense regions via 3D region growing.

> **Status:** Weeks 5–7 complete and tested. See [`HANDOFF.md`](HANDOFF.md)
> for a detailed, honest breakdown — read that before assuming anything
> here is further along than it is.

## What it does

| Stage | Status | What it does |
|---|---|---|
| **Ingestion** | ✅ Done | Custom binary parser extracts metadata + pixel data from raw `.dcm` files, all 3 uncompressed transfer syntaxes |
| **Reconstruction** | ✅ Done | Stacks 2D slices into a 3D volume, HU normalization, spacing-aware resampling |
| **Processing** | ✅ Done | AVX2-accelerated Gaussian blur & Sobel edges (benchmarked vs scalar) + custom thread pool parallelizing across slices |
| **Detection** | ✅ Done | 3D region-growing anomaly detector with connected component labeling, verified against a known synthetic target |
| **Output** | ⏳ Not started | Annotated DICOM / JSON / PNG export |

## Highlights

- **Hand-rolled DICOM parser** — all 3 uncompressed transfer syntaxes,
  auto-detected. Cleanly rejects compressed files instead of misreading them.
- **Spacing-aware 3D reconstruction** — resamples unevenly-spaced slices
  onto a uniform grid via linear interpolation.
- **AVX2 SIMD filters, correctness-verified against scalar** — Gaussian
  blur and Sobel edges both exactly match their scalar reference (max abs
  diff: 0), with real measured speedups.
- **Custom thread pool** — built from `std::thread` / `std::mutex` /
  `std::condition_variable` (no `std::async`), correctness-tested with 4
  dedicated unit tests (`tests/thread_pool_test.cpp`), parallelizes
  per-slice filtering across an entire volume with zero result difference
  from the single-threaded path.
- **Region-growing anomaly detection** — verified end-to-end against a
  known synthetic test target: a nodule injected at a known location was
  found by the detector at the *exact* bounding box, with no false
  positives, at a clinically-grounded density threshold (not a number
  tuned to make the test pass).

## Build & run

```bash
mkdir -p build && cd build
cmake ..
make
cd ..
./build/ingestion_test samples/ct_small.dcm
./build/reconstruction_test samples/series
./build/processing_test samples/ct_small.dcm       # single-slice AVX2 vs scalar
./build/processing_test samples/series_large         # whole-volume thread pool benchmark
./build/detection_test samples/series_large 400
./build/thread_pool_test
```

> **Note on benchmarks:** the build defaults to `Release` (`-O2`) —
> unoptimized builds understate both scalar and AVX2 paths and distort the
> comparison. Thread pool speedup depends entirely on how many cores your
> machine actually has (`nproc` to check) — with 1 core, no speedup is
> mathematically possible and the pool will show a small overhead instead,
> which is the *correct* result on that hardware, not a bug.

## Repository layout

```
include/
  dicom_parser.h      — shared binary DICOM parsing core
  volume.h              — shared 3D volume construction (used by reconstruction + detection)
  simd_filters.h          — generic scalar/AVX2 convolution engine + named filters
  thread_pool.h             — custom thread pool
src/
  ingestion/                 — single-slice metadata/pixel extraction + preview
  reconstruction/              — multi-slice loading, sorting, HU conversion, 3D stacking
  processing/                    — Gaussian blur + Sobel edges, thread-pooled volume benchmark
  detection/                       — 3D region-growing anomaly detector
  output/                            — annotated DICOM/JSON/PNG export (upcoming)
tests/
  thread_pool_test.cpp                 — thread pool correctness tests
samples/
  series/                                 — 8-slice test series (uneven spacing, for reconstruction)
  series_large/                             — 40-slice test series (synthetic, isolated injected nodule, for detection + threading benchmarks)
ARCHITECTURE.md                              — full design doc
HANDOFF.md                                     — authoritative, honest status
```

## Tech stack

C++20, CMake, AVX2 intrinsics, `std::thread`/`std::mutex`/`std::condition_variable`.
DCMTK is used only as an external reference tool (`dcmdump`) during
development — not linked into the build.
