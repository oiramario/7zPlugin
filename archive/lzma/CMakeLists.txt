# LZMA SDK - static library for 7z decompression
# Only includes source files needed for 7z archive decoding.

project(lzma LANGUAGES C)

set(LZMA_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/C")

set(SOURCES
    ${LZMA_SRC_DIR}/7zAlloc.c
    ${LZMA_SRC_DIR}/7zArcIn.c
    ${LZMA_SRC_DIR}/7zBuf.c
    ${LZMA_SRC_DIR}/7zBuf2.c
    ${LZMA_SRC_DIR}/7zCrc.c
    ${LZMA_SRC_DIR}/7zCrcOpt.c
    ${LZMA_SRC_DIR}/7zDec.c
    ${LZMA_SRC_DIR}/7zFile.c
    ${LZMA_SRC_DIR}/7zStream.c
    ${LZMA_SRC_DIR}/Alloc.c
    ${LZMA_SRC_DIR}/Bcj2.c
    ${LZMA_SRC_DIR}/Bra.c
    ${LZMA_SRC_DIR}/Bra86.c
    ${LZMA_SRC_DIR}/CpuArch.c
    ${LZMA_SRC_DIR}/Delta.c
    ${LZMA_SRC_DIR}/Lzma2Dec.c
    ${LZMA_SRC_DIR}/LzmaDec.c
    ${LZMA_SRC_DIR}/Ppmd7.c
    ${LZMA_SRC_DIR}/Ppmd7Dec.c
    ${LZMA_SRC_DIR}/Sha256.c
    ${LZMA_SRC_DIR}/Sha256Opt.c
)

add_library(lzma STATIC ${SOURCES})

target_include_directories(lzma PUBLIC ${LZMA_SRC_DIR})

target_compile_definitions(lzma PRIVATE
    Z7_LZMA_PROB32     # 32-bit probability table (native word size on x86)
    Z7_CRC_NUM_TABLES=8 # CRC unrolled across 8 tables
)

target_compile_options(lzma PRIVATE
    /W0
    $<$<CONFIG:Release>:/O2 /arch:SSE2>
)
