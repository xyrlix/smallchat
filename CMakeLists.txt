cmake_minimum_required(VERSION 3.16)
project(SmallChat VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 包含目录
include_directories(
    ${CMAKE_SOURCE_DIR}/include
)

# 源文件
set(SERVER_SOURCES
    src/server.cpp
    src/main_server.cpp
)

set(CLIENT_SOURCES
    src/client.cpp
    src/main_client.cpp
)

# 库
set(EXTRA_LIBS pthread)

# 服务器库
add_library(smallchat_server_lib STATIC
    src/server.cpp
)

# 客户端库
add_library(smallchat_client_lib STATIC
    src/client.cpp
)

# 服务器可执行文件
add_executable(smallchat_server ${SERVER_SOURCES})
target_link_libraries(smallchat_server PRIVATE smallchat_server_lib ${EXTRA_LIBS})

# 客户端可执行文件
add_executable(smallchat_client ${CLIENT_SOURCES})
target_link_libraries(smallchat_client PRIVATE smallchat_client_lib smallchat_server_lib ${EXTRA_LIBS})

# 安装
install(TARGETS smallchat_server smallchat_client
    RUNTIME DESTINATION bin
)

# 测试程序
add_executable(test_chat examples/test_chat.cpp)
target_link_libraries(test_chat PRIVATE smallchat_server_lib smallchat_client_lib ${EXTRA_LIBS})

# 打印信息
message(STATUS "Building SmallChat")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Install prefix: ${CMAKE_INSTALL_PREFIX}")
