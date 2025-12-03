cmake_minimum_required(VERSION 3.10)
set(CMAKE_TOOLCHAIN_FILE "$ENV{HOME}/vcpkg/scripts/buildsystems/vcpkg.cmake"
    CACHE STRING "Vcpkg toolchain file")
project(Forum LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include_directories(${CMAKE_SOURCE_DIR}/include)
find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
add_executable(main main.cpp)
if(WIN32)
    message(STATUS "Detected Windows build")
    target_compile_definitions(main PRIVATE
        _WIN32
        WIN32_LEAN_AND_MEAN
        _WIN32_WINNT=0x0A00
    )
    target_link_libraries(main PRIVATE
        ws2_32
        crypt32
        iphlpapi
        winpthread
    )
    if(MINGW)
        target_link_options(main PRIVATE
            -static
            -static-libgcc
            -static-libstdc++
        )
    endif()
else()
    message(STATUS "Detected Linux build")
    target_link_libraries(main PRIVATE
        pthread
        dl
    )
endif()
target_link_libraries(main PRIVATE
    unofficial::sqlite3::sqlite3
    nlohmann_json::nlohmann_json
)