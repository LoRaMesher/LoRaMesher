"""
@file extra_script.py
@brief PlatformIO extra script that applies the build settings LoRaMesher requires.
@details PlatformIO runs this script before building any project that depends on the
         library. It enforces the required C++ standard on every build environment
         (library, project, global and dependency environments), selects clang for
         native builds and configures the instrumentation used by the native test
         variants.
"""

import os
import subprocess

Import("env", "projenv")

## C++ standard flag required by the library for each supported platform.
CPP_STANDARD_FLAGS = {
    "native": "-std=gnu++20",
    "espressif32": "-std=gnu++2a",
}

## Native test environment that collects LLVM source-based coverage.
PROFILE_ENV = "test_native_profile"

## Native test environment that collects LLVM XRay traces.
XRAY_ENV = "test_native_xray"

current_platform = env.get("PIOPLATFORM", "")
current_env = env.get("PIOENV", "")
global_env = DefaultEnvironment()
project_dir = env.subst("$PROJECT_DIR")


def set_cpp_standard(environment, std_flag):
    """
    @brief Replace every C++ standard flag in CXXFLAGS with the given one
    @details Frameworks may append their flags as nested lists, so CXXFLAGS is
             flattened before filtering. Non-string entries are preserved.
    @param environment SCons environment to modify
    @param std_flag Standard flag to apply, e.g. "-std=gnu++20"
    """
    flags = environment.Flatten(environment.get("CXXFLAGS", []))
    flags = [f for f in flags if not (isinstance(f, str) and f.startswith("-std="))]
    environment.Replace(CXXFLAGS=flags + [std_flag])


def use_clang(environment):
    """
    @brief Select clang as the C, C++ and link driver of an environment
    @param environment SCons environment to modify
    """
    environment.Replace(CC="clang", CXX="clang++", LINK="clang++")


def find_profile_runtime():
    """
    @brief Locate the LLVM profiling runtime archive used by clang
    @details clang++ only links the runtime automatically when the instrumentation
             flag is present on the link step, so it is linked explicitly.
    @return Absolute path of the runtime archive, or None when it is unavailable
    """
    try:
        path = subprocess.check_output(
            ["clang++", "--print-file-name=libclang_rt.profile-x86_64.a"], text=True
        ).strip()
    except (OSError, subprocess.CalledProcessError) as error:
        print(f"LoRaMesher: Warning: could not locate LLVM profiling runtime: {error}")
        return None
    if not os.path.exists(path):
        print("LoRaMesher: Warning: LLVM profiling runtime not found; .profraw will not be written")
        return None
    print(f"LoRaMesher: Linking LLVM profiling runtime: {path}")
    return path


def configure_profiling(environments):
    """
    @brief Enable LLVM source-based coverage collection on the given environments
    @details build_flags only reach the compile step, so the instrumentation flag and
             the profiling runtime are added to LINKFLAGS explicitly. LLVM_PROFILE_FILE
             is also exported to the process environment because build-script
             environment changes do not reach the test binary; set it in the shell
             before running pio test to control the output path.
    @param environments SCons environments to modify
    """
    coverage_dir = os.path.join(project_dir, ".pio", "coverage")
    os.makedirs(coverage_dir, exist_ok=True)
    profile_file = os.path.join(coverage_dir, "%e-%p.profraw")
    os.environ["LLVM_PROFILE_FILE"] = profile_file

    link_flags = ["-fprofile-instr-generate"]
    runtime = find_profile_runtime()
    if runtime:
        link_flags.append(runtime)

    for environment in environments:
        environment.Append(LINKFLAGS=list(link_flags), ENV={"LLVM_PROFILE_FILE": profile_file})


def configure_leak_sanitizer(environment):
    """
    @brief Point the sanitizer runtime at the project leak suppression file
    @param environment SCons environment to modify
    """
    environment.Append(
        ENV={
            "LSAN_OPTIONS": f"suppressions={os.path.join(project_dir, 'lsan.supp')}",
            "ASAN_OPTIONS": "detect_leaks=1:halt_on_error=0:exitcode=1",
        }
    )


build_envs = [env, global_env, projenv] + [lb.env for lb in env.GetLibBuilders()]

std_flag = CPP_STANDARD_FLAGS.get(current_platform)
if std_flag:
    for environment in build_envs:
        set_cpp_standard(environment, std_flag)

if current_platform == "native":
    for environment in build_envs:
        use_clang(environment)

    if current_env == PROFILE_ENV:
        configure_profiling([env, projenv, global_env])
    elif current_env == XRAY_ENV:
        for environment in [env, projenv, global_env]:
            environment.Append(LINKFLAGS=["-fxray-instrument"])
    else:
        configure_leak_sanitizer(env)
