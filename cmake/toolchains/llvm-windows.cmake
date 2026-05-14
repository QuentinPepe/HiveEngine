# LLVM/Clang Toolchain for Windows
# Usage: cmake -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/llvm-windows.cmake ..

set(CMAKE_SYSTEM_NAME Windows)

function(hive_find_windows_sdk_tool tool_name out_var)
    file(GLOB _hive_windows_sdk_tool_paths
        "C:/Program Files (x86)/Windows Kits/10/bin/*/x64/${tool_name}"
        "C:/Program Files (x86)/Windows Kits/*/bin/x64/${tool_name}"
    )
    if(_hive_windows_sdk_tool_paths)
        list(SORT _hive_windows_sdk_tool_paths COMPARE NATURAL ORDER DESCENDING)
        list(GET _hive_windows_sdk_tool_paths 0 _hive_windows_sdk_tool)
        set(${out_var} "${_hive_windows_sdk_tool}" PARENT_SCOPE)
    endif()
endfunction()

# Find LLVM installation
# Priority: LLVM_PATH env var > clang shipped with any Visual Studio edition > standalone LLVM
if(DEFINED ENV{LLVM_PATH})
    set(LLVM_ROOT "$ENV{LLVM_PATH}")
else()
    set(_hive_vs_root_candidates "")
    if(DEFINED ENV{ProgramFiles})
        list(APPEND _hive_vs_root_candidates "$ENV{ProgramFiles}/Microsoft Visual Studio")
    endif()
    if(DEFINED ENV{ProgramW6432})
        list(APPEND _hive_vs_root_candidates "$ENV{ProgramW6432}/Microsoft Visual Studio")
    endif()
    list(APPEND _hive_vs_root_candidates
        "C:/Program Files/Microsoft Visual Studio"
        "C:/Program Files (x86)/Microsoft Visual Studio")
    list(REMOVE_DUPLICATES _hive_vs_root_candidates)

    set(_hive_vs_llvm_globs "")
    foreach(_hive_vs_root IN LISTS _hive_vs_root_candidates)
        # Wildcard the VS version (2019, 2022, 18, future) and edition
        # (Community, Professional, Enterprise, BuildTools, Preview).
        list(APPEND _hive_vs_llvm_globs "${_hive_vs_root}/*/*/VC/Tools/Llvm/x64")
    endforeach()

    set(_hive_vs_llvm_paths "")
    if(_hive_vs_llvm_globs)
        file(GLOB _hive_vs_llvm_paths ${_hive_vs_llvm_globs})
    endif()

    if(_hive_vs_llvm_paths)
        list(SORT _hive_vs_llvm_paths COMPARE NATURAL ORDER DESCENDING)
        list(GET _hive_vs_llvm_paths 0 LLVM_ROOT)
    elseif(EXISTS "C:/Program Files/LLVM")
        set(LLVM_ROOT "C:/Program Files/LLVM")
    else()
        message(FATAL_ERROR
            "LLVM not found. Tried LLVM_PATH env var, every Visual Studio edition under "
            "'Microsoft Visual Studio/*/*/VC/Tools/Llvm/x64', and 'C:/Program Files/LLVM'. "
            "Either install the C++ Clang tooling component in Visual Studio (any edition), "
            "install standalone LLVM, or set LLVM_PATH to your install.")
    endif()

    unset(_hive_vs_root_candidates)
    unset(_hive_vs_llvm_globs)
    unset(_hive_vs_llvm_paths)
endif()

# Set compilers
set(CMAKE_C_COMPILER "${LLVM_ROOT}/bin/clang-cl.exe" CACHE FILEPATH "C compiler")
set(CMAKE_CXX_COMPILER "${LLVM_ROOT}/bin/clang-cl.exe" CACHE FILEPATH "C++ compiler")
set(CMAKE_AR "${LLVM_ROOT}/bin/llvm-lib.exe" CACHE FILEPATH "Archiver")
set(CMAKE_RANLIB "" CACHE FILEPATH "Ranlib (not needed with llvm-lib)")
set(CMAKE_LINKER "${LLVM_ROOT}/bin/lld-link.exe" CACHE FILEPATH "Linker")

if(NOT DEFINED CMAKE_RC_COMPILER)
    hive_find_windows_sdk_tool("rc.exe" HIVE_WINDOWS_RC_COMPILER)
    if(HIVE_WINDOWS_RC_COMPILER)
        set(CMAKE_RC_COMPILER "${HIVE_WINDOWS_RC_COMPILER}" CACHE FILEPATH "RC compiler")
    else()
        set(CMAKE_RC_COMPILER "rc" CACHE FILEPATH "RC compiler")
    endif()
endif()

# Use LLD linker
set(CMAKE_EXE_LINKER_FLAGS_INIT "-fuse-ld=lld")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-fuse-ld=lld")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "-fuse-ld=lld")

# Target triple for Windows x64
set(CMAKE_C_COMPILER_TARGET "x86_64-pc-windows-msvc")
set(CMAKE_CXX_COMPILER_TARGET "x86_64-pc-windows-msvc")

message(STATUS "LLVM Toolchain: ${LLVM_ROOT}")
