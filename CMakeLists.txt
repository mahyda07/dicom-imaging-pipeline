cmake_minimum_required(VERSION 3.20)
project(dicom_imaging_pipeline CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Week 5: ingestion layer — DICOM tag/pixel parser
add_executable(ingestion_test
    src/ingestion/main.cpp
)
target_include_directories(ingestion_test PRIVATE include)
