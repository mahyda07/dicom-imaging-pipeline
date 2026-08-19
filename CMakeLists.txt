cmake_minimum_required(VERSION 3.20)
project(dicom_imaging_pipeline CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O2")

add_executable(ingestion_test src/ingestion/main.cpp)
target_include_directories(ingestion_test PRIVATE include)

add_executable(reconstruction_test src/reconstruction/main.cpp)
target_include_directories(reconstruction_test PRIVATE include)

add_executable(processing_test src/processing/main.cpp)
target_include_directories(processing_test PRIVATE include)
target_compile_options(processing_test PRIVATE -mavx2)

find_package(Threads REQUIRED)
target_link_libraries(processing_test PRIVATE Threads::Threads)

add_executable(detection_test src/detection/main.cpp)
target_include_directories(detection_test PRIVATE include)

add_executable(thread_pool_test tests/thread_pool_test.cpp)
target_include_directories(thread_pool_test PRIVATE include)
target_link_libraries(thread_pool_test PRIVATE Threads::Threads)

add_executable(output_test src/output/main.cpp)
target_include_directories(output_test PRIVATE include third_party)
