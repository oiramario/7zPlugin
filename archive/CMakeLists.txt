project(7zArchive)

add_subdirectory(lzma)

add_library(${PROJECT_NAME} STATIC
    7zArchive.cpp
)

target_include_directories(${PROJECT_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    $<TARGET_PROPERTY:lzma,INTERFACE_INCLUDE_DIRECTORIES>
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    lzma
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    $<$<BOOL:${DEBUG_7Z_TRACE}>:DEBUG_7Z_TRACE=1>
)
