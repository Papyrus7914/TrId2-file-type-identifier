cmake_minimum_required(VERSION 3.31)
cmake_policy(SET CMP0092 NEW) # MSVC don't add `/W3`
project(TrId2_project LANGUAGES C CXX)
enable_testing()
include(FetchContent)
set(CMAKE_WARN_DEPRECATED OFF CACHE BOOL "" FORCE)
set(CMAKE_CXX_SCAN_FOR_MODULES 0)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
set(CMAKE_CXX_STANDARD 23)
set(CONSOLE_APP_VERSION "0.0" CACHE STRING "Console App version")
find_package(Python3 REQUIRED COMPONENTS Interpreter)

FetchContent_Declare(boost
                     GIT_REPOSITORY https://github.com/boostorg/boost.git
                     GIT_TAG boost-1.92.0
                     GIT_SHALLOW TRUE
                     EXCLUDE_FROM_ALL
                     SYSTEM)
FetchContent_MakeAvailable(boost)

FetchContent_Declare(doctest
                     GIT_REPOSITORY https://github.com/doctest/doctest
                     GIT_TAG v2.5.3
                     GIT_SHALLOW TRUE
                     EXCLUDE_FROM_ALL
                     SYSTEM)
FetchContent_MakeAvailable(doctest)
include("${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake")

FetchContent_Declare(cli11
                     GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
                     GIT_TAG v2.7.2
                     GIT_SHALLOW TRUE
                     EXCLUDE_FROM_ALL
                     SYSTEM)
FetchContent_MakeAvailable(cli11)

FetchContent_Declare(json
                     GIT_REPOSITORY https://github.com/nlohmann/json.git
                     GIT_TAG v3.12.0
                     GIT_SHALLOW TRUE
                     EXCLUDE_FROM_ALL
                     SYSTEM)
FetchContent_MakeAvailable(json)

FetchContent_Declare(simdjson
                     GIT_REPOSITORY https://github.com/simdjson/simdjson.git
                     GIT_TAG v5.0.4
                     GIT_SHALLOW TRUE
                     EXCLUDE_FROM_ALL
                     SYSTEM)
FetchContent_MakeAvailable(simdjson)

FetchContent_Declare(cpp-subprocess SYSTEM
                     GIT_REPOSITORY https://github.com/arun11299/cpp-subprocess.git
                     GIT_TAG v2.5
                     GIT_SHALLOW TRUE
                     EXCLUDE_FROM_ALL
                     SYSTEM)
FetchContent_MakeAvailable(cpp-subprocess)

FetchContent_Populate(cmake_template
                      GIT_REPOSITORY https://github.com/cpp-best-practices/cmake_template
                      GIT_TAG b86318abbf55a657a11a0cf2acce69f221841b22)
include(${cmake_template_SOURCE_DIR}/cmake/CompilerWarnings.cmake)
include(${cmake_template_SOURCE_DIR}/cmake/Sanitizers.cmake)

FetchContent_Populate(testdata
                      GIT_REPOSITORY https://github.com/ImageMagick/ImageMagick
                      GIT_TAG 0ca86fca35915e22e66b4b1f95995d9f5f690f9a)

file(GLOB_RECURSE TrId2Lib_source_files CONFIGURE_DEPENDS "src/lib/*.h")
file(GLOB TrId2_database_yaml_files CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/database/*.yaml")
set(TrId2_generated_database_file "${CMAKE_BINARY_DIR}/TrId2Database.json")
add_custom_command(
  OUTPUT "${TrId2_generated_database_file}"
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/generate_database_cpp_header.py" "${TrId2_generated_database_file}"
  DEPENDS "${CMAKE_SOURCE_DIR}/tools/generate_database_cpp_header.py" ${TrId2_database_yaml_files}
  COMMENT "Generating TrId2 JSON database"
  VERBATIM)
add_custom_target(TrId2_generated_database DEPENDS "${TrId2_generated_database_file}")

add_library(TrId2Lib INTERFACE ${TrId2Lib_source_files})
target_compile_options(TrId2Lib INTERFACE $<$<CXX_COMPILER_ID:MSVC>:/wd4702>)
target_include_directories(TrId2Lib INTERFACE "src")
target_link_libraries(TrId2Lib INTERFACE nlohmann_json::nlohmann_json simdjson::simdjson)
add_dependencies(TrId2Lib TrId2_generated_database)

add_library(TrId2Lib_warnings INTERFACE)
myproject_set_project_warnings(TrId2Lib_warnings ON "" "" "" "")
target_link_libraries(TrId2Lib INTERFACE TrId2Lib_warnings)

file(GLOB_RECURSE TrId2Console_source_files CONFIGURE_DEPENDS "src/TrId2Console/*.h" "src/TrId2Console/*.cpp" "src/TrId2Console/*.manifest")
add_executable(TrId2Console ${TrId2Console_source_files})
target_link_libraries(TrId2Console PUBLIC TrId2Lib CLI11::CLI11 Boost::dll)
target_compile_definitions(TrId2Console PRIVATE CONSOLE_APP_VERSION="${CONSOLE_APP_VERSION}")

file(GLOB_RECURSE TrId2_tests_source_files CONFIGURE_DEPENDS "src/Tests/*.h" "src/Tests/*.cpp" "src/Tests/*.manifest")
add_executable(TrId2_tests ${TrId2_tests_source_files})
target_link_libraries(TrId2_tests PRIVATE TrId2Lib doctest::doctest cpp-subprocess::subprocess)
target_compile_definitions(TrId2_tests PRIVATE
                           TRID2_CONSOLE_APP_PATH="$<TARGET_FILE:TrId2Console>"
                           TRID2_DATABASE_PATH="${TrId2_generated_database_file}"
                           TEST_DATA_FOLDER="${testdata_SOURCE_DIR}"
                           $<$<CXX_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>
                           $<$<CXX_COMPILER_ID:MSVC>:_SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING>)
add_dependencies(TrId2_tests TrId2Console)
doctest_discover_tests(TrId2_tests)
