# ==================== Compiler Detection ====================

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(DIAGON_COMPILER_GNU_LIKE TRUE)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    set(DIAGON_COMPILER_MSVC TRUE)
endif()

message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")

# ==================== Warning Flags ====================

if(DIAGON_COMPILER_GNU_LIKE)
    add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
        # -Werror                     # Treat warnings as errors (enable in CI)
        -Wno-unused-parameter       # Allow unused parameters
        -Wno-sign-compare           # Common in index code
    )
elseif(DIAGON_COMPILER_MSVC)
    add_compile_options(
        /W4                         # Warning level 4
        # /WX                       # Treat warnings as errors (enable in CI)
        /wd4100                     # Unused parameter (common)
        /wd4244                     # Conversion warnings
    )
endif()

# ==================== Optimization Flags ====================

if(CMAKE_BUILD_TYPE MATCHES "Release|RelWithDebInfo")
    message(STATUS "Applying Release optimizations")
    if(DIAGON_COMPILER_GNU_LIKE)
        add_compile_options(
            -O3                     # Maximum optimization
            -DNDEBUG                # Disable assertions
            -ffast-math             # Fast floating-point math
        )

        # CPU-specific optimization
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
            add_compile_options(-march=native)  # Dev builds
            # add_compile_options(-march=x86-64-v3)  # Production builds (AVX2)
        endif()

    elseif(DIAGON_COMPILER_MSVC)
        add_compile_options(
            /O2                     # Maximum optimization
            /DNDEBUG                # Disable assertions
            /fp:fast                # Fast floating-point
            /arch:AVX2              # Enable AVX2
        )
    endif()
endif()

if(CMAKE_BUILD_TYPE MATCHES "Debug")
    message(STATUS "Applying Debug settings")
    if(DIAGON_COMPILER_GNU_LIKE)
        add_compile_options(
            -O0                     # No optimization
            -g                      # Debug symbols
            -fno-omit-frame-pointer
        )

        # Sanitizers (optional, can be expensive)
        # add_compile_options(-fsanitize=address -fsanitize=undefined)
        # add_link_options(-fsanitize=address -fsanitize=undefined)

    elseif(DIAGON_COMPILER_MSVC)
        add_compile_options(
            /Od                     # No optimization
            /Zi                     # Debug symbols
        )
    endif()
endif()

# ==================== SIMD Flags ====================

if(DIAGON_ENABLE_SIMD)
    message(STATUS "SIMD optimizations enabled")
    if(DIAGON_COMPILER_GNU_LIKE)
        # SIMD flags for specific files
        set(DIAGON_SIMD_FLAGS
            -mavx2
            -mfma
            -mbmi2
        )
    elseif(DIAGON_COMPILER_MSVC)
        set(DIAGON_SIMD_FLAGS
            /arch:AVX2
        )
    endif()

    # Note: SIMD flags applied to specific source files in module CMakeLists
endif()

# ==================== Link-Time Optimization (LTO) ====================

if(CMAKE_BUILD_TYPE MATCHES "Release")
    include(CheckIPOSupported)
    check_ipo_supported(RESULT IPO_SUPPORTED OUTPUT IPO_ERROR)

    if(IPO_SUPPORTED)
        message(STATUS "Link-Time Optimization (LTO) enabled")
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
    else()
        message(STATUS "Link-Time Optimization (LTO) not supported: ${IPO_ERROR}")
    endif()
endif()

# ==================== Position Independent Code ====================

# Enable PIC for shared libraries
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

message(STATUS "Compiler flags configured")
