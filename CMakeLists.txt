cmake_minimum_required(VERSION 3.16)
project(minosprime CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra -Wpedantic)

find_package(Threads REQUIRED)

add_library(mp_core STATIC
  src/mp/json.cpp
  src/mp/model.cpp
  src/mp/serialize.cpp
  src/mp/undo.cpp
  src/mp/dsl.cpp
  src/mp/engine.cpp
  src/mp/validator.cpp
  src/mp/stands.cpp
  src/mp/api.cpp
)
target_include_directories(mp_core PUBLIC src)
target_link_libraries(mp_core PUBLIC Threads::Threads)

add_executable(minosprime src/main_headless.cpp)
target_link_libraries(minosprime PRIVATE mp_core)

add_executable(mp_test
  tests/main.cpp
  tests/test_json.cpp
  tests/test_model.cpp
  tests/test_serialize_undo.cpp
  tests/test_dsl.cpp
  tests/test_engine.cpp
  tests/test_validator_stands_api.cpp
)
target_link_libraries(mp_test PRIVATE mp_core)

enable_testing()
add_test(NAME all COMMAND mp_test)
