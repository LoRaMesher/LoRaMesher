"""
@file extra_script.py
@brief Script to automatically set platform-specific build flags when library is included.
@details This script is executed by PlatformIO before the build process and sets
         appropriate C++ standard flags based on the target platform.
"""

Import("env")
from os.path import join, realpath

env.Replace(CC="clang", CXX="clang++")

# Get global environment
global_env = DefaultEnvironment()

def set_platform_cpp_standard(environment, platform):
    """
    @brief Apply platform-specific C++ standard flags
    @param environment The environment to modify (can be library or global)
    @param platform The target platform string
    """

    # First, remove any existing C++11 flags
    environment.Replace(CXXFLAGS=[flag for flag in environment.get("CXXFLAGS", []) if "-std=gnu++11" not in flag])
    
    # Set platform-specific flags
    if platform == "native":
        print("LoRaMesher: Setting native platform flags (C++20)")
        environment.Append(CXXFLAGS=["-std=c++20", "-std=gnu++20"])
    
    elif platform == "espressif32":
        print("LoRaMesher: Setting ESP32 platform flags (C++2a)")
        environment.Append(CXXFLAGS=["-std=c++2a", "-std=gnu++2a"])
    
    # You can add more platform-specific settings here as needed
    else:
        print(f"LoRaMesher: Unknown platform '{platform}', no custom flags applied")

# Get current platform
current_platform = env.get("PIOPLATFORM", "")

# Apply C++ standard settings to the library environment
set_platform_cpp_standard(env, current_platform)

# Apply the same settings to the global environment (affects project and other libraries)
set_platform_cpp_standard(global_env, current_platform)

print("LoRaMesher: Configuring global environment for platform:", current_platform)

#
# Pass flags to the other Project Dependencies (libraries)
#
for lb in env.GetLibBuilders():
    # Set the C++ standard flags for dependent libraries too
    set_platform_cpp_standard(lb.env, current_platform)

    # If newer versions of googletest are used, the following lines can be uncommented to include specific paths
    # # Include specific paths for GoogleTest library
    # if lb.name in ["googletest", "GoogleTest"]:
    #     print(f"LoRaMesher: Configuring GoogleTest library")
    #     lb.env.Append(CPPPATH=[
    #         join(realpath(lb.path), "googletest", "include"),
    #         join(realpath(lb.path), "googlemock", "include"),
    #         join(realpath(lb.path), "googletest", "src"),
    #         join(realpath(lb.path), "googlemock", "src"),
    #     ])
    #     # Exclude google test files from the build process
    #     lb.env.Replace(SRC_FILTER=["-<googletest/test/*>",])

# Operate with the project environment (files located in the `src` folder)

Import("projenv")
# Set the C++ standard for the project environment too
set_platform_cpp_standard(projenv, current_platform)
