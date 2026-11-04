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

add_library(qmap_core STATIC src/device.cpp src/qasm_io.cpp)
target_include_directories(qmap_core PUBLIC include)
target_link_libraries(qmap_core PUBLIC nlohmann_json::nlohmann_json)
