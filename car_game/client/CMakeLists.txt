cmake_minimum_required(VERSION 3.5)
project(game-client)

## use this to globally use C++11 with in our project
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")
set(CMAKE_CXX_STANDARD 11)

## load in pkg-config support
find_package(PkgConfig)
## use pkg-config to get hints for 0mq locations
pkg_check_modules(PC_ZeroMQ QUIET zmq)

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
find_package(nlohmann_json)
set(SOURCE_FILES client.cpp)
add_executable(client ${SOURCE_FILES})

## add the include directory to our compile directives
target_include_directories(client PUBLIC ${ZeroMQ_INCLUDE_DIR})
## at the 0mq library to our link directive



find_package(SFML 2.5 COMPONENTS system window graphics network audio REQUIRED)
include_directories(${SFML_INCLUDE_DIRS})
target_link_libraries(client PUBLIC ${ZeroMQ_LIBRARY} sfml-system sfml-window sfml-graphics sfml-audio sfml-network "-lX11" nlohmann_json::nlohmann_json)
