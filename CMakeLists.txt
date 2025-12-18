cmake_minimum_required(VERSION 3.20)

project(microstructure_sim
  VERSION 0.1.0
  DESCRIPTION "Event-driven limit order book + matching engine simulator"
  LANGUAGES CXX
)

# ---------------- Options ----------------
option(MSIM_ENABLE_WERROR "Treat warnings as errors" OFF)
option(MSIM_ENABLE_SANITIZERS "Enable ASan/UBSan (Clang/GCC)" OFF)
option(MSIM_BUILD_GATEWAY "Build local HTTP gateway UI executable" ON)

# ---------------- Language setup ----------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------- Warnings (define BEFORE use) ----------------
function(msim_set_warnings target)
  if (MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive-)
    if (MSIM_ENABLE_WERROR)
      target_compile_options(${target} PRIVATE /WX)
    endif()
  else()
    target_compile_options(${target} PRIVATE
      -Wall -Wextra -Wpedantic
      -Wshadow -Wconversion -Wsign-conversion
      -Wnon-virtual-dtor -Wold-style-cast
      -Woverloaded-virtual -Wnull-dereference
    )
    if (MSIM_ENABLE_WERROR)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  endif()
endfunction()

# ---------------- Sanitizers (define BEFORE use) ----------------
function(msim_enable_sanitizers target)
  if (MSIM_ENABLE_SANITIZERS AND NOT MSVC)
    target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(${target} PRIVATE -fsanitize=address,undefined)
  endif()
endfunction()

# ---------------- Library: msim ----------------
add_library(msim
  src/book.cpp
  src/matching_engine.cpp
  src/simulator.cpp
  src/order_flow.cpp
  src/rules.cpp

  # World + agents
  src/world.cpp
  src/agents/noise_trader.cpp
  src/agents/market_maker.cpp

  # Option B foundation: live stepping world (gateway uses this)
  src/live_world.cpp
)

target_include_directories(msim PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
msim_set_warnings(msim)
msim_enable_sanitizers(msim)

# ---------------- CLI executable ----------------
add_executable(msim_cli
  src/main.cpp
)

target_link_libraries(msim_cli PRIVATE msim)
msim_set_warnings(msim_cli)
msim_enable_sanitizers(msim_cli)

# ---------------- Gateway executable (Option B) ----------------
if (MSIM_BUILD_GATEWAY)
  include(FetchContent)

  # Header-only HTTP server: cpp-httplib
  FetchContent_Declare(
    cpp_httplib
    URL https://github.com/yhirose/cpp-httplib/archive/refs/tags/v0.16.0.zip
  )
  FetchContent_GetProperties(cpp_httplib)
  if (NOT cpp_httplib_POPULATED)
    FetchContent_Populate(cpp_httplib)
  endif()

  add_executable(msim_gateway
    src/gateway_main.cpp
  )
  target_link_libraries(msim_gateway PRIVATE msim)
  target_include_directories(msim_gateway PRIVATE ${cpp_httplib_SOURCE_DIR})
  msim_set_warnings(msim_gateway)
  msim_enable_sanitizers(msim_gateway)
endif()

# ---------------- Testing ----------------
include(CTest)
enable_testing()

include(FetchContent)

# Recommended for MSVC CRT compatibility
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
)
FetchContent_MakeAvailable(googletest)

add_executable(msim_tests
  tests/test_types.cpp
  tests/test_book.cpp
  tests/test_matching.cpp
  tests/test_cancel_modify.cpp
  tests/test_simulator.cpp
  tests/test_rules.cpp
  tests/test_order_types.cpp
  tests/test_agents_smoke.cpp
)

target_link_libraries(msim_tests PRIVATE msim GTest::gtest_main)
msim_set_warnings(msim_tests)
msim_enable_sanitizers(msim_tests)

include(GoogleTest)
gtest_discover_tests(msim_tests)
