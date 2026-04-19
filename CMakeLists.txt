cmake_minimum_required(VERSION 3.16)
project(SmallChat VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(
    ${CMAKE_SOURCE_DIR}/include
)

set(SERVER_SOURCES
    src/server.cpp
    src/common.cpp
    src/main_server.cpp
)

set(CLIENT_SOURCES
    src/client.cpp
    src/common.cpp
    src/main_client.cpp
)

set(EXTRA_LIBS pthread)

find_package(OpenSSL REQUIRED)
if(OPENSSL_FOUND)
    include_directories(${OPENSSL_INCLUDE_DIR})
    set(EXTRA_LIBS ${EXTRA_LIBS} ${OPENSSL_LIBRARIES})
    message(STATUS "OpenSSL found: ${OPENSSL_VERSION}")
else()
    message(FATAL_ERROR "OpenSSL not found")
endif()

find_package(PkgConfig)
pkg_check_modules(LIBWEBSOCKETS libwebsockets)
if(LIBWEBSOCKETS_FOUND)
    include_directories(${LIBWEBSOCKETS_INCLUDE_DIRS})
    set(EXTRA_LIBS ${EXTRA_LIBS} ${LIBWEBSOCKETS_LIBRARIES})
    message(STATUS "libwebsockets found: ${LIBWEBSOCKETS_VERSION}")
    set(HAVE_LIBWEBSOCKETS ON)
else()
    message(WARNING "libwebsockets not found. WebSocket server will not be built.")
    set(HAVE_LIBWEBSOCKETS OFF)
endif()

add_library(smallchat_server_lib STATIC
    src/server.cpp
    src/common.cpp
)

add_library(smallchat_client_lib STATIC
    src/client.cpp
    src/common.cpp
)

add_executable(smallchat_server ${SERVER_SOURCES})
target_link_libraries(smallchat_server PRIVATE smallchat_server_lib ${EXTRA_LIBS})

add_executable(smallchat_client ${CLIENT_SOURCES})
target_link_libraries(smallchat_client PRIVATE smallchat_client_lib smallchat_server_lib ${EXTRA_LIBS})

if(HAVE_LIBWEBSOCKETS)
    add_executable(smallchat_web_server 
        src/main_web.cpp 
        src/web_server.cpp 
        src/common.cpp
    )
    target_link_libraries(smallchat_web_server PRIVATE smallchat_server_lib ${EXTRA_LIBS})
    
    install(TARGETS smallchat_server smallchat_client smallchat_web_server
        RUNTIME DESTINATION bin
    )
else()
    install(TARGETS smallchat_server smallchat_client
        RUNTIME DESTINATION bin
    )
endif()

add_executable(test_chat examples/test_chat.cpp src/common.cpp)
target_link_libraries(test_chat PRIVATE smallchat_server_lib smallchat_client_lib ${EXTRA_LIBS})

file(COPY web/ DESTINATION ${CMAKE_BINARY_DIR}/web)

message(STATUS "Building SmallChat")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Install prefix: ${CMAKE_INSTALL_PREFIX}")