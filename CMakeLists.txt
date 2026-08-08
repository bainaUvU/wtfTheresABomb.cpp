cmake_minimum_required(VERSION 4.3)
project(wtfTheresABomb)

set(CMAKE_CXX_STANDARD 20)

add_executable(serverMain serverMain.cpp)
target_link_libraries(serverMain PRIVATE ws2_32)
add_executable(clientMain clientMain.cpp)
target_link_libraries(clientMain PRIVATE ws2_32)