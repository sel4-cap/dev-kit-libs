cmake_minimum_required(VERSION 3.7.2)

set(CPU cortex-a53)
set(TOOLCHAIN aarch64-linux-gnu)
set(CMAKE_C_COMPILER ${TOOLCHAIN}-gcc)
set(CMAKE_C_COMPILER_WORKS 1)
project(${MICROKIT_APP} LANGUAGES C)

set(LD ${TOOLCHAIN}-ld)
set(CMAKE_ASM_COMPILER ${TOOLCHAIN}-as)
set(MICROKIT_DIR /packages/microkit/auts_microkit_stable_plus_big_stack)

# Microkit specific information
set(MICROKIT_SDK ${MICROKIT_DIR}/release/microkit-sdk-1.2.6)
set(MICROKIT_BOARD ${PLATFORM})
set(MICROKIT_CONFIG debug)
# Set python variables
set(PYTHONPATH /usr/bin/python)
set(MICROKIT_TOOL "python3 -m microkit")
# Set dtb path
set(DTB_PATH ${CMAKE_CURRENT_SOURCE_DIR}/boards/${PLATFORM}.dtb)

set(BOARD_DIR ${MICROKIT_SDK}/board/${MICROKIT_BOARD}/${MICROKIT_CONFIG})
set(PICOLIBC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../picolibc_build/picolibc/aarch64-linux-gnu)

set(CFLAGS -mcpu=${CPU} "-DDTB_PATH=\"${DTB_PATH}\"" -nostdlib -ffreestanding -Wall -Wno-unused-function)
set(LDFLAGS -L${BOARD_DIR}/lib -nostdlib)
set(LIBS -lmicrokit -Tmicrokit.ld -lgcc -L${PICOLIBC_DIR}/lib -lc -lm -lgcc)

set(PROJECT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/example/${PLATFORM}/${MICROKIT_APP})
set(BUILD_DIR ${PROJECT_DIR}/build)
set(EXAMPLE_BUILD_DIR ${PROJECT_DIR}/example-build)
set(IMAGE_FILE ${EXAMPLE_BUILD_DIR}/sel4_image.img)
set(REPORT_FILE ${EXAMPLE_BUILD_DIR}/report.txt)

set(libUbootPlatform ${PLATFORM})

# Set Macros depending on the application 
if(${MICROKIT_APP} STREQUAL "uboot-driver-example")
    set(IMAGES "${MICROKIT_APP}.elf")
    add_definitions(-DUBOOT_DRIVER_EXAMPLE)
endif()

if(${MICROKIT_APP} STREQUAL "security_demo")
    set(IMAGES "keyreader.elf" "transmitter.elf" "crypto.elf")
    add_definitions(-DSECURITY_DEMO)
endif()

# Add libraries 
add_subdirectory(libubootdrivers)
add_subdirectory(libutils)
add_subdirectory(libmicrokitdma)
target_include_directories(microkitdma PUBLIC 
    "${BOARD_DIR}/include" 
    "libubootdrivers/include/public_api/"
    "${PICOLIBC_DIR}/include/")
add_subdirectory(libmicrokitdma_keyreader)
target_include_directories(microkitdma_keyreader PUBLIC 
    "${BOARD_DIR}/include" 
    "libubootdrivers/include/public_api/"
    "${PICOLIBC_DIR}/include/")
add_subdirectory(libmicrokitdma_transmitter)
target_include_directories(microkitdma_transmitter PUBLIC 
    "${BOARD_DIR}/include" 
    "libubootdrivers/include/public_api/"
    "${PICOLIBC_DIR}/include/")

# Transform image files
list(TRANSFORM IMAGES PREPEND "${BUILD_DIR}/")

if(${MICROKIT_APP} STREQUAL "uboot-driver-example")
add_executable(${MICROKIT_APP}.elf example/${PLATFORM}/${MICROKIT_APP}/${MICROKIT_APP}.c)
endif()

if(${MICROKIT_APP} STREQUAL "security_demo")
add_executable(keyreader.elf keyreader/keyreader.c)
add_executable(transmitter.elf transmitter/transmitter.c src/circular_buffer.c)
add_executable(crypto.elf crypto/crypto.c src/circular_buffer.c)
endif()

if(${MICROKIT_APP} STREQUAL "uboot-driver-example")
# Linker - app
target_link_options(${MICROKIT_APP}.elf PRIVATE ${LDFLAGS})
target_compile_options(${MICROKIT_APP}.elf PRIVATE ${CFLAGS})
target_include_directories(${MICROKIT_APP}.elf PRIVATE 
    "${BOARD_DIR}/include" 
    "libubootdrivers/include/public_api/"
    "${PICOLIBC_DIR}/include/"
)
target_link_libraries(${MICROKIT_APP}.elf PUBLIC ubootdrivers utils microkitdma)
get_target_property(UBOOTDRIVERS_INCLUDE_DIRS ubootdrivers INTERFACE_INCLUDE_DIRECTORIES)
target_include_directories(${MICROKIT_APP}.elf PRIVATE UBOOTDRIVERS_INCLUDE_DIRS)
target_link_libraries(${MICROKIT_APP}.elf PRIVATE ${LIBS})
endif()

if(${MICROKIT_APP} STREQUAL "security_demo")
# Linker - keyreader
target_link_options(keyreader.elf PRIVATE ${LDFLAGS})
target_compile_options(keyreader.elf PRIVATE ${CFLAGS})
target_include_directories(keyreader.elf PRIVATE 
    "${BOARD_DIR}/include"  
    "libubootdrivers/include/public_api/"
    "${PICOLIBC_DIR}/include/"
    "include"
)
target_link_libraries(keyreader.elf PUBLIC ubootdrivers utils microkitdma_keyreader)
get_target_property(UBOOTDRIVERS_INCLUDE_DIRS ubootdrivers INTERFACE_INCLUDE_DIRECTORIES)
target_include_directories(keyreader.elf PRIVATE UBOOTDRIVERS_INCLUDE_DIRS)
target_link_libraries(keyreader.elf PRIVATE ${LIBS})

# Linker - transmitter
target_link_options(transmitter.elf PRIVATE ${LDFLAGS})
target_compile_options(transmitter.elf PRIVATE ${CFLAGS})
target_include_directories(transmitter.elf PRIVATE 
    "${BOARD_DIR}/include"  
    "libubootdrivers/include/public_api/"
    "${PICOLIBC_DIR}/include/"
    "include"
)
target_link_libraries(transmitter.elf PUBLIC ubootdrivers utils microkitdma_transmitter)
get_target_property(UBOOTDRIVERS_INCLUDE_DIRS ubootdrivers INTERFACE_INCLUDE_DIRECTORIES)
target_include_directories(transmitter.elf PRIVATE UBOOTDRIVERS_INCLUDE_DIRS)
target_link_libraries(transmitter.elf PRIVATE ${LIBS})

# Linker - crypto
target_link_options(crypto.elf PRIVATE ${LDFLAGS})
target_compile_options(crypto.elf PRIVATE ${CFLAGS})
target_include_directories(crypto.elf PRIVATE 
    "${BOARD_DIR}/include"  
    "${PICOLIBC_DIR}/include/"
    "include"
)
target_link_libraries(crypto.elf PUBLIC ubootdrivers utils)
get_target_property(UBOOTDRIVERS_INCLUDE_DIRS ubootdrivers INTERFACE_INCLUDE_DIRECTORIES)
target_include_directories(crypto.elf PRIVATE UBOOTDRIVERS_INCLUDE_DIRS)
target_link_libraries(crypto.elf PRIVATE ${LIBS})
endif()

add_custom_target(make-img 
    ALL DEPENDS ${IMAGES} "${SYSTEM_FILE}" COMMAND bash -c
    "${MICROKIT_SDK}/bin/microkit ../${MICROKIT_APP}.system --search-path ${EXAMPLE_BUILD_DIR} --board ${MICROKIT_BOARD} --config ${MICROKIT_CONFIG} --output ${IMAGE_FILE} --report ${REPORT_FILE}")


