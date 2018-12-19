cmake_minimum_required(VERSION 3.5)

project(Example CXX C)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake_modules/")

#global build subdirectories
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

#global compiler
if(CMAKE_VERSION VERSION_LESS "3.8")
  include(CheckCXXCompilerFlag)
  CHECK_CXX_COMPILER_FLAG("-std=c++17" COMPILER_SUPPORTS_CXX17)
  if(NOT COMPILER_SUPPORTS_CXX17)
    message(FATAL_ERROR "The compiler ${CMAKE_CXX_COMPILER} has no C++17 support.")
  endif()
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17")
  include(CheckCCompilerFlag)
  CHECK_C_COMPILER_FLAG("-std=gnu99" COMPILER_SUPPORTS_GNU99)
  if(NOT COMPILER_SUPPORTS_GNU99)
    message(FATAL_ERROR "The compiler ${CMAKE_C_COMPILER} has no GNU C99 support.")
  endif()
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=gnu99")
else()
  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_EXTENSIONS OFF)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_C_STANDARD 99)
  set(CMAKE_C_EXTENSIONS OFF)
  set(C_STANDARD_REQUIRED ON)
endif()

#global packages
## thread
find_package(Threads REQUIRED)
set(CMAKE_REQUIRED_LIBRARIES pthread)
set(common_deps
  ${CMAKE_THREAD_LIBS_INIT})

## boost
set(BOOST_COMPONENTS
    atomic chrono thread system regex random program_options date_time iostreams)
set(BOOST_HEADER_COMPONENTS container)
### boost context
list(APPEND BOOST_COMPONENTS context coroutine)
### seastar
list(APPEND BOOST_COMPONENTS filesystem timer)
set(Boost_USE_MULTITHREADED ON)
set(BOOST_J 1 CACHE STRING
    "max jobs for Boost build")
set(Boost_USE_STATIC_LIBS ON)
include(BuildBoost)
build_boost(1.67
    COMPONENTS ${BOOST_COMPONENTS} ${BOOST_HEADER_COMPONENTS})
include_directories(BEFORE SYSTEM ${Boost_INCLUDE_DIRS})

add_subdirectory(src)
