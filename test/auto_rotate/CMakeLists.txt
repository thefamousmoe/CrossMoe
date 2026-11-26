add_executable(AutoRotateTest AutoRotateTest.cpp)
target_include_directories(AutoRotateTest PRIVATE
  ${REPO_ROOT}/lib/hal
  ${REPO_ROOT}/src/activities/reader
)
target_link_libraries(AutoRotateTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(AutoRotateTest)
