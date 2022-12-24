cmake_minimum_required(VERSION 3.5)
project(game-server)

## use this to globally use C++11 with in our project
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")
set(CMAKE_CXX_STANDARD 11)

## load in pkg-config support
find_package(PkgConfig)
## use pkg-config to get hints for 0mq locations
pkg_check_modules(PC_ZeroMQ QUIET zmq)
find_package(nlohmann_json)
## use the hint from above to find where 'zmq.hpp' is located
find_path(ZeroMQ_INCLUDE_DIR
        NAMES zmq.hpp
        PATHS ${PC_ZeroMQ_INCLUDE_DIRS}
        )

## use the hint from about to find the location of libzmq
find_library(ZeroMQ_LIBRARY
        NAMES zmq
        PATHS ${PC_ZeroMQ_LIBRARY_DIRS}
        )

set(SOURCE_FILES server.cpp)
add_executable(server ${SOURCE_FILES})

## add the include directory to our compile directives
target_include_directories(server PUBLIC ${ZeroMQ_INCLUDE_DIR})
## at the 0mq library to our link directive

target_link_libraries(server PUBLIC ${ZeroMQ_LIBRARY} nlohmann_json::nlohmann_json)
