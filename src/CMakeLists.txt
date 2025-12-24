# Use C++20 (or newer if needed)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS "-Wall -Wextra -Wno-return-type-c-linkage -Wno-deprecated-declarations")
set(CMAKE_CXX_FLAGS_DEBUG "-g")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

# Optional SDR backend support
option(USE_HACKRF "Enable HackRF support" ON)
option(USE_RTLSDR "Enable RTL-SDR support" ON)

# Base source files
set(OPENSTINT_SOURCES
    main.cpp
    frame.cpp
    transponder.cpp
    passing.cpp
    counters.cpp
    sdr_device.cpp
)

# Add SDR backend sources based on options
if(USE_HACKRF)
    list(APPEND OPENSTINT_SOURCES sdr_hackrf.cpp)
endif()

if(USE_RTLSDR)
    list(APPEND OPENSTINT_SOURCES sdr_rtlsdr.cpp)
endif()

add_executable(openstint ${OPENSTINT_SOURCES})

# Required dependencies
find_path(LIQUID_INCLUDE_DIR NAMES liquid/liquid.h)
find_library(LIQUID_LIB REQUIRED NAMES liquid liquid-dsp)

find_path(FEC_INCLUDE_DIR NAMES fec.h)
find_library(FEC_LIB REQUIRED NAMES fec)

find_package(cppzmq REQUIRED)

# Optional SDR dependencies
if(USE_HACKRF)
    find_path(HACKRF_INCLUDE_DIR NAMES libhackrf/hackrf.h)
    find_library(HACKRF_LIB NAMES hackrf)

    if(HACKRF_LIB)
        target_compile_definitions(openstint PRIVATE HAVE_HACKRF)
        target_include_directories(openstint PRIVATE ${HACKRF_INCLUDE_DIR})
        target_link_libraries(openstint ${HACKRF_LIB})
        message(STATUS "HackRF support enabled")
    else()
        message(WARNING "HackRF support requested but library not found")
    endif()
endif()

if(USE_RTLSDR)
    find_path(RTLSDR_INCLUDE_DIR NAMES rtl-sdr.h)
    find_library(RTLSDR_LIB NAMES rtlsdr)

    if(RTLSDR_LIB)
        target_compile_definitions(openstint PRIVATE HAVE_RTLSDR)
        target_include_directories(openstint PRIVATE ${RTLSDR_INCLUDE_DIR})
        target_link_libraries(openstint ${RTLSDR_LIB})
        message(STATUS "RTL-SDR support enabled")
    else()
        message(WARNING "RTL-SDR support requested but library not found")
    endif()
endif()

# Common includes and link libraries
target_compile_options(openstint PRIVATE -I ".")
target_include_directories(openstint PRIVATE ${LIQUID_INCLUDE_DIR} ${cppzmq_INCLUDE_DIR} ${FEC_INCLUDE_DIR})
target_link_libraries(openstint
    ${LIQUID_LIB}
    ${FEC_LIB}
    cppzmq
    m
    c
)
