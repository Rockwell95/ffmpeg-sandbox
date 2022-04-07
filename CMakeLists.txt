cmake_minimum_required(VERSION 3.17)

# set the project name
project(FFMPEGSandbox VERSION 0.1)

# specify the C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(CMAKE C_STANDARD 17)
set(CMAKE C_STANDARD_REQUIRED True)

set(CMAKE_CXX_FLAGS "-D__STDC_CONSTANT_MACROS")

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBAV REQUIRED IMPORTED_TARGET
    libavdevice
    libavfilter
    libavformat
    libavcodec
    libswresample
    libswscale
    libavutil
)

# add the executable
add_executable(${PROJECT_NAME} main.cpp KGCodec.cpp KGCodec.h)
add_executable(udp_redirect udp_redirect.c)

configure_file(Config.h.in Config.h)

target_include_directories(${PROJECT_NAME} 
    PUBLIC
        "${PROJECT_BINARY_DIR}"
)

target_link_libraries(${PROJECT_NAME} PkgConfig::LIBAV)