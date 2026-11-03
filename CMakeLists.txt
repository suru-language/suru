cmake_minimum_required(VERSION 3.20)
project(suru C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(SU_SANITIZE "Build with AddressSanitizer + UndefinedBehaviorSanitizer" OFF)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -Wpedantic -Wconversion -Wshadow)
  if(SU_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address,undefined)
  else()
    add_compile_options(-Werror)
  endif()
endif()

file(GLOB SURU_CORE_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c)
list(REMOVE_ITEM SURU_CORE_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/main.c)

add_library(suru_core STATIC ${SURU_CORE_SRC})
target_include_directories(suru_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(suru ${CMAKE_CURRENT_SOURCE_DIR}/src/main.c)
target_link_libraries(suru PRIVATE suru_core)

enable_testing()

file(GLOB SURU_UNIT_SRC ${CMAKE_CURRENT_SOURCE_DIR}/tests/unit/test_*.c)
foreach(testsrc ${SURU_UNIT_SRC})
  get_filename_component(tname ${testsrc} NAME_WE)
  add_executable(${tname} ${testsrc})
  target_link_libraries(${tname} PRIVATE suru_core)
  target_include_directories(${tname} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests/unit)
  add_test(NAME ${tname} COMMAND ${tname})
endforeach()

add_test(NAME integration
  COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/run.sh $<TARGET_FILE:suru>)
