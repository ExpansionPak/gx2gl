#!/bin/bash

# Ensure devkitPro environment variables are set
if [ -z "$DEVKITPRO" ]; then
    if [ -d "/opt/devkitpro" ]; then
        export DEVKITPRO=/opt/devkitpro
    else
        echo "Error: DEVKITPRO environment variable is not set and /opt/devkitpro does not exist."
        exit 1
    fi
fi

if [ -z "$DEVKITPPC" ]; then
    export DEVKITPPC=$DEVKITPRO/devkitPPC
fi

TOOLS_BIN="$DEVKITPRO/tools/bin"
ELF2RPL="$TOOLS_BIN/elf2rpl"

# Add devkitPPC to PATH if not already present
case ":$PATH:" in
  *":$DEVKITPPC/bin:"*) ;;
  *) export PATH=$DEVKITPPC/bin:$PATH ;;
esac

case ":$PATH:" in
  *":$TOOLS_BIN:"*) ;;
  *) export PATH=$TOOLS_BIN:$PATH ;;
esac

# Build directory setup
BUILD_DIR="build_wsl"
mkdir -p $BUILD_DIR
cd $BUILD_DIR

# Run CMake
echo "Configuring with CMake..."
cmake .. -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/WiiU.cmake -G "Unix Makefiles"

# Compile
if [ $? -eq 0 ]; then
    echo "Compiling..."
    make -j$(nproc)
    if [ $? -eq 0 ]; then
        echo "Converting to RPX..."
        if [ ! -x "$ELF2RPL" ]; then
            echo "Build completed, but elf2rpl was not found at: $ELF2RPL"
            exit 1
        fi
        "$ELF2RPL" gl33_test.elf gl33_test.rpx
        if [ $? -eq 0 ]; then
            echo "------------------------------------------------"
            echo "Build Successful!"
            echo "Output located in: $BUILD_DIR/gl33_test.rpx"
            echo "------------------------------------------------"
        else
            echo "RPX conversion FAILED via: $ELF2RPL"
            exit 1
        fi
    else
        echo "Build FAILED during compilation."
        exit 1
    fi
else
    echo "CMake configuration FAILED."
    exit 1
fi
