cmake_minimum_required(VERSION 3.16)

project(MiniRedis CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(MINIREDIS_BUILD_TESTS "Build unit tests" OFF)

add_library(miniredis_core STATIC
        src/core/sds.cpp
        src/core/skipList.cpp
        src/object/redisObject.cpp
        src/storage/inMemoryDB.cpp
        src/protocol/respParser.cpp
        src/command/commandDispatcher.cpp
        src/persistence/aof.cpp
        )

# 顺序很重要:必须写在 add_library 之后
target_include_directories(miniredis_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# 网络库 + 服务器:myMuduo 用 epoll/eventfd,只能在 Linux 编(容器里 build);Mac 上跳过
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(MUDUO_SRC
            muduo/src/Acceptor.cpp
            muduo/src/Buffer.cpp
            muduo/src/Channel.cpp
            muduo/src/CurrentThread.cpp
            muduo/src/DefaultPoller.cpp
            muduo/src/EPollPoller.cpp
            muduo/src/EventLoop.cpp
            muduo/src/EventLoopThread.cpp
            muduo/src/EventLoopThreadPool.cpp
            muduo/src/InetAddress.cpp
            muduo/src/Poller.cpp
            muduo/src/Socket.cpp
            muduo/src/TcpConnection.cpp
            muduo/src/TcpServer.cpp
            muduo/src/Thread.cpp
            muduo/src/TimerQueue.cpp
            )
    add_library(myMuduoLib STATIC ${MUDUO_SRC})
    target_include_directories(myMuduoLib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/muduo/include)

    add_executable(miniredis_server src/main.cpp src/net/redisServer.cpp)
    target_include_directories(miniredis_server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(miniredis_server PRIVATE miniredis_core myMuduoLib pthread)
endif()

if(MINIREDIS_BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    enable_testing()

    foreach(test_name test_sds test_dict test_object test_storage test_resp test_command test_skiplist)
        add_executable(${test_name} tests/${test_name}.cpp)
        target_link_libraries(${test_name} PRIVATE miniredis_core GTest::gtest_main)
        add_test(NAME ${test_name} COMMAND ${test_name})
    endforeach()
endif()
