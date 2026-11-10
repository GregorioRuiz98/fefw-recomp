cmake_minimum_required(VERSION 3.22)
project(fefw_android_probe LANGUAGES C CXX)
if(NOT ANDROID)
    message(FATAL_ERROR "Use the Android NDK toolchain")
endif()
add_library(nxrt_android_zbic STATIC runtime/third_party/zbic/zstd.c)
set_target_properties(nxrt_android_zbic PROPERTIES POSITION_INDEPENDENT_CODE ON C_VISIBILITY_PRESET hidden)
target_include_directories(nxrt_android_zbic PUBLIC runtime/third_party/zbic)
target_compile_definitions(nxrt_android_zbic PUBLIC ZSTD_ZBIC_SUPPORT=1)
add_library(nxrt_android STATIC runtime/src/memory_android.cpp
    runtime/src/module.cpp runtime/src/nso.cpp runtime/src/nxmodule.cpp
    runtime/src/dispatch_android.cpp runtime/src/abi_dispatch_android.cpp
    runtime/src/log_android.cpp runtime/src/config.cpp runtime/src/ini_merge.cpp runtime/src/gauges_android.cpp
    runtime/src/session_android.cpp runtime/src/hooks.cpp runtime/src/abi_platform_android.cpp
    runtime/src/catch_call.cpp runtime/src/handlers.cpp runtime/src/typeinfo.cpp runtime/src/unwind.cpp
    runtime/src/kernel_android.cpp runtime/src/svc_android.cpp runtime/src/svc_memory_android.cpp
    runtime/src/svc_info_android.cpp runtime/src/svc_process_android.cpp
    runtime/src/kernel_sync_android.cpp runtime/src/svc_threads_android.cpp
    runtime/src/hipc.cpp runtime/src/ipc.cpp runtime/src/ipc_sm.cpp runtime/src/ipc_stub.cpp runtime/src/svc_ipc_android.cpp
    runtime/src/log_service_android.cpp)
set_target_properties(nxrt_android PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(nxrt_android PUBLIC runtime/include)
target_compile_features(nxrt_android PUBLIC cxx_std_20)
target_compile_options(nxrt_android PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)
target_link_libraries(nxrt_android PUBLIC dl log nxrt_android_zbic)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/fixtures/fixture_data.h"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/runtime/tests/prepare_fixtures.py"
        --compiler "${CMAKE_CXX_COMPILER}" --out "${CMAKE_CURRENT_BINARY_DIR}/fixtures"
    DEPENDS runtime/tests/prepare_fixtures.py runtime/tests/fixture.c VERBATIM)
add_custom_target(nxrt_android_fixtures DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/fixtures/fixture_data.h")

set(memory_tests runtime/tests/test_memory.cpp runtime/tests/test_runner.cpp
    runtime/tests/test_nxmodule.cpp runtime/tests/test_loader.cpp runtime/tests/test_dispatch_android.cpp
    runtime/tests/test_nso_android.cpp runtime/tests/test_hooks.cpp
    runtime/tests/test_eh.cpp runtime/tests/test_eh_abi3.cpp runtime/tests/test_eh_typeinfo.cpp runtime/tests/test_abi_android.cpp
    runtime/tests/test_svc_android.cpp runtime/tests/test_kernel_sync.cpp runtime/tests/test_hipc.cpp runtime/tests/test_ipc.cpp
    runtime/tests/test_threads_android.cpp runtime/tests/test_log_service_android.cpp)
add_library(nxrt_android_contract_tests OBJECT ${memory_tests})
set_target_properties(nxrt_android_contract_tests PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(nxrt_android_contract_tests PRIVATE nxrt_android)
add_dependencies(nxrt_android_contract_tests nxrt_android_fixtures)
target_include_directories(nxrt_android_contract_tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/fixtures")
target_compile_options(nxrt_android_contract_tests PRIVATE "-ffile-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}=.")
add_executable(nxrt_android_memory_tests ${memory_tests})
target_link_libraries(nxrt_android_memory_tests PRIVATE nxrt_android)
add_dependencies(nxrt_android_memory_tests nxrt_android_fixtures)
target_include_directories(nxrt_android_memory_tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/fixtures")
target_compile_definitions(nxrt_android_memory_tests PRIVATE NXRT_ANDROID_TEST_CLI)
target_compile_options(nxrt_android_memory_tests PRIVATE "-ffile-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}=.")

add_library(fefw_android SHARED native/probe.cpp native/vulkan_test.cpp native/runtime_test.cpp $<TARGET_OBJECTS:nxrt_android_contract_tests>)
target_include_directories(fefw_android PRIVATE runtime/tests)
target_compile_features(fefw_android PRIVATE cxx_std_20)
target_compile_options(fefw_android PRIVATE -Wall -Wextra -Werror -Wno-missing-field-initializers -fno-strict-aliasing)
target_compile_definitions(fefw_android PRIVATE VK_USE_PLATFORM_ANDROID_KHR)
target_link_libraries(fefw_android PRIVATE nxrt_android android nativewindow log vulkan)
target_link_options(fefw_android PRIVATE -Wl,-z,max-page-size=16384 -Wl,-z,common-page-size=16384)

# Optional private build from the user's own locally generated C++. These archives
# are NOT linked into the diagnostics APK and must never enter a public package.
set(NXRT_ANDROID_LIFT_DIR "" CACHE PATH "Private directory containing locally lifted game modules")
if(NXRT_ANDROID_LIFT_DIR)
    if(NOT ANDROID_ABI STREQUAL "arm64-v8a")
        message(FATAL_ERROR "The private game module target currently requires arm64-v8a")
    endif()
    set(lifted_targets)
    foreach(module IN ITEMS main rtld sdk subsdk0 subsdk1 subsdk2)
        set(module_dir "${NXRT_ANDROID_LIFT_DIR}/${module}")
        if(NOT EXISTS "${module_dir}/module_${module}.cpp" OR NOT EXISTS "${module_dir}/funcs.h")
            message(FATAL_ERROR "Missing locally generated descriptor/header for ${module}")
        endif()
        file(GLOB module_code CONFIGURE_DEPENDS "${module_dir}/c_*.cpp")
        if(NOT module_code)
            message(FATAL_ERROR "No locally generated functions for ${module}")
        endif()
        add_library(nxrt_lift_${module} STATIC ${module_code} "${module_dir}/module_${module}.cpp")
        set_target_properties(nxrt_lift_${module} PROPERTIES POSITION_INDEPENDENT_CODE ON)
        target_include_directories(nxrt_lift_${module} PRIVATE runtime/include "${module_dir}")
        target_compile_features(nxrt_lift_${module} PRIVATE cxx_std_20)
        target_compile_options(nxrt_lift_${module} PRIVATE -O2 -g0 -fno-strict-aliasing -fwrapv)
        list(APPEND lifted_targets nxrt_lift_${module})
    endforeach()
    add_custom_target(nxrt_lifted_modules DEPENDS ${lifted_targets})
endif()
