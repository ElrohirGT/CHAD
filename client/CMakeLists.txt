cmake_minimum_required(VERSION 3.27)
project(chad_ui C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)

# PTHREADS LIBRARY
find_package(Threads REQUIRED)

# UI LIBRARY
find_package(Qt6 REQUIRED COMPONENTS Core Widgets)

# WEBSOCKET LIBRARY (Mongoose)
add_library(mongoose STATIC deps/mongoose/mongoose.c)
target_include_directories(mongoose PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/deps/mongoose)
set_target_properties(mongoose PROPERTIES LINKER_LANGUAGE C)

# CLIENT
add_executable(${PROJECT_NAME} client.cpp)

target_link_libraries(${PROJECT_NAME}
  PRIVATE
    Threads::Threads
    mongoose
    Qt::Core
    Qt::Widgets)

add_custom_command(
    TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/icons
        ${CMAKE_CURRENT_BINARY_DIR}/icons
    COMMAND ${CMAKE_COMMAND} -E echo "Build completed for ${PROJECT_NAME}"
)
