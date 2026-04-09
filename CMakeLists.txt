cmake_minimum_required(VERSION 3.18)

add_subdirectory(tools)

set(LLVM_MOS_PLATFORM rp6502)
find_package(llvm-mos-sdk REQUIRED)

project(MusicTime C CXX ASM)

# Define the option (Default is ON/Native) 
option(USE_NATIVE_OPL2 "Use the RIA native OPL2 support" ON)

if(USE_NATIVE_OPL2)
    add_definitions(-DUSE_NATIVE_OPL2)
    message(STATUS "Targeting: Native RIA OPL2")
else()
    message(STATUS "Targeting: FPGA TinyFPGA Sound Card")
endif()

add_executable(MusicTime)

# Assets embedded in the ROM (auto-loaded into XRAM at boot):
rp6502_asset(MusicTime help src/help.txt)

rp6502_executable(MusicTime 
    DATA file 
    RESET file
)

target_sources(MusicTime PRIVATE
    src/main.c
)
