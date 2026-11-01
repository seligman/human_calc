# Human Calc C port: CMake build for Visual Studio, Xcode, Ninja and Makefiles (DESIGN.md 12.4).
# The Makefile is the Linux development build; this file serves every other toolchain and CI.
#
#   cmake -S c -B build-msvc -A x64 -DHC_TESTS=ON && cmake --build build-msvc --config Release
#   ctest --test-dir build-msvc -C Release
#   cmake -S c -B build-ios -G Xcode -DCMAKE_SYSTEM_NAME=iOS -DHC_TESTS=OFF   (static library)
#
# Options:
#   HC_SHARED         a DLL / dylib / .so (HC_BUILD_DLL, hidden symbols) instead of a static
#                     library; tests always link the object files, never the shared library
#   HC_TESTS          unit tests (tests/{rt,engine}/test_*.c, tests/test_*.c), the runner
#                     (hc_test), the CLI (hc), CTest registration, the hc_vectors target
#   HC_CHECK_HEADERS  every header compiled alone (human_calc.h also as C++), the stub archive
#                     and, except on iOS, the stub link check
#   HC_RUNNER_ARGS    extra runner arguments (default --tolerance=10 off Linux, DESIGN.md 13.2)
#   HC_PY_RESULTS     a tests/py_results.py output file: adds the exact runner test against it
#
# Python 3 is needed for HC_TESTS and HC_CHECK_HEADERS (tools/gen_stubs.py runs at configure
# time; editing a header re-runs it).
cmake_minimum_required(VERSION 3.16)
project(human_calc LANGUAGES C)

option(HC_SHARED "Build a shared library (DLL / dylib / so)" OFF)
option(HC_TESTS "Build the unit tests, the runner and the CLI" OFF)
option(HC_CHECK_HEADERS "Compile every header alone and build the stub archive" OFF)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
if(MSVC AND CMAKE_VERSION VERSION_LESS 3.21)
    add_compile_options(/std:c11)
endif()

set(HC_DIR ${CMAKE_CURRENT_SOURCE_DIR})
file(GLOB HC_SOURCES CONFIGURE_DEPENDS ${HC_DIR}/src/*.c ${HC_DIR}/src/rt/*.c ${HC_DIR}/src/gen/*.c)
set(HC_THIRD_PARTY ${HC_DIR}/third_party/hc_tommath.c ${HC_DIR}/third_party/hc_pcre2.c
    ${HC_DIR}/third_party/hc_ryu.c)
set(HC_LIBM_SOURCES ${HC_DIR}/src/rt/py_math.c ${HC_DIR}/src/rt/py_float.c
    ${HC_DIR}/src/rt/py_float_text.c)
set(HC_INCLUDE_DIRS ${HC_DIR}/include ${HC_DIR}/src ${HC_DIR}/third_party
    ${HC_DIR}/third_party/libtommath ${HC_DIR}/third_party/pcre2/src ${HC_DIR}/third_party/ryu)
file(GLOB HC_HEADERS CONFIGURE_DEPENDS ${HC_DIR}/include/*.h ${HC_DIR}/src/*.h
    ${HC_DIR}/src/rt/*.h ${HC_DIR}/src/gen/*.h)
list(APPEND HC_HEADERS ${HC_DIR}/third_party/hc_tommath.h ${HC_DIR}/third_party/hc_pcre2.h)

# Compiler settings that matter for results (DESIGN.md 12.1) and the warning policy (12.2).
if(MSVC)
    set(HC_FP_FLAGS /fp:precise)
    set(HC_WARN /W4 /WX)
    set(HC_NO_WARN /W0)
    set(HC_NO_BUILTIN /Oi-)
else()
    set(HC_FP_FLAGS -ffp-contract=off)
    set(HC_WARN -Wall -Wextra -pedantic -Werror -Wconversion -Wshadow -Wstrict-prototypes
        -Wmissing-prototypes -Wvla -Wpointer-arith -Wcast-qual)
    set(HC_NO_WARN -w)
    set(HC_NO_BUILTIN -fno-builtin)
endif()
if(CMAKE_INTERPROCEDURAL_OPTIMIZATION)
    message(FATAL_ERROR "human_calc must not be built with link-time optimization (DESIGN.md 5.1)")
endif()

set(HC_SYSLIBS)
if(UNIX AND NOT APPLE)
    list(APPEND HC_SYSLIBS m)
endif()

# Every source of DESIGN.md 12.1, compiled once; the library and the tests use these objects.
add_library(hc_objects OBJECT ${HC_SOURCES} ${HC_THIRD_PARTY})
target_include_directories(hc_objects PUBLIC ${HC_INCLUDE_DIRS})
target_compile_options(hc_objects PRIVATE ${HC_FP_FLAGS})
set_source_files_properties(${HC_SOURCES} PROPERTIES COMPILE_OPTIONS "${HC_WARN}")
set_source_files_properties(${HC_THIRD_PARTY} PROPERTIES COMPILE_OPTIONS "${HC_NO_WARN}")
foreach(src ${HC_LIBM_SOURCES})
    if(EXISTS ${src})
        set_source_files_properties(${src} PROPERTIES COMPILE_OPTIONS "${HC_WARN};${HC_NO_BUILTIN}")
    endif()
endforeach()

if(HC_SHARED)
    set_target_properties(hc_objects PROPERTIES POSITION_INDEPENDENT_CODE ON
        C_VISIBILITY_PRESET hidden)
    target_compile_definitions(hc_objects PUBLIC HC_BUILD_DLL)
    add_library(human_calc SHARED $<TARGET_OBJECTS:hc_objects>)
    target_link_libraries(human_calc PRIVATE ${HC_SYSLIBS})
else()
    add_library(human_calc STATIC $<TARGET_OBJECTS:hc_objects>)
endif()
target_include_directories(human_calc PUBLIC ${HC_DIR}/include)

# ---- stubs (DESIGN.md 14.2) and the header check --------------------------------------------

if(HC_TESTS OR HC_CHECK_HEADERS)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(HC_STUB_DIR ${CMAKE_BINARY_DIR}/stubs)
    execute_process(COMMAND ${Python3_EXECUTABLE} ${HC_DIR}/tools/gen_stubs.py --out ${HC_STUB_DIR}
        RESULT_VARIABLE hc_rc)
    if(NOT hc_rc EQUAL 0)
        message(FATAL_ERROR "tools/gen_stubs.py failed")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${HC_HEADERS}
        ${HC_DIR}/tools/gen_stubs.py)
    file(GLOB HC_STUB_SOURCES ${HC_STUB_DIR}/*.c)
    add_library(hc_stubs STATIC ${HC_STUB_SOURCES})
    target_include_directories(hc_stubs PRIVATE ${HC_INCLUDE_DIRS})
    target_compile_options(hc_stubs PRIVATE ${HC_FP_FLAGS} ${HC_WARN})
    if(HC_SHARED)
        target_compile_definitions(hc_stubs PRIVATE HC_BUILD_DLL)
    endif()
endif()

if(HC_CHECK_HEADERS)
    set(hc_checks)
    foreach(h ${HC_HEADERS})
        file(RELATIVE_PATH rel ${HC_DIR} ${h})
        string(REGEX REPLACE "^(include|src|third_party)/" "" inc ${rel})
        string(MAKE_C_IDENTIFIER ${rel} id)
        set(out ${CMAKE_BINARY_DIR}/check_headers/${id}.c)
        file(GENERATE OUTPUT ${out} CONTENT
            "#include \"${inc}\"\n#include \"${inc}\"\ntypedef int hc_check_headers_tu;\n")
        list(APPEND hc_checks ${out})
    endforeach()
    set_source_files_properties(${hc_checks} PROPERTIES GENERATED TRUE)
    add_library(hc_check_headers OBJECT ${hc_checks})
    target_include_directories(hc_check_headers PRIVATE ${HC_INCLUDE_DIRS})
    target_compile_options(hc_check_headers PRIVATE ${HC_FP_FLAGS} ${HC_WARN})

    include(CheckLanguage)
    check_language(CXX)
    if(CMAKE_CXX_COMPILER)
        enable_language(CXX)
        set(out ${CMAKE_BINARY_DIR}/check_headers/human_calc_cpp.cpp)
        file(GENERATE OUTPUT ${out} CONTENT
            "#include \"human_calc.h\"\n#include \"human_calc.h\"\nint hc_check_cpp_tu;\n")
        set_source_files_properties(${out} PROPERTIES GENERATED TRUE)
        add_library(hc_check_header_cpp OBJECT ${out})
        target_include_directories(hc_check_header_cpp PRIVATE ${HC_DIR}/include)
        set_target_properties(hc_check_header_cpp PROPERTIES CXX_STANDARD 11
            CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
        if(MSVC)
            target_compile_options(hc_check_header_cpp PRIVATE /W4 /WX)
        else()
            target_compile_options(hc_check_header_cpp PRIVATE -Wall -Wextra -pedantic -Werror)
        endif()
    endif()

    if(NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
        add_executable(stub_link_check ${HC_STUB_DIR}/check/link_check.c)
        target_include_directories(stub_link_check PRIVATE ${HC_INCLUDE_DIRS})
        target_compile_options(stub_link_check PRIVATE ${HC_FP_FLAGS} ${HC_WARN})
        target_compile_definitions(stub_link_check PRIVATE _CRT_SECURE_NO_WARNINGS)
        target_link_libraries(stub_link_check PRIVATE hc_stubs ${HC_SYSLIBS})
    endif()
endif()

# ---- tests, runner, CLI ----------------------------------------------------------------------

if(HC_TESTS)
    enable_testing()
    find_package(Threads)
    set(HC_TEST_LIBS hc_objects hc_stubs ${HC_SYSLIBS})
    if(Threads_FOUND)
        list(APPEND HC_TEST_LIBS Threads::Threads)
    endif()
    file(GLOB HC_TEST_SUPPORT CONFIGURE_DEPENDS ${HC_DIR}/tests/hc_test.c ${HC_DIR}/tests/tolerance.c)
    file(GLOB HC_UNIT_TESTS CONFIGURE_DEPENDS ${HC_DIR}/tests/rt/test_*.c
        ${HC_DIR}/tests/engine/test_*.c ${HC_DIR}/tests/test_*.c)
    list(REMOVE_ITEM HC_UNIT_TESTS ${HC_DIR}/tests/test_runner.c)

    function(hc_program name)
        add_executable(${name} ${ARGN})
        target_include_directories(${name} PRIVATE ${HC_DIR}/tests)
        target_compile_options(${name} PRIVATE ${HC_FP_FLAGS} ${HC_WARN})
        target_compile_definitions(${name} PRIVATE _CRT_SECURE_NO_WARNINGS)
        target_link_libraries(${name} PRIVATE ${HC_TEST_LIBS})
    endfunction()

    foreach(src ${HC_UNIT_TESTS})
        get_filename_component(name ${src} NAME_WE)
        if(TARGET ${name})
            message(FATAL_ERROR "two unit tests are named ${name}")
        endif()
        hc_program(${name} ${src} ${HC_TEST_SUPPORT})
        add_test(NAME ${name} COMMAND ${name} --vectors ${CMAKE_BINARY_DIR}/vectors)
        set_tests_properties(${name} PROPERTIES SKIP_RETURN_CODE 77)
    endforeach()

    if(EXISTS ${HC_DIR}/tests/test_runner.c)
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            set(hc_default_runner_args "")
        else()
            set(hc_default_runner_args "--tolerance=10")
        endif()
        set(HC_RUNNER_ARGS "${hc_default_runner_args}" CACHE STRING "extra hc_test arguments")
        hc_program(hc_test ${HC_DIR}/tests/test_runner.c ${HC_TEST_SUPPORT})
        get_filename_component(hc_root ${HC_DIR} DIRECTORY)
        separate_arguments(hc_runner_args NATIVE_COMMAND "${HC_RUNNER_ARGS}")
        add_test(NAME runner COMMAND hc_test --root ${hc_root} ${hc_runner_args})
        if(HC_PY_RESULTS)
            add_test(NAME runner_exact COMMAND hc_test --root ${hc_root} --expected ${HC_PY_RESULTS})
        endif()
    endif()
    if(EXISTS ${HC_DIR}/tools/hc_cli.c)
        hc_program(hc ${HC_DIR}/tools/hc_cli.c)
    endif()
    if(EXISTS ${HC_DIR}/tests/fuzz_driver.c)
        hc_program(fuzz_driver ${HC_DIR}/tests/fuzz_driver.c)
    endif()

    # Vector files for the unit tests (DESIGN.md 13.3): cmake --build . --target hc_vectors
    file(GLOB HC_VECTOR_SCRIPTS CONFIGURE_DEPENDS ${HC_DIR}/tests/vectors/*.py)
    list(REMOVE_ITEM HC_VECTOR_SCRIPTS ${HC_DIR}/tests/vectors/vecutil.py)
    set(hc_vector_commands)
    foreach(script ${HC_VECTOR_SCRIPTS})
        list(APPEND hc_vector_commands COMMAND ${Python3_EXECUTABLE} ${script} --tier quick
            --out ${CMAKE_BINARY_DIR}/vectors)
    endforeach()
    add_custom_target(hc_vectors ${hc_vector_commands}
        COMMENT "Writing vector files to ${CMAKE_BINARY_DIR}/vectors" VERBATIM)
endif()
