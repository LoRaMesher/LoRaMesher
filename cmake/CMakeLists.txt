# find_package(GTest REQUIRED)
# include(GoogleTest)

# add_executable(config_tests
#     test/types/configurations/pin_config_test.cpp
#     test/types/configurations/radio_config_test.cpp
#     test/types/configurations/protocol_config_test.cpp
#     test/types/configurations/config_test.cpp
# )

# target_link_libraries(config_tests
#     PRIVATE
#     GTest::GTest
#     GTest::Main
#     loramesher_lib  # Your library target
# )

# gtest_discover_tests(config_tests)