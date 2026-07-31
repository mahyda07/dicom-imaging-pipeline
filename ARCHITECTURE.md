# Architecture Design Document
## High-Performance Medical Imaging Pipeline (DICOM Processor)
### Tynovate Studio 2026 — Track B

---

## 1. Overview

This system ingests raw DICOM scan files, reconstructs them into a 3D
volume, applies image enhancement, detects anomalous density regions,
and exports structured findings. It is organized as a strict five-stage
pipeline — each stage consumes the previous stage's output and has no
knowledge of stages beyond its immediate neighbor. This keeps each
layer independently testable and lets layers be developed and
validated in isolation before wiring them together.

```
Raw DICOM files
      │
      ▼
┌─────────────────┐
│ Ingestion       │  parses binary DICOM, extracts metadata + pixel data
└─────────────────┘
      │
      ▼
┌─────────────────┐
│ Reconstruction  │  stacks 2D slices into a 3D voxel grid
└─────────────────┘
      │
      ▼
┌─────────────────┐
│ Processing      │  SIMD-accelerated filters, parallelized via thread pool
└─────────────────┘
      │
      ▼
┌─────────────────┐
│ Detection       │  region-growing anomaly detection on the volume
└─────────────────┘
      │
      ▼
┌─────────────────┐
│ Output          │  annotated DICOM / JSON / PNG export
└─────────────────┘
```

---

## 2. Layer-by-layer design

### 2.1 Ingestion Layer — **implemented (Week 5)**

Responsible for reading a raw `.dcm` file and producing an in-memory
representation of one slice: its metadata and its pixel array.

**Data structures:**
```cpp
struct DicomElement {
    uint16_t group;
    uint16_t element;
    std::string vr;
    std::vector<uint8_t> value;
};

struct SliceMetadata {
    std::string modality;       // e.g. "CT"
    uint16_t rows;
    uint16_t columns;
    uint16_t bitsAllocated;
    double sliceLocation;       // position along the scan axis (Week 6 input)
};

struct DicomSlice {
    SliceMetadata meta;
    std::vector<uint8_t> pixelData;
};
```

**Design decisions:**
- The parser is hand-written against the DICOM tag/VR/length/value
  format directly rather than depending on DCMTK for parsing logic.
  DCMTK is used only as an external validation reference (see §4),
  never linked into the parsing path itself — the point of the
  internship is demonstrating we can implement the binary format
  ourselves.
- Currently supports **Explicit VR Little Endian** (the transfer
  syntax used by our validated sample file). **Implicit VR** and
  **Big Endian** support are the immediate next hardening step before
  Week 5 is considered fully closed out, since real scanner exports
  aren't guaranteed to use Explicit VR.
- Self-verification is built in: after parsing, the pipeline checks
  that `PixelData.size() == Rows × Columns × (BitsAllocated / 8)` and
  flags a mismatch rather than silently proceeding with bad data.

### 2.2 Reconstruction Layer — **planned (Week 6)**

Takes a series of `DicomSlice` objects (ordered by `sliceLocation`)
and stacks them into one 3D voxel grid.

**Data structures:**
```cpp
struct VoxelGrid {
    std::vector<int16_t> voxels;  // flattened 3D array, Hounsfield units
    size_t width, height, depth;
    double voxelSpacing[3];       // physical mm per voxel, per axis
};
```

**Design decisions:**
- Slices are normalized to Hounsfield Units (HU) during stacking,
  since HU is what makes CT density values comparable across
  different scanners and settings.
- Trilinear interpolation handles cases where slice spacing isn't
  perfectly uniform, so the resulting grid represents real physical
  space rather than just "slice index space."

### 2.3 Processing Layer — **planned (Week 6–7)**

Applies AVX2 SIMD-accelerated filters (Gaussian blur, edge detection,
histogram equalization) across the voxel grid, parallelized across
slices using a custom thread pool (built from `std::thread`,
`std::mutex`, `std::condition_variable` — no `std::async`).

**Concurrency model:**
- One thread pool, sized to `std::thread::hardware_concurrency()`.
- Work unit = one slice's filter pass — slices are independent, so
  this parallelizes with no shared mutable state and no locking
  needed inside the hot path itself, only in the work queue.
- Benchmarks will compare single-threaded vs. pooled throughput to
  quantify the speedup, per the internship's evaluation criteria.

### 2.4 Detection Layer — **planned (Week 7)**

3D region-growing: starting from seed voxels within a suspicious HU
range, expands outward to neighboring voxels within a density
threshold, producing connected regions with bounding boxes and
density scores.

### 2.5 Output Layer — **planned (Week 8)**

Serializes results as annotated DICOM, a JSON findings report, and
PNG slice exports (optionally HL7 FHIR JSON for EHR integration).

---

## 3. Cross-cutting concerns

- **Error handling:** every layer validates its own input size/shape
  assumptions before processing (e.g. ingestion checks pixel data
  size; reconstruction will check consistent slice dimensions before
  stacking) rather than assuming well-formed input.
- **Testing strategy:** each layer gets unit tests using small,
  hand-verifiable inputs (e.g. a slice with known Rows/Columns/HU
  values) plus one integration test using a real downloaded sample
  file, so correctness isn't only checked against synthetic data.

---

## 4. Independent verification

A secondary reference check is used throughout: DCMTK's `dcmdump`
tool is run against the same sample files, and our parser's extracted
values (modality, dimensions, pixel data size) are manually
cross-checked against its output. This is not part of the build or
runtime path — it's a validation step during development.

---

## 5. Week-by-week roadmap

| Week | Focus | Status |
|---|---|---|
| 5 | Environment setup, custom DICOM parser, metadata + pixel extraction | Done — all 3 uncompressed transfer syntaxes, tested |
| 6 | 3D volume reconstruction, HU normalization, resampling, SIMD filters | Done — see HANDOFF.md for measured benchmark numbers |
| 7 | Custom thread pool, region-growing detection | Not started |
| 8 | Output/reporting layer, benchmarking, demo, final write-up | Not started |

**See `HANDOFF.md` for the authoritative, detailed status — this table is
a summary only.**
