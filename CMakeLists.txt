cmake_minimum_required(VERSION 3.18.1)
project(Kin VERSION 3.2 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)

# Android STL selection (static to avoid dependency on system libc++)
if(ANDROID)
    set(ANDROID_STL c++_static)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

# ---- 构建开关:免授权调试(仅内部用) ----
# 授权已改为 App 驱动:守护无条件运行, 只读 App 下发的 auth.json 知道是否付费, 不再自行验签。
# -DNO_LICENSE=ON  → 定义 CTS_NO_LICENSE,lic::readAuth() 恒返回已付费(免授权调试)。
# 默认(OFF)       → 读 auth.json 的付费状态(未付费也照常运行, 付费墙在 App 端功能级)。
option(NO_LICENSE "Build with license signal ignored (always paid): for internal debugging" OFF)
if(NO_LICENSE)
    add_compile_definitions(CTS_NO_LICENSE)
    message(STATUS "Kin edition: 公益版 (NO_LICENSE — 授权校验已关闭)")
else()
    message(STATUS "Kin edition: 捐赠版 (licensed — Ed25519 校验启用)")
endif()


# Warnings
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wshadow")

# Release build optimizations (learned from CoreTaskOptimizer)
if(CMAKE_BUILD_TYPE STREQUAL "Release" OR NOT CMAKE_BUILD_TYPE)
    # 注：本进程不做浮点运算，去掉 -ffast-math（它会改变 NaN/Inf 语义、可能引入 UB，对纯整数+sysfs IO 的守护进程毫无收益）
    # [优化 §6.8] IO 密集型常驻 daemon 瓶颈在 sysfs 读写而非算力。-O3+循环展开徒增二进制体积与常驻 RSS。
    #             改 -Os(优化体积):更小的代码段、更低的常驻内存、更快的载入；速度基本无差。保留 -flto。
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -Os -flto")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -finline-functions -fomit-frame-pointer")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -fno-rtti -fno-threadsafe-statics -fvisibility=hidden")
    # Remove unused sections to reduce binary size
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -ffunction-sections -fdata-sections")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} -Wl,--gc-sections -Wl,--strip-all -flto")
endif()

# Debug build flags
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0 -DDEBUG_DURATION=1")
endif()

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)

# Source files
# tweetnacl.c 是公有领域 Ed25519 实现(授权验证用),作为独立 C 编译单元,保持原样不混入 C++。
set(SOURCES
    src/main.cpp
    src/tweetnacl.c
)

# Threads (portable)
find_package(Threads REQUIRED)

# Executable
add_executable(Kin ${SOURCES})

# Link libraries
target_link_libraries(Kin PRIVATE Threads::Threads)

# Android-specific link libraries
if(ANDROID)
    target_link_libraries(Kin PRIVATE log)
endif()

# Post-build: strip binary for Android Release builds (learned from CoreTaskOptimizer)
if(ANDROID AND CMAKE_BUILD_TYPE STREQUAL "Release")
    add_custom_command(TARGET Kin POST_BUILD
        COMMAND ${CMAKE_STRIP} $<TARGET_FILE:Kin>
        COMMENT "Stripping binary for Android release"
    )
endif()

# Output directory
set_target_properties(Kin PROPERTIES
    OUTPUT_NAME "Kinetic"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Installation
install(TARGETS Kin DESTINATION bin)
