cmake_minimum_required(VERSION 3.16)
project(HttpWebServer VERSION 1.0.0 LANGUAGES CXX)

# 设置C++标准为C++20以支持协程
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 编译选项
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")

# 添加include目录
include_directories(${CMAKE_SOURCE_DIR})
include_directories(${CMAKE_SOURCE_DIR}/include)
find_package(fmt REQUIRED)
# 添加所有源文件
file(GLOB_RECURSE SOURCES 
    "src/core/*.cpp"
    "src/http/*.cpp"
    "src/network/*.cpp"
    "src/utils/*.cpp"
    "src/*.cpp"
)

# 添加所有头文件
file(GLOB_RECURSE HEADERS 
    "src/core/*.hpp"
    "src/http/*.hpp"
    "src/network/*.hpp"
    "src/utils/*.hpp"
)

# 创建主可执行文件
add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

# 链接系统库
target_link_libraries(${PROJECT_NAME} 
                    PRIVATE
                    fmt::fmt
                    )

# 设置输出目录
set_target_properties(${PROJECT_NAME}
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)


