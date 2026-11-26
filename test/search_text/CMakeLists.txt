add_executable(SearchTextTest SearchTextTest.cpp)
target_include_directories(SearchTextTest PRIVATE ${REPO_ROOT}/src/activities/reader)
target_link_libraries(SearchTextTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(SearchTextTest)
