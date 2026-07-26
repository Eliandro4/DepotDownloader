cmake_minimum_required(VERSION 3.16)
project(depotdownloader_c C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_definitions(-D_GNU_SOURCE)

find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)
find_package(ZLIB REQUIRED)

add_subdirectory(../../SteamKit/steamkit_c steamkit_c_build)

set(DEPOT_DOWNLOADER_SOURCES
    src/main.c
    src/config.c
    src/persistence.c
    src/hash_map.c
    src/util.c
    src/steam3_session.c
    src/content_downloader.c
    src/proto_manifest.c
)

add_executable(depotdownloader ${DEPOT_DOWNLOADER_SOURCES})

target_include_directories(depotdownloader PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/../../SteamKit/steamkit_c/include
)

target_link_libraries(depotdownloader
    steamkit_c
    OpenSSL::SSL
    OpenSSL::Crypto
    CURL::libcurl
    ZLIB::ZLIB
    pthread
)