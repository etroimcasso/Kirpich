# Test runner. Auto-discovers any *.cpp in tests/
# so that adding a test file requires no CMake change.

file(GLOB TEST_SOURCES CONFIGURE_DEPENDS "*.cpp")

if(TEST_SOURCES)
    add_executable(kirpich-tests ${TEST_SOURCES})
    target_link_libraries(kirpich-tests PRIVATE
        kirpich-lib
        GTest::gtest_main
    )
    target_compile_features(kirpich-tests PRIVATE cxx_std_20)
    include(GoogleTest)
    gtest_discover_tests(kirpich-tests)
endif()
