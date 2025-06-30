# Define source files using GLOB_RECURSE - this gets all source files
file(GLOB_RECURSE LORAMESHER_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

# Note: Instead of explicitly filtering sources by platform,
# we rely on the preprocessor directives in system_config.hpp
# to conditionally compile the appropriate code for each platform

# Conditionally set up the target based on platform
if(ESP_PLATFORM)
    # ESP-IDF component registration
    idf_component_register(
        SRCS ${LORAMESHER_SOURCES}
        INCLUDE_DIRS 
            "${CMAKE_CURRENT_SOURCE_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/../include"
        REQUIRES 
            driver
            esp_timer
    )
    
    # Add Arduino define for ESP-IDF when using Arduino as framework
    target_compile_definitions(${COMPONENT_LIB} PUBLIC ARDUINO=100)
    
elseif(BUILD_DESKTOP)
    # Desktop build - this will activate the LORAMESHER_BUILD_NATIVE sections
    
    # Add GoogleTest/GoogleMock for desktop build
    include(FetchContent)
    FetchContent_Declare(
      googletest
      GIT_REPOSITORY https://github.com/google/googletest.git
      GIT_TAG release-1.12.1  # Use a specific release tag for stability
    )
    # For Windows: Prevent overriding the parent project's compiler/linker settings
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
    
    # Create library target
    add_library(loramesher_lib ${LORAMESHER_SOURCES})
    
    # Set include directories
    target_include_directories(loramesher_lib PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../include"
        "${radiolib_SOURCE_DIR}/src"  # RadioLib headers
        "${gtest_SOURCE_DIR}/include"  # GTest headers
        "${gmock_SOURCE_DIR}/include"  # GMock headers
    )
    
    # Link against GMock for the desktop build
    target_link_libraries(loramesher_lib PUBLIC
        gmock
    )
    
    # Set output name and version properties
    set_target_properties(loramesher_lib PROPERTIES
        OUTPUT_NAME "LoRaMesher"
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
    )
    
    # Install targets
    install(TARGETS loramesher_lib
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        RUNTIME DESTINATION bin
    )
    
    # Install headers
    install(
        DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/"
        DESTINATION include/loramesher
        FILES_MATCHING PATTERN "*.h*"
    )
    
    install(
        DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../include/"
        DESTINATION include/loramesher
        FILES_MATCHING PATTERN "*.h*"
    )
else()
    # PlatformIO embedded build (non ESP-IDF)
    
    # Create library target
    add_library(loramesher_lib ${LORAMESHER_SOURCES})
    
    # Set include directories
    target_include_directories(loramesher_lib PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../include"
        "${radiolib_SOURCE_DIR}/src"  # RadioLib headers
    )
    
    # Add Arduino define for PlatformIO builds
    target_compile_definitions(loramesher_lib PUBLIC ARDUINO=100)
    
    # Set output name and version properties
    set_target_properties(loramesher_lib PROPERTIES
        OUTPUT_NAME "LoRaMesher"
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
    )
endif()