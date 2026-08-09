cmake_minimum_required(VERSION 3.20)
project(dicom_imaging_pipeline CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Without an explicit build type, CMake defaults to no optimization (-O0),
# which understates real performance for both scalar and AVX2 code and makes
# the measured SIMD speedup meaningless. Default to Release unless the
# person building explicitly asks for something else (e.g. -DCMAKE_BUILD_TYPE=Debug).
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O2")

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

find_package(Threads REQUIRED)
target_link_libraries(processing_test PRIVATE Threads::Threads)

# Week 7: detection layer — region growing anomaly detection
add_executable(detection_test
    src/detection/main.cpp
)
target_include_directories(detection_test PRIVATE include)

# Week 7: thread pool correctness tests
add_executable(thread_pool_test
    tests/thread_pool_test.cpp
)
target_include_directories(thread_pool_test PRIVATE include)
target_link_libraries(thread_pool_test PRIVATE Threads::Threads)
