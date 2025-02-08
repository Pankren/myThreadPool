cmake_minimum_required(VERSION 3.10)

# Set the project name
project(ThreadPool)

# Specify the C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

aux_source_directory(. SRC_LIST)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

# Add the executable
add_executable(threadTest ${SRC_LIST})

# Include directories
# include_directories(${PROJECT_SOURCE_DIR}/include)

# Link libraries (if any)
# target_link_libraries(thread_pool <library_name>)