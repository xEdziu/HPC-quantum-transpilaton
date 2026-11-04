cmake_minimum_required(VERSION 3.24)
project(qmap LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/utf-8 /W4)
endif()

include(FetchContent)
FetchContent_Declare(
  json
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(json)

add_library(qmap_core STATIC src/device.cpp src/qasm_io.cpp src/route_circuit.cpp)
target_include_directories(qmap_core PUBLIC include)
target_link_libraries(qmap_core PUBLIC nlohmann_json::nlohmann_json)

add_executable(qmap_eval src/eval_main.cpp)
target_link_libraries(qmap_eval PRIVATE qmap_core)

enable_testing()
add_executable(qmap_tests tests/test_main.cpp)
target_link_libraries(qmap_tests PRIVATE qmap_core)
target_compile_definitions(qmap_tests PRIVATE QMAP_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../data")
add_test(NAME qmap_tests COMMAND qmap_tests)
