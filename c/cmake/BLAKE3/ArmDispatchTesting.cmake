# These link-wrapper fixtures exercise the actual dispatch code without adding
# instrumentation or mutable preferences to the production library.
if(NOT (BLAKE3_USE_SVE2 AND BLAKE3_USE_SME))
  return()
endif()

if(CMAKE_VERSION VERSION_LESS 3.12)
  find_package(PythonInterp 3 REQUIRED)
else()
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  set(PYTHON_EXECUTABLE "${Python3_EXECUTABLE}")
endif()
set(BLAKE3_TEST_RUNNER "" CACHE STRING "Optional command/list for ARM dispatch tests, e.g. qemu-aarch64")
option(BLAKE3_TEST_REQUIRE_ARM_DISPATCH "Fail instead of skipping unavailable ARM dispatch coverage" OFF)

get_target_property(arm_sources blake3 SOURCES)
get_target_property(arm_definitions blake3 COMPILE_DEFINITIONS)
get_target_property(arm_options blake3 COMPILE_OPTIONS)
add_library(blake3-arm-test-library STATIC ${arm_sources})
target_include_directories(blake3-arm-test-library PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(blake3-arm-test-library PUBLIC ${arm_definitions})
if(arm_options)
  target_compile_options(blake3-arm-test-library PRIVATE ${arm_options})
endif()
if(BLAKE3_USE_TBB)
  target_link_libraries(blake3-arm-test-library PUBLIC TBB::tbb)
endif()

set(arm_wrap_symbols getauxval blake3_hash_many blake3_sve2_vector_length
  blake3_sme_vector_length blake3_hash_many_portable blake3_hash_many_neon
  blake3_hash_many_sve2 blake3_hash_many_sme blake3_hash_many_sme2)
foreach(target blake3-arm-dispatch-test blake3-arm-dispatch-fault-test)
  add_executable(${target} tests/arm_dispatch_test.cpp tests/arm_dispatch_trace.c)
  target_compile_features(${target} PRIVATE c_std_11 cxx_std_17)
  target_link_libraries(${target} PRIVATE blake3-arm-test-library)
  foreach(symbol IN LISTS arm_wrap_symbols)
    set_property(TARGET ${target} APPEND_STRING PROPERTY LINK_FLAGS " -Wl,--wrap=${symbol}")
  endforeach()
endforeach()
target_sources(blake3-arm-dispatch-fault-test PRIVATE tests/arm_dispatch_fault.c)
set_property(TARGET blake3-arm-dispatch-fault-test APPEND_STRING
  PROPERTY LINK_FLAGS " -Wl,--wrap=blake3_hasher_finalize")

set(arm_test_args)
foreach(argument IN LISTS BLAKE3_TEST_RUNNER)
  list(APPEND arm_test_args "--runner=${argument}")
endforeach()
if(BLAKE3_TEST_REQUIRE_ARM_DISPATCH)
  list(APPEND arm_test_args --require-supported)
endif()
add_test(NAME blake3-arm-dispatch
  COMMAND "${PYTHON_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_arm_dispatch.py"
    --binary "$<TARGET_FILE:blake3-arm-dispatch-test>"
    --fault-binary "$<TARGET_FILE:blake3-arm-dispatch-fault-test>"
    ${arm_test_args})
set_tests_properties(blake3-arm-dispatch PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 600)

# The existing CI test cleans and rebuilds only blake3-asm-test. Preserve the
# dispatch executables for the subsequent CTest entry after that clean step.
if(TARGET blake3-asm-test)
  add_dependencies(blake3-asm-test blake3-arm-dispatch-test blake3-arm-dispatch-fault-test)
endif()
