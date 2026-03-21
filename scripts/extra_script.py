"""
@file extra_script.py
@brief Script to automatically set platform-specific build flags when library is included.
@details This script is executed by PlatformIO before the build process and sets
         appropriate C++ standard flags based on the target platform.
"""

Import("env")
import os
import subprocess
from os.path import join, realpath

current_platform = env.get("PIOPLATFORM", "")
current_env = env.get("PIOENV", "")

if current_platform in ["native", "test_native"]:
    env.Replace(CC="clang", CXX="clang++", LINK="clang++")

if current_platform == "native" and current_env not in ("test_native_profile", "test_native_xray"):
    project_dir = env.subst("$PROJECT_DIR")
    lsan_supp = os.path.join(project_dir, "lsan.supp")
    env.Append(ENV={
        "LSAN_OPTIONS": f"suppressions={lsan_supp}",
        "ASAN_OPTIONS": "detect_leaks=1:halt_on_error=0:exitcode=1",
    })

# For the profile env: create the coverage output directory, wire LLVM_PROFILE_FILE
# into all available env layers (build-script env changes do not reach the test binary
# subprocess — set LLVM_PROFILE_FILE in the shell before pio test to control the path),
# add -fprofile-instr-generate to LINKFLAGS (build_flags only reaches CCFLAGS, not
# LINKFLAGS), and explicitly link libclang_rt.profile-x86_64.a (clang++ only auto-links
# it when the flag is present on the link step).
_profile_rt_lib = None
if current_env == "test_native_profile":
    project_dir = env.subst("$PROJECT_DIR")
    coverage_dir = os.path.join(project_dir, ".pio", "coverage")
    os.makedirs(coverage_dir, exist_ok=True)
    profile_file = os.path.join(coverage_dir, "%e-%p.profraw")
    env.Append(ENV={"LLVM_PROFILE_FILE": profile_file})
    os.environ["LLVM_PROFILE_FILE"] = profile_file
    DefaultEnvironment().Replace(ENV={
        **DefaultEnvironment().get("ENV", {}),
        "LLVM_PROFILE_FILE": profile_file,
    })

    env.Append(LINKFLAGS=["-fprofile-instr-generate"])

    try:
        rt = subprocess.check_output(
            ["clang++", "--print-file-name=libclang_rt.profile-x86_64.a"],
            text=True
        ).strip()
        if os.path.exists(rt):
            _profile_rt_lib = rt
            env.Append(LINKFLAGS=[rt])
            print(f"LoRaMesher: Linking LLVM profiling runtime: {rt}")
        else:
            print("LoRaMesher: Warning: LLVM profiling runtime not found; .profraw will not be written")
    except Exception as e:
        print(f"LoRaMesher: Warning: could not locate LLVM profiling runtime: {e}")

global_env = DefaultEnvironment()

def set_platform_cpp_standard(environment, platform):
    """
    @brief Apply platform-specific C++ standard flags
    @param environment The environment to modify (can be library or global)
    @param platform The target platform string
    """
    environment.Replace(CXXFLAGS=[f for f in environment.get("CXXFLAGS", []) if not f.startswith("-std=")])

    if platform in ["native", "test_native"]:
        environment.Append(CXXFLAGS=["-std=gnu++20"])
    elif platform == "espressif32":
        environment.Append(CXXFLAGS=["-std=gnu++2a"])

set_platform_cpp_standard(env, current_platform)
set_platform_cpp_standard(global_env, current_platform)

for lb in env.GetLibBuilders():
    set_platform_cpp_standard(lb.env, current_platform)
    if current_platform in ["native", "test_native"]:
        lb.env.Replace(CC="clang", CXX="clang++", LINK="clang++")

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

Import("projenv")
set_platform_cpp_standard(projenv, current_platform)
if current_platform in ["native", "test_native"]:
    for e in [projenv, global_env]:
        e.Replace(CC="clang", CXX="clang++", LINK="clang++")

if current_env == "test_native_profile":
    for e in [projenv, global_env]:
        e.Append(LINKFLAGS=["-fprofile-instr-generate"])
        if _profile_rt_lib:
            e.Append(LINKFLAGS=[_profile_rt_lib])
        e.Append(ENV={"LLVM_PROFILE_FILE": profile_file})

if current_env == "test_native_xray":
    for e in [env, projenv, global_env]:
        e.Append(LINKFLAGS=["-fxray-instrument"])
