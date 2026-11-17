cmake_minimum_required(VERSION 3.20)
project(tinyobsw C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(OBSW_TESTS    "Build unit tests"                ON)
option(OBSW_SANITIZE "Build with ASan + UBSan"         OFF)
option(OBSW_COVERAGE "Build with coverage flags"       OFF)
option(OBSW_FUZZ     "Build libFuzzer targets (clang)" OFF)

if(CMAKE_CROSSCOMPILING)
  set(OBSW_TESTS OFF)
endif()

if(OBSW_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
  add_link_options(-fsanitize=address,undefined)
endif()
if(OBSW_COVERAGE)
  add_compile_options(--coverage -O0)
  add_link_options(--coverage)
endif()
if(OBSW_FUZZ)
  add_compile_options(-fsanitize=fuzzer-no-link,address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

add_library(tinyobsw STATIC
  src/crc16.c
  # add as you write them: src/ccsds.c src/pus.c src/instrument.c src/services/st01.c ...
)
target_include_directories(tinyobsw PUBLIC include)
target_compile_options(tinyobsw PRIVATE
  -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
  -ffunction-sections -fdata-sections)

if(NOT CMAKE_CROSSCOMPILING)
  add_executable(obsw_sim sim/main_posix.c)
  target_link_libraries(obsw_sim PRIVATE tinyobsw)
endif()

if(OBSW_TESTS)
  include(FetchContent)
  FetchContent_Declare(unity
    GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity.git
    GIT_TAG v2.6.0)
  FetchContent_MakeAvailable(unity)
  enable_testing()
  foreach(t crc16)            # add test names here: ccsds pus st01 ...
    add_executable(test_${t} tests/test_${t}.c)
    target_link_libraries(test_${t} PRIVATE tinyobsw unity)
    add_test(NAME ${t} COMMAND test_${t})
  endforeach()
endif()

if(OBSW_FUZZ)
  add_executable(fuzz_packet fuzz/fuzz_packet.c)
  target_link_libraries(fuzz_packet PRIVATE tinyobsw)
  target_link_options(fuzz_packet PRIVATE -fsanitize=fuzzer)
endif()
