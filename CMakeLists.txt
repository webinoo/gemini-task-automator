cmake_minimum_required(VERSION 3.20)
project(GeminiTaskAutomator CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(CURL REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_executable(GeminiAutomator
    src/main.cpp
    src/app.rc
)

target_link_libraries(GeminiAutomator PRIVATE
    CURL::libcurl
    nlohmann_json::nlohmann_json
)

# Tell MSVC linker not to generate an extra conflicting manifest
target_link_options(GeminiAutomator PRIVATE /MANIFEST:NO)