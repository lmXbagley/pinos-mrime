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

# ---- настольный CAD (SDL2 + OpenGL) ----
set(MP_GUI_SOURCES
  src/gui/app.cpp
  src/gui/camera.cpp
  src/gui/draw.cpp
  src/gui/inspector.cpp
  src/gui/main.cpp
  src/gui/panels.cpp
  src/gui/scene.cpp
  src/gui/widgets.cpp
)

if(WIN32)
  find_package(SDL2 QUIET)
  if(SDL2_FOUND)
    message(STATUS "MINOSPRIME GUI: включён (SDL2)" )
    add_executable(minosprime_gui WIN32 ${MP_GUI_SOURCES})
    target_include_directories(minosprime_gui PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(minosprime_gui PRIVATE mp_core ${SDL2_LIBRARIES} opengl32)
  else()
    message(STATUS "MINOSPRIME GUI: пропущен — SDL2 не найден (ядро и тесты строятся)")
  endif()
elseif(APPLE)
  find_package(SDL2 QUIET)
  if(SDL2_FOUND)
    message(STATUS "MINOSPRIME GUI: включён (SDL2)" )
    add_executable(minosprime_gui MACOSX_BUNDLE ${MP_GUI_SOURCES})
    target_include_directories(minosprime_gui PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(minosprime_gui PRIVATE mp_core ${SDL2_LIBRARIES}
      "-framework OpenGL")
  else()
    message(STATUS "MINOSPRIME GUI: пропущен — SDL2 не найден (brew install sdl2)")
  endif()
else()
  find_package(PkgConfig QUIET)
  find_library(SDL2_LIBRARY NAMES SDL2)
  find_path(SDL2_INCLUDE_DIR NAMES SDL.h PATH_SUFFIXES SDL2)
  if(SDL2_LIBRARY AND SDL2_INCLUDE_DIR)
    message(STATUS "MINOSPRIME GUI: включён (SDL2: ${SDL2_LIBRARY})")
    add_executable(minosprime_gui ${MP_GUI_SOURCES})
    target_include_directories(minosprime_gui PRIVATE ${SDL2_INCLUDE_DIR})
    target_link_libraries(minosprime_gui PRIVATE mp_core ${SDL2_LIBRARY} GL m)
  else()
    message(STATUS "MINOSPRIME GUI: пропущен — SDL2 не найден (sudo apt install libsdl2-dev)")
  endif()
endif()
