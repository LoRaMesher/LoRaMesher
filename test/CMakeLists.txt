# Only build tests in desktop mode
if(NOT BUILD_DESKTOP AND NOT CMAKE_TESTING_ENABLED)
    message(STATUS "Tests skipped - only available in desktop build or when CMAKE_TESTING_ENABLED is set")
    return()
endif()

# Find Google Test
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.16.0
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)
include(GoogleTest)

# Add Google Test/Mock include directories
include_directories(
    ${gtest_SOURCE_DIR}/include
    ${gmock_SOURCE_DIR}/include
)

# Define a list to track all test targets
set(ALL_TEST_TARGETS "")

# Set default test timeout (in seconds)
set(DEFAULT_TEST_TIMEOUT 5)
message(STATUS "Default test timeout: ${DEFAULT_TEST_TIMEOUT} seconds")

# Function to find and add test executables from directories
# Usage: add_test_suite(NAME suite_name DIRS dir1 dir2 ... [TIMEOUT seconds])
function(add_test_suite)
    cmake_parse_arguments(ARGS "" "NAME;TIMEOUT" "DIRS" ${ARGN})
    
    if(NOT ARGS_NAME)
        message(FATAL_ERROR "Test suite name not provided")
        return()
    endif()
    
    if(NOT ARGS_DIRS)
        message(FATAL_ERROR "Test directories not provided for ${ARGS_NAME}")
        return()
    endif()
    
    # Set test timeout - use provided value or default
    if(NOT ARGS_TIMEOUT)
        set(ARGS_TIMEOUT ${DEFAULT_TEST_TIMEOUT})
    endif()
    
    set(SOURCES "")
    
    # Collect sources from all specified directories
    foreach(DIR ${ARGS_DIRS})
        file(GLOB_RECURSE DIR_SOURCES
            "${CMAKE_CURRENT_SOURCE_DIR}/${DIR}/*.cpp"
        )
        list(APPEND SOURCES ${DIR_SOURCES})
    endforeach()
    
    # Only create the test target if there are sources
    if(SOURCES)
        message(STATUS "Creating test suite '${ARGS_NAME}' with sources from: ${ARGS_DIRS} (timeout: ${ARGS_TIMEOUT}s)")
        
        # Create test executable
        add_executable(${ARGS_NAME} ${SOURCES})
        
        # Link libraries
        target_link_libraries(${ARGS_NAME}
            PRIVATE
            loramesher_lib
            gtest
            gtest_main
            gmock
            gmock_main
        )
        
        # Discover tests with timeout property
        gtest_discover_tests(${ARGS_NAME} 
            PROPERTIES 
            TIMEOUT ${ARGS_TIMEOUT}
        )
        
        # Add to the global list of test targets
        list(APPEND ALL_TEST_TARGETS ${ARGS_NAME})
        set(ALL_TEST_TARGETS ${ALL_TEST_TARGETS} PARENT_SCOPE)
    else()
        message(WARNING "No sources found for test suite '${ARGS_NAME}' in directories: ${ARGS_DIRS}")
    endif()
endfunction()

# Auto-discover all test directories
file(GLOB TEST_DIRS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} 
    "${CMAKE_CURRENT_SOURCE_DIR}/*"
)

# Process each test directory
foreach(DIR ${TEST_DIRS})
    if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${DIR}")
        # Create a sanitized name for the directory
        string(REPLACE "/" "_" SUITE_NAME "${DIR}_tests")
        
        # Subdirectories to check for test files
        file(GLOB SUBDIRS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} 
            "${CMAKE_CURRENT_SOURCE_DIR}/${DIR}/*"
        )
        
        set(VALID_SUBDIRS "")
        set(HAS_TEST_FILES FALSE)
        
        # Check for direct test files in the directory
        file(GLOB DIRECT_TEST_FILES
            "${CMAKE_CURRENT_SOURCE_DIR}/${DIR}/*.cpp"
        )
        
        if(DIRECT_TEST_FILES)
            set(HAS_TEST_FILES TRUE)
            add_test_suite(NAME ${SUITE_NAME} DIRS ${DIR})
        else()
            # Create a list of valid subdirectories containing test files
            foreach(SUBDIR ${SUBDIRS})
                if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${SUBDIR}")
                    file(GLOB SUBDIR_TEST_FILES
                        "${CMAKE_CURRENT_SOURCE_DIR}/${SUBDIR}/*.cpp"
                    )
                    
                    if(SUBDIR_TEST_FILES)
                        string(REPLACE "/" "_" SUBSUITE_NAME "${SUBDIR}_tests")
                        add_test_suite(NAME ${SUBSUITE_NAME} DIRS ${SUBDIR})
                    endif()
                endif()
            endforeach()
        endif()
    endif()
endforeach()

# Create a custom target that depends on all test targets
add_custom_target(
    build_all_tests
    COMMENT "Building all test suites"
    DEPENDS ${ALL_TEST_TARGETS}
)

# Create a custom target to run all tests
add_custom_target(
    run_all_tests
    COMMENT "Running all test suites"
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    DEPENDS build_all_tests
)

# Allow override of timeout when running tests
add_custom_target(
    run_all_tests_extended_timeout
    COMMENT "Running all test suites with extended timeout (300s)"
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure --timeout 300
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    DEPENDS build_all_tests
)

# Add a custom target that allows specifying a timeout at build time
add_custom_target(
    run_tests_with_timeout
    COMMENT "Running all test suites with custom timeout"
    COMMAND ${CMAKE_COMMAND} -E env CTEST_OUTPUT_ON_FAILURE=1 ${CMAKE_CTEST_COMMAND} --timeout ${TEST_TIMEOUT}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    DEPENDS build_all_tests
)