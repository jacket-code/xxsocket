cmake_minimum_required(VERSION 3.16)

project(texas_holdem_server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(THS_BUILD_TESTS "Build tests" ON)

add_library(xxsocket
  src/xxsocket.cpp
)
target_include_directories(xxsocket PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_library(poker_engine
  src/poker/card.cpp
  src/poker/hand_eval.cpp
  src/poker/holdem_table.cpp
)
target_include_directories(poker_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_executable(poker_server
  src/server/main.cpp
  src/server/tcp_server.cpp
  src/server/app_server.cpp
  src/server/store.cpp
  # legacy single-table protocol (kept for reference; not used by main)
  src/server/protocol.cpp
)
target_include_directories(poker_server PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(poker_server PRIVATE xxsocket poker_engine)

if(THS_BUILD_TESTS)
  enable_testing()
  add_executable(poker_engine_tests
    src/tests/test_engine.cpp
  )
  target_include_directories(poker_engine_tests PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(poker_engine_tests PRIVATE poker_engine)
  add_test(NAME poker_engine_tests COMMAND poker_engine_tests)
endif()
