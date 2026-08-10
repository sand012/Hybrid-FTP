cmake_minimum_required(VERSION 3.10)
project(HybridFTP CXX)
 
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
 
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

# ============================================================
#  Core library — RDT + common utilities
# ============================================================
add_library(HybridFTP_Core
    src/rdt/UDPSocket.cpp
    src/rdt/CustomHeader.cpp
    src/rdt/SlidingWindow.cpp      # Flow Control + Congestion Control (GBN + Slow Start + AIMD)
    src/rdt/ReliableTransfer.cpp   # Wrapper: RDTSender/RDTReceiver -> RDTWindowSender/Receiver
    src/common/CommandParser.cpp
    src/common/CryptoHash.cpp
    src/common/FileHandler.cpp
    
)
target_include_directories(HybridFTP_Core PUBLIC
    src/rdt
    src/common
)
target_link_libraries(HybridFTP_Core PRIVATE Threads::Threads OpenSSL::SSL OpenSSL::Crypto)

# ============================================================
#  FTP Server & Client
# ============================================================
add_executable(ftp_server
    src/server/main_server.cpp
    src/server/ServerManager.cpp
    src/server/Session.cpp
    src/common/CommandParser.cpp
    src/common/PathManager.cpp
)
target_link_libraries(ftp_server PRIVATE Threads::Threads HybridFTP_Core OpenSSL::SSL OpenSSL::Crypto)
 
add_executable(ftp_client
    src/client/main_client.cpp
    src/client/ClientCLI.cpp
)
target_include_directories(ftp_client PRIVATE src/rdt src/common)
target_link_libraries(ftp_client PRIVATE Threads::Threads HybridFTP_Core OpenSSL::SSL OpenSSL::Crypto)

# ============================================================
#  RDT File Transfer — nghiệm thu Sliding Window (Go-Back-N)
# ============================================================
add_executable(rdt_file_server
    src/rdt/FileTransferServer.cpp
)
target_include_directories(rdt_file_server PRIVATE src/rdt)
target_link_libraries(rdt_file_server PRIVATE HybridFTP_Core)

add_executable(rdt_file_client
    src/rdt/FileTransferClient.cpp
)
target_include_directories(rdt_file_client PRIVATE src/rdt)
target_link_libraries(rdt_file_client PRIVATE HybridFTP_Core)



include(CTest)

if(BUILD_TESTING)
    # This developer-only integration test is not present in every checkout.
    # Only register the target when its source file is available so a normal
    # configure/build is not prevented by an optional test.
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/client/test_e2e_dev3.cpp")
        add_executable(test_e2e_dev3
            src/client/test_e2e_dev3.cpp
        )
        target_include_directories(test_e2e_dev3 PRIVATE src/rdt src/common)
        target_link_libraries(test_e2e_dev3 PRIVATE HybridFTP_Core OpenSSL::SSL OpenSSL::Crypto Threads::Threads)
        add_test(NAME test_e2e_dev3 COMMAND test_e2e_dev3)
    endif()

    add_executable(path_manager_listing_test
        tests/PathManagerListingTest.cpp
        src/common/PathManager.cpp
    )
    target_include_directories(path_manager_listing_test PRIVATE src/common)
    add_test(NAME path_manager_listing_test COMMAND path_manager_listing_test)
endif()

