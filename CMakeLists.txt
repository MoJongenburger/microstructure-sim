cmake_minimum_required(VERSION 3.20)

project(microstructure_sim
  VERSION 0.1.0
  DESCRIPTION "Event-driven limit order book + matching engine simulator"
  LANGUAGES CXX
)

# ---------------- Options ----------------
option(MSIM_ENABLE_WERROR "Treat warnings as errors" OFF)
option(MSIM_ENABLE_SANITIZERS "Enable ASan/UBSan (Clang/GCC)" OFF)

# Step 18 (Option B): HTTP gateway + simple web UI
option(MSIM_BUILD_GATEWAY "Build HTTP gateway + web UI" ON)

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

  # Step 16 (World + agents)
  src/world.cpp
  src/agents/noise_trader.cpp

  # Step 17 (Market maker agent)
  src/agents/market_maker.cpp
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

# ---------------- Dependencies (FetchContent) ----------------
include(FetchContent)

# ---------------- Step 18: Gateway deps + executable ----------------
if (MSIM_BUILD_GATEWAY)
  # cpp-httplib (header-only HTTP server)
  FetchContent_Declare(
    httplib
    URL https://github.com/yhirose/cpp-httplib/archive/refs/tags/v0.16.3.zip
  )

  # nlohmann/json
  FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/archive/refs/tags/v3.11.3.zip
  )

  FetchContent_MakeAvailable(httplib nlohmann_json)

  add_executable(msim_gateway
    src/gateway_main.cpp
  )

  target_link_libraries(msim_gateway PRIVATE
    msim
    httplib::httplib
    nlohmann_json::nlohmann_json
  )

  msim_set_warnings(msim_gateway)
  msim_enable_sanitizers(msim_gateway)
endif()

# ---------------- Testing ----------------
include(CTest)
enable_testing()

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

  # Agents / world smoke test
  tests/test_agents_smoke.cpp
)

target_link_libraries(msim_tests PRIVATE msim GTest::gtest_main)
msim_set_warnings(msim_tests)
msim_enable_sanitizers(msim_tests)

include(GoogleTest)
gtest_discover_tests(msim_tests)
