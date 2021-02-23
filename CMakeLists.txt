CMAKE_MINIMUM_REQUIRED(VERSION 3.0)
project(creeper)

ADD_DEFINITIONS(-DUNICODE)
ADD_DEFINITIONS(-D_UNICODE)
ADD_DEFINITIONS(-DDEBUG_ARGS="")

set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MT")
add_executable(creeper-installer WIN32
    src/main.cc
    src/app.rc
    src/app.manifest
)
