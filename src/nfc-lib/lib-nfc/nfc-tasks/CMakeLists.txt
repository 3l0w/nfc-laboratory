set(CMAKE_CXX_STANDARD 17)

set(PRIVATE_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/main/cpp)
set(PUBLIC_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/main/include)

add_library(nfc-tasks STATIC
        src/main/cpp/AdaptiveSamplingTask.cpp
        src/main/cpp/FourierProcessTask.cpp
        src/main/cpp/FrameDecoderTask.cpp
        src/main/cpp/FrameStorageTask.cpp
        src/main/cpp/SignalReceiverTask.cpp
        src/main/cpp/SignalRecorderTask.cpp
        )

if (CMAKE_SYSTEM_PROCESSOR MATCHES "(x86)|(X86)|(amd64)|(AMD64)")
    target_compile_options(nfc-tasks PRIVATE "-msse2" -DUSE_SSE2)
endif ()

#target_compile_options(nfc-tasks PRIVATE "-fopt-info-vec-optimized")
#target_compile_options(nfc-tasks PRIVATE "-msse2" -DUSE_SSE2)

target_include_directories(nfc-tasks PUBLIC ${PUBLIC_INCLUDE_DIR})
target_include_directories(nfc-tasks PRIVATE ${PRIVATE_SOURCE_DIR})

target_link_libraries(nfc-tasks nfc-decode rt-lang sdr-io nlohmann)