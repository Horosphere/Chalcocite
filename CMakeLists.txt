project(Chalcocite)
cmake_minimum_required(VERSION 3.5)

set(PROJECT_SOURCE_DIR ${CMAKE_SOURCE_DIR}/src)
set(CMAKE_BINARY_DIR ${CMAKE_SOURCE_DIR}/bin)
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR})
set(LIBRARY_OUTPUT_PATH ${CMAKE_BINARY_DIR})

set (CMAKE_C_STANDARD 11)

# Do not remove the following auto-generated signatures

# Auto-generated. Do not edit. All changes will be undone
set(SOURCE_FILES
    ${PROJECT_SOURCE_DIR}/media.c
    ${PROJECT_SOURCE_DIR}/test.c
    ${PROJECT_SOURCE_DIR}/playback.c
    ${PROJECT_SOURCE_DIR}/main.c
    ${PROJECT_SOURCE_DIR}/interactive.c
    ${PROJECT_SOURCE_DIR}/video.c
    ${PROJECT_SOURCE_DIR}/audio.c
    ${PROJECT_SOURCE_DIR}/container/packetqueue.c
    ${PROJECT_SOURCE_DIR}/container/vectorptr.c
   )
# Auto-generated end


add_executable(Chalcocite ${SOURCE_FILES})
target_link_libraries(Chalcocite m)
target_link_libraries(Chalcocite avcodec avformat avutil swscale swresample)
target_link_libraries(Chalcocite SDL2)
target_link_libraries(Chalcocite readline)
