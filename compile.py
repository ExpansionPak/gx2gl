import os
import sys
import subprocess
import multiprocessing
# this script is just for convenience, you can invoke make manually if need be
def get_env_var(name, default=None):
    val = os.environ.get(name)
    if not val:
        return default
    return val

def main():
    print("Starting Universal Build...")


    devkitpro = get_env_var("DEVKITPRO")
    

    if os.name == "nt" and devkitpro == "/opt/devkitpro":
        devkitpro = "C:/devkitPro"

    if not devkitpro:
        default_linux = "/opt/devkitpro"
        default_win = "C:/devkitPro"
        if os.path.isdir(default_linux):
            devkitpro = default_linux
        elif os.path.isdir(default_win):
            devkitpro = default_win
        else:
            print("Error: DEVKITPRO environment variable is not set and default locations do not exist.")
            sys.exit(1)
    
    os.environ["DEVKITPRO"] = devkitpro

    devkitppc = get_env_var("DEVKITPPC")
    if not devkitppc:
        devkitppc = os.path.join(devkitpro, "devkitPPC")
        os.environ["DEVKITPPC"] = devkitppc

    tools_bin = os.path.join(devkitpro, "tools", "bin").replace("\\", "/")
    

    exe_ext = ".exe" if os.name == "nt" else ""
    elf2rpl_cmd = os.path.join(tools_bin, f"elf2rpl{exe_ext}")


    paths_to_add = [os.path.join(devkitppc, "bin"), tools_bin]
    current_path = os.environ.get("PATH", "")
    for p in paths_to_add:
        if p not in current_path:
            current_path = p + os.pathsep + current_path
    os.environ["PATH"] = current_path

    build_dir = "build"
    if not os.path.exists(build_dir):
        os.makedirs(build_dir)
    

    print("Configuring with CMake...")
    toolchain_file = os.path.join(devkitpro, "cmake", "WiiU.cmake").replace("\\", "/")
    cmake_cmd = [
        "cmake", "..", 
        f"-DCMAKE_TOOLCHAIN_FILE={toolchain_file}"
    ]

    cmake_cmd.extend(["-G", "Unix Makefiles"])
    
    result = subprocess.run(cmake_cmd, cwd=build_dir)
    if result.returncode != 0:
        print("CMake configuration FAILED.")
        sys.exit(1)

    # Compile
    print("Compiling...")
    cores = str(multiprocessing.cpu_count())
    make_cmd = ["make", f"-j{cores}"]
    result = subprocess.run(make_cmd, cwd=build_dir)
    if result.returncode != 0:
        print("Build FAILED during compilation.")
        sys.exit(1)

    # Convert to RPX
    print("Converting to RPX...")
    elf2rpl_found = False
    for ext in ["", ".exe"]:
        candidate = os.path.join(tools_bin, f"elf2rpl{ext}").replace("\\", "/")
        if os.path.isfile(candidate):
            elf2rpl_cmd = candidate
            elf2rpl_found = True
            break

    if not elf2rpl_found:
        print(f"Build completed, but elf2rpl was not found in: {tools_bin}")
        # Not failing here completely as elf2rpl might just be missing on some hosts, but warn the user.
        sys.exit(1)

    elf_file = "gl33_test.elf"
    rpx_file = "gl33_test.rpx"
    elf2rpl_run = subprocess.run([elf2rpl_cmd, elf_file, rpx_file], cwd=build_dir)
    
    if elf2rpl_run.returncode == 0:
        print("-" * 48)
        print("Build Successful!")
        print(f"Output located in: {build_dir}/{rpx_file}")
        print("-" * 48)
    else:
        print(f"RPX conversion FAILED via: {elf2rpl_cmd}")
        sys.exit(1)

if __name__ == "__main__":
    main()
