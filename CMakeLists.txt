cmake_minimum_required(VERSION 3.31)
project(DLLInjector VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (!WIN32)
	message(SEND_ERROR "Only supported platform is Windows")
endif ()

if (CMAKE_SIZEOF_VOID_P EQUAL 8)
	set(ARCHITECTURE "x64")
elseif (CMAKE_SIZEOF_VOID_P EQUAL 4)
	set(ARCHITECTURE "x86")
else ()
	message(SEND_ERROR "Only supported architectures are 'x64' and 'x86'")
endif ()

set(DEPS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/deps)
set(SOURCE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/src)

file(GLOB_RECURSE SOURCES ${SOURCE_DIRS}/*.cpp)

add_executable(${PROJECT_NAME} ${SOURCES})
target_include_directories(${PROJECT_NAME} PRIVATE ${SOURCE_DIRS} ${DEPS_ROOT}/ASTD/include)
target_compile_definitions(${PROJECT_NAME} PRIVATE
	$<$<CONFIG:Debug>:BUILD_DEBUG=1>
	$<$<CONFIG:Release>:BUILD_RELEASE=1>
	ASTD_USE_UNICODE=0
)