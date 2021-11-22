# mesytec-mcpd cmake example

cmake_minimum_required(VERSION 3.12)
project(mesytec-mcpd-cmake-example)

find_package(mesytec-mcpd REQUIRED)

add_executable(mcpd-example mcpd-example.cc)
target_link_libraries(mcpd-example PRIVATE mesytec-mcpd::mesytec-mcpd)
