project(AX_7Z VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /source-charset:utf-8")

set(BUILD_SHARED_LIBS ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_library(${PROJECT_NAME} SHARED
    dllmain.cpp
    AX_7z.cpp
    7zPlugin.cpp
    res/resources.rc
    AX_7z.def
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    7zArchive
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    $<$<BOOL:${DEBUG_7Z_TRACE}>:DEBUG_7Z_TRACE=1>
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
    OUTPUT_NAME "AX_7z"
    SUFFIX ".apl"
)

install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)
