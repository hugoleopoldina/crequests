cmake_minimum_required(VERSION 3.15)
project(crequests LANGUAGES C)

option(CREQUESTS_BUILD_EXAMPLE "Compilar o programa de exemplo (examples/example.c)" ON)

include(FetchContent)

# ---------------------------------------------------------------------
# cJSON: usado para os dicionários de headers/cookies/query params
# (fields.h) e para corpos JSON (creq_request_set_json_body/creq_post_json).
# ---------------------------------------------------------------------
set(ENABLE_CJSON_TEST OFF CACHE BOOL "" FORCE)
set(ENABLE_CJSON_UTILS OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(cJSON
    GIT_REPOSITORY https://github.com/DaveGamble/cJSON.git
    GIT_TAG master)

# ---------------------------------------------------------------------
# ctinylogger: logger usado em toda a lib (log_debug/log_info/
# log_warning/log_error). Ver https://github.com/hugoleopoldina/ctinylogger
# ---------------------------------------------------------------------
set(CTINYLOGGER_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
FetchContent_Declare(ctinylogger
    GIT_REPOSITORY https://github.com/hugoleopoldina/ctinylogger.git
    GIT_TAG main)

FetchContent_MakeAvailable(cJSON ctinylogger)

# ---------------------------------------------------------------------
# OpenSSL: necessário para HTTPS (tls.c). Precisa estar instalado no
# sistema -- ver README.md para instruções por plataforma (Linux,
# Windows, Termux).
# ---------------------------------------------------------------------
find_package(OpenSSL REQUIRED)

add_library(crequests STATIC
    src/types.c
    src/url.c
    src/internal.c
    src/request.c
    src/fields.c
    src/response.c
    src/parser.c
    src/request_builder.c
    src/session.c
    src/socket.c
    src/tls.c
    src/client.c
)

target_include_directories(crequests
    PUBLIC
	${cjson_SOURCE_DIR}
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(crequests
    PUBLIC
        cjson
        ctinylogger
        OpenSSL::SSL
        OpenSSL::Crypto
)

if(WIN32)
    # ws2_32 (Winsock) é referenciado via #pragma comment em socket.h
    # ao compilar com MSVC; no MinGW isso é ignorado e precisamos
    # linkar explicitamente.
    target_link_libraries(crequests PUBLIC ws2_32)
    target_compile_definitions(crequests PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads REQUIRED)
    target_link_libraries(crequests PUBLIC Threads::Threads)
endif()

target_compile_options(crequests PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang>:-Wall -Wextra>
)

if(CREQUESTS_BUILD_EXAMPLE)
    add_executable(crequests_example examples/example.c)
    target_link_libraries(crequests_example PRIVATE crequests)
endif()

include(GNUInstallDirs)

install(TARGETS crequests
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY include/crequests
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
