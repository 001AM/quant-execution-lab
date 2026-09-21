cmake_minimum_required(VERSION 3.20)

project(quant_engine VERSION 0.1.0 LANGUAGES CXX)

include(CTest)
option(ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

add_library(quant_engine STATIC
    src/analytics/execution_analytics.cpp
    src/analytics/implementation_shortfall.cpp
    src/derivatives/black_scholes.cpp
    src/derivatives/greeks.cpp
    src/derivatives/implied_volatility.cpp
    src/execution/execution_scheduler.cpp
    src/execution/execution_summary.cpp
    src/execution/parent_order.cpp
    src/execution/pov.cpp
    src/execution/twap.cpp
    src/execution/volume_profile.cpp
    src/execution/vwap.cpp
    src/matching_engine.cpp
    src/order.cpp
    src/order_book.cpp
    src/price_level.cpp
    src/version.cpp
)
add_library(quant_engine::quant_engine ALIAS quant_engine)

target_compile_features(quant_engine PUBLIC cxx_std_20)
target_include_directories(quant_engine
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

function(quant_engine_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
    endif()
endfunction()

function(quant_engine_enable_sanitizers target)
    if(ENABLE_SANITIZERS)
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
            target_compile_options(${target} PRIVATE
                -fsanitize=address,undefined
                -fno-omit-frame-pointer
            )
            target_link_options(${target} PRIVATE -fsanitize=address,undefined)
        else()
            message(FATAL_ERROR "ENABLE_SANITIZERS requires a Clang or GNU compiler")
        endif()
    endif()
endfunction()

quant_engine_enable_warnings(quant_engine)
quant_engine_enable_sanitizers(quant_engine)

add_executable(execution_example examples/execution_example.cpp)
target_link_libraries(execution_example PRIVATE quant_engine::quant_engine)
quant_engine_enable_warnings(execution_example)
quant_engine_enable_sanitizers(execution_example)

add_executable(options_example examples/options_example.cpp)
target_link_libraries(options_example PRIVATE quant_engine::quant_engine)
quant_engine_enable_warnings(options_example)
quant_engine_enable_sanitizers(options_example)

add_library(quant_engine_benchmark_support STATIC
    benchmarks/benchmark_cancel.cpp
    benchmarks/benchmark_matching.cpp
    benchmarks/benchmark_mixed.cpp
    benchmarks/benchmark_modify.cpp
    benchmarks/benchmark_report.cpp
    benchmarks/benchmark_runner.cpp
    benchmarks/benchmark_scaling.cpp
    benchmarks/benchmark_submit.cpp
    benchmarks/latency_recorder.cpp
    benchmarks/workload_generator.cpp
)
target_link_libraries(quant_engine_benchmark_support PUBLIC quant_engine::quant_engine)
target_include_directories(quant_engine_benchmark_support PUBLIC benchmarks)
target_compile_definitions(quant_engine_benchmark_support PUBLIC
    QUANT_ENGINE_BUILD_TYPE="${CMAKE_BUILD_TYPE}")
quant_engine_enable_warnings(quant_engine_benchmark_support)
quant_engine_enable_sanitizers(quant_engine_benchmark_support)

add_executable(quant_engine_benchmarks benchmarks/benchmark_main.cpp)
target_link_libraries(quant_engine_benchmarks PRIVATE quant_engine_benchmark_support)
set_target_properties(quant_engine_benchmarks PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/benchmarks")
quant_engine_enable_warnings(quant_engine_benchmarks)
quant_engine_enable_sanitizers(quant_engine_benchmarks)

if(BUILD_TESTING)
    function(add_quant_engine_test name source)
        add_executable(${name} ${source})
        target_link_libraries(${name} PRIVATE quant_engine::quant_engine)
        quant_engine_enable_warnings(${name})
        quant_engine_enable_sanitizers(${name})
        add_test(NAME ${name} COMMAND ${name})
    endfunction()

    add_quant_engine_test(quant_engine_version_tests tests/version_test.cpp)
    add_quant_engine_test(quant_engine_order_tests tests/test_order.cpp)
    add_quant_engine_test(quant_engine_price_level_tests tests/test_price_level.cpp)
    add_quant_engine_test(quant_engine_order_book_tests tests/test_order_book.cpp)
    add_quant_engine_test(quant_engine_market_order_tests tests/test_market_orders.cpp)
    add_quant_engine_test(quant_engine_modify_order_tests tests/test_modify_order.cpp)
    add_quant_engine_test(quant_engine_lifecycle_tests tests/test_order_lifecycle.cpp)
    add_quant_engine_test(quant_engine_matching_engine_tests tests/test_matching_engine.cpp)
    add_quant_engine_test(quant_engine_parent_order_tests
        tests/execution/test_parent_order.cpp)
    add_quant_engine_test(quant_engine_twap_tests tests/execution/test_twap.cpp)
    add_quant_engine_test(quant_engine_vwap_tests tests/execution/test_vwap.cpp)
    add_quant_engine_test(quant_engine_pov_tests tests/execution/test_pov.cpp)
    add_quant_engine_test(quant_engine_execution_scheduler_tests
        tests/execution/test_execution_scheduler.cpp)
    add_quant_engine_test(quant_engine_execution_analytics_tests
        tests/analytics/test_execution_analytics.cpp)
    add_quant_engine_test(quant_engine_implementation_shortfall_tests
        tests/analytics/test_implementation_shortfall.cpp)
    add_quant_engine_test(quant_engine_black_scholes_tests
        tests/derivatives/test_black_scholes.cpp)
    add_quant_engine_test(quant_engine_greeks_tests
        tests/derivatives/test_greeks.cpp)
    add_quant_engine_test(quant_engine_implied_volatility_tests
        tests/derivatives/test_implied_volatility.cpp)
    add_executable(quant_engine_benchmark_infrastructure_tests
        tests/benchmarks/test_benchmark_infrastructure.cpp)
    target_link_libraries(quant_engine_benchmark_infrastructure_tests PRIVATE
        quant_engine_benchmark_support)
    quant_engine_enable_warnings(quant_engine_benchmark_infrastructure_tests)
    quant_engine_enable_sanitizers(quant_engine_benchmark_infrastructure_tests)
    add_test(NAME quant_engine_benchmark_infrastructure_tests
        COMMAND quant_engine_benchmark_infrastructure_tests)
endif()
