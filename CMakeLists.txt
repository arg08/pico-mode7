cmake_minimum_required(VERSION 3.12)

# Pull in SDK (must be before project)
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

if (PICO_SDK_VERSION_STRING VERSION_LESS "1.3.0")
    message(FATAL_ERROR "Raspberry Pi Pico SDK version 1.3.0 (or later) required. Your version is ${PICO_SDK_VERSION_STRING}")
endif()


project(mode7 C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialize the SDK
pico_sdk_init()

include(FetchContent)
set(GIT_IGNORE_UNTRACKED TRUE)
FetchContent_Declare(cmake_git_version_tracking
  GIT_REPOSITORY https://github.com/andrew-hardin/cmake-git-version-tracking.git
  GIT_TAG 904dbda1336ba4b9a1415a68d5f203f576b696bb
)
FetchContent_MakeAvailable(cmake_git_version_tracking)



add_executable(mode7
        main.c
		mode7.c
		makesyncs.c
		fonts.c
		test_pages.c
        )

pico_generate_pio_header(mode7 ${CMAKE_CURRENT_LIST_DIR}/mode7.pio)


# Using USB console only as no spare pins for UART
pico_enable_stdio_usb(mode7 1)
pico_enable_stdio_uart(mode7 0)


# pull in common dependencies
target_link_libraries(mode7
	pico_stdlib
	pico_multicore
	hardware_dma
	hardware_pio
	cmake_git_version_tracking
	)

# create map/bin/hex file etc.
pico_add_extra_outputs(mode7)

