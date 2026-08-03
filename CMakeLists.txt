cmake_minimum_required(VERSION 3.10)
project(HybridFTP CXX)
 
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
 
find_package(Threads REQUIRED)


add_library(HybridFTP_Core
    src/rdt/UDPSocket.cpp
    src/rdt/CustomHeader.cpp
    src/rdt/ReliableTransfer.cpp
    src/common/CommandParser.cpp
    src/common/CryptoHash.cpp
    src/common/FileHandler.cpp
    src/common/TCPControl.cpp
)

target_link_libraries(HybridFTP_Core PRIVATE Threads::Threads)

add_executable(ftp_server
    src/server/main_server.cpp
    src/server/ServerManager.cpp
    src/server/Session.cpp
)
target_link_libraries(ftp_server PRIVATE Threads::Threads HybridFTP_Core)
 
add_executable(ftp_client
    src/client/main_client.cpp
    src/client/ClientCLI.cpp
)
target_link_libraries(ftp_client PRIVATE Threads::Threads HybridFTP_Core)