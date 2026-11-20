cmake_minimum_required(VERSION 3.20)
project(chess3d C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

include(FetchContent)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(raylib
  GIT_REPOSITORY https://github.com/raysan5/raylib.git
  GIT_TAG 5.5
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(raylib)

# Chess rules and battle mode have no raylib dependency so they can be tested headless.
add_library(chess STATIC src/chess/chess.cpp src/ai/ai.cpp src/battle/battle.cpp src/game/game.cpp)
target_include_directories(chess PUBLIC src)

if(EMSCRIPTEN)
  set(CMAKE_EXECUTABLE_SUFFIX ".html")
endif()

add_executable(chess3d src/main.cpp
  src/app/app.cpp src/render/pieces.cpp src/render/board_view.cpp src/ui/hud.cpp
  src/battle/battle_scene.cpp src/battle/battle_fx.cpp src/render/fx.cpp src/net/net.cpp src/net/online.cpp)
target_include_directories(chess3d PRIVATE src)
target_link_libraries(chess3d PRIVATE chess raylib)

if(EMSCRIPTEN)
  set_target_properties(chess3d PROPERTIES OUTPUT_NAME index)
  target_link_options(chess3d PRIVATE
    -sUSE_GLFW=3
    -sALLOW_MEMORY_GROWTH=1
    --shell-file ${CMAKE_SOURCE_DIR}/web/shell.html --pre-js ${CMAKE_SOURCE_DIR}/web/net.js)
else()
  enable_testing()
  add_executable(perft tests/perft.cpp)
  target_link_libraries(perft PRIVATE chess)
  add_test(NAME perft COMMAND perft)
  add_executable(rules tests/rules.cpp)
  target_link_libraries(rules PRIVATE chess)
  add_test(NAME rules COMMAND rules)
  add_executable(ai_test tests/ai.cpp)
  target_link_libraries(ai_test PRIVATE chess)
  add_test(NAME ai COMMAND ai_test)
  add_executable(battle_test tests/battle.cpp)
  target_link_libraries(battle_test PRIVATE chess)
  add_test(NAME battle COMMAND battle_test)
  add_executable(game_test tests/game.cpp)
  target_link_libraries(game_test PRIVATE chess)
  add_test(NAME game COMMAND game_test)
endif()
