cmake_minimum_required(VERSION 3.15)
project(AX_7Z LANGUAGES C CXX)

option(DEBUG_7Z_TRACE "Enable 7z plugin trace logging via OutputDebugString" OFF)

add_subdirectory(archive)
add_subdirectory(plugin)
