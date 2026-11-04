cmake_minimum_required(VERSION 3.24)

# sm_86 (RTX 3060) as SASS, sm_120 (RTX 5070/5080) as SASS + PTX.
set(CMAKE_CUDA_ARCHITECTURES "86-real;120")
project(qmap LANGUAGES CXX CUDA)

set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/utf-8> $<$<COMPILE_LANGUAGE:CXX>:/W4>
                      $<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=/utf-8>)
endif()
# Source line info for Nsight Compute; does not change generated code.
add_compile_options($<$<COMPILE_LANGUAGE:CUDA>:--generate-line-info>)

find_package(CUDAToolkit REQUIRED)

include(FetchContent)
FetchContent_Declare(
  json
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(json)

add_library(qmap_core STATIC src/device.cpp src/qasm_io.cpp src/route_circuit.cpp src/output.cpp src/ga.cpp)
target_include_directories(qmap_core PUBLIC include)
target_link_libraries(qmap_core PUBLIC nlohmann_json::nlohmann_json)

add_library(qmap_gpu STATIC src/gpu_fitness.cu)
target_include_directories(qmap_gpu PUBLIC include)
target_link_libraries(qmap_gpu PUBLIC qmap_core PRIVATE CUDA::cudart CUDA::nvtx3)

add_executable(qmap_eval src/eval_main.cpp)
target_link_libraries(qmap_eval PRIVATE qmap_core)

enable_testing()
add_executable(qmap_tests tests/test_main.cpp)
target_link_libraries(qmap_tests PRIVATE qmap_core)
target_compile_definitions(qmap_tests PRIVATE QMAP_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../data")
add_test(NAME qmap_tests COMMAND qmap_tests)

add_executable(qmap_gpu_tests tests/test_gpu.cpp)
target_link_libraries(qmap_gpu_tests PRIVATE qmap_gpu)
target_compile_definitions(qmap_gpu_tests PRIVATE QMAP_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../data")
add_test(NAME qmap_gpu_tests COMMAND qmap_gpu_tests)

add_executable(qmap_ga src/ga_main.cpp)
target_link_libraries(qmap_ga PRIVATE qmap_core qmap_gpu CUDA::nvtx3)

add_executable(qmap_ga_tests tests/test_ga.cpp)
target_link_libraries(qmap_ga_tests PRIVATE qmap_core)
target_compile_definitions(qmap_ga_tests PRIVATE QMAP_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../data")
add_test(NAME qmap_ga_tests COMMAND qmap_ga_tests)
