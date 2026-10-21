cmake_minimum_required(VERSION 3.10)
project(alpshell CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

option(ALPSHELL_SANITIZE "Build with AddressSanitizer and UBSan" OFF)

# GNU readline: the header and the library are both required.
find_path(READLINE_INCLUDE_DIR readline/readline.h)
find_library(READLINE_LIBRARY readline)
if(NOT READLINE_INCLUDE_DIR OR NOT READLINE_LIBRARY)
  message(FATAL_ERROR "GNU readline not found (Debian/Ubuntu: apt install libreadline-dev)")
endif()

file(GLOB ALPSHELL_LIB_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

add_library(alpshell_core STATIC ${ALPSHELL_LIB_SOURCES})
target_include_directories(alpshell_core PUBLIC include ${READLINE_INCLUDE_DIR})
target_link_libraries(alpshell_core PUBLIC ${READLINE_LIBRARY})
target_compile_options(alpshell_core PUBLIC -Wall -Wextra)
if(ALPSHELL_SANITIZE)
  target_compile_options(alpshell_core PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer
                                               -fno-sanitize-recover=undefined)
  target_link_options(alpshell_core PUBLIC -fsanitize=address,undefined)
endif()

add_executable(alpshell main.cpp)
target_link_libraries(alpshell PRIVATE alpshell_core)

# Unit tests and script tests (only once tests/ exists).
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/unit_tests.cpp)
  enable_testing()
  add_executable(unit_tests tests/unit_tests.cpp)
  target_link_libraries(unit_tests PRIVATE alpshell_core)
  add_test(NAME unit_tests COMMAND unit_tests)
  find_package(Python3 COMPONENTS Interpreter)
  if(Python3_FOUND)
    add_test(NAME script_tests
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_script_tests.py
                     --shell $<TARGET_FILE:alpshell>)
  endif()
endif()
