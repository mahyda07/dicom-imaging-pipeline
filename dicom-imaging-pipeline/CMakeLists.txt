cmake_minimum_required(VERSION 3.20)
project(dicom_imaging_pipeline CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Week 5: ingestion layer — DICOM tag/pixel parser
add_executable(ingestion_test
    src/ingestion/main.cpp
)
target_include_directories(ingestion_test PRIVATE include)

# Week 6: reconstruction layer — stacks slices into a 3D volume
add_executable(reconstruction_test
    src/reconstruction/main.cpp
)
target_include_directories(reconstruction_test PRIVATE include)

# Week 6: processing layer — AVX2-accelerated filters, benchmarked vs scalar
add_executable(processing_test
    src/processing/main.cpp
)
target_include_directories(processing_test PRIVATE include)
target_compile_options(processing_test PRIVATE -mavx2)
