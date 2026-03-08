cmake_minimum_required(VERSION 3.31)
project(DLLInjector VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (NOT WIN32)
	message(SEND_ERROR "Only supported platform is Windows")
endif ()

if (NOT (CMAKE_SIZEOF_VOID_P EQUAL 8 OR CMAKE_SIZEOF_VOID_P EQUAL 4))
	message(FATAL_ERROR "Only supported architectures are 'x64' and 'x86'")
endif()

set(SOURCES_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
add_executable(${PROJECT_NAME} ${SOURCES_DIR}/main.cpp)