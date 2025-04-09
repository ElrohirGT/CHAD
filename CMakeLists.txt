cmake_minimum_required(VERSION 3.27)
project(chad_client CXX)
set(CMAKE_CXX_STANDARD 17)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets)

add_subdirectory(deps/facil.io)

add_executable(${PROJECT_NAME} client/client.cpp)
target_sources(${PROJECT_NAME} PRIVATE ${PROJECT_SOURCES})
target_include_directories(${PROJECT_NAME} PRIVATE ${PROJECT_INCLUDE})
target_link_libraries(${PROJECT_NAME} PRIVATE facil.io ${CMAKE_THREAD_LIBS_INIT} Qt::Core Qt::Widgets)

add_custom_command(
    TARGET ${PROJECT_NAME}
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "Build completed for ${PROJECT_NAME}"
)
