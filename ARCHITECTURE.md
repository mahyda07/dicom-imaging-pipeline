# Architecture Design Document
## High-Performance Medical Imaging Pipeline (DICOM Processor)
### Tynovate Studio 2026 — Track B

## 1. Overview

Five-stage pipeline, each stage consuming the previous stage's output:

```
Raw DICOM files -> Ingestion -> Reconstruction -> Processing -> Detection -> Output
```

## 2. Layer-by-layer design

### 2.1 Ingestion — implemented (Week 5)
Hand-written parser (`include/dicom_parser.h`). Supports Explicit VR LE,
Implicit VR LE, Explicit VR BE, auto-detected via the file's own
TransferSyntaxUID. Rejects compressed syntaxes cleanly. Self-verifies
extracted pixel data size.

### 2.2 Reconstruction — implemented (Week 6)
`include/volume.h`. Sorts slices by real SliceLocation, converts to
Hounsfield Units via each file's RescaleSlope/Intercept, resamples onto
uniform Z spacing via linear interpolation between the two nearest real
slices. (Linear along Z only — X/Y are already a uniform grid within each
slice, so full 3D trilinear isn't needed here.)

### 2.3 Processing — implemented (Weeks 6-7)
`include/simd_filters.h`, `include/thread_pool.h`. Gaussian blur and
Sobel edge detection, both separable (1D horizontal + 1D vertical pass),
implemented scalar and AVX2, correctness-verified to match exactly.
Histogram equalization is scalar-only (cumulative histogram = sequential
dependency, not a SIMD candidate). Custom thread pool
(`std::thread`/`std::mutex`/`std::condition_variable`) parallelizes
blurring across a volume's slices, chunked (not one task per slice) to
keep per-task overhead small relative to actual work.

### 2.4 Detection — implemented (Week 7)
3D region growing via 6-connected flood fill, which doubles as connected
component labeling. Threshold: 400 HU (soft tissue is roughly 0-100 HU,
dense/calcified tissue starts around 400+ HU clinically). Reports voxel
count, physical volume (using real voxel spacing), bounding box, mean HU
per region.

### 2.5 Output — not started (Week 8)
Planned: annotated DICOM export, JSON findings report, PNG slice export.

## 3. Roadmap

| Week | Focus | Status |
|---|---|---|
| 5 | DICOM parser | Done, tested |
| 6 | Reconstruction, SIMD filters | Done, tested |
| 7 | Thread pool, region-growing detection | Done, tested — see HANDOFF.md for the full thread-pool investigation |
| 8 | Output/reporting | Done — JSON + annotated PNG export; no DICOM re-export (scoped out, see HANDOFF.md) |

See `HANDOFF.md` for the authoritative, detailed status.
