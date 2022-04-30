# 
# Copyright (c) 2013-2017 Ardexa Pty Ltd
#
# This code is licensed under the MIT License (MIT).
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
# IN THE SOFTWARE.
#
# CMAKE file for the Ardexa plugin to read an SMA Bluetooth enabled inverter
#
# Notes:
# 1.	Makesure the bluetooth dev library is installed.... via: sudo apt-get install -y libbluetooth-dev
#

CMAKE_MINIMUM_REQUIRED(VERSION 2.4)

# Set the project name
PROJECT( sma2redis)
add_executable(sma2redis)

set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g ")
set(CMAKE_C_FLAGS "${CMAKE_CXX_FLAGS} -g ")
add_definitions(-DLINUX -std=c++17)

# Set the output folder where your program will be created
set(CMAKE_BINARY_DIR .)
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR})
set(HIREDIS hiredis )
set(LINUX limero/linux)
set(LIMERO limero)
set(ARDUINOJSON ArduinoJson/src)


include_directories(
    src 
    ${LIMERO}/inc
    ${LIMERO}/linux
    ${HIREDIS}
    ${ARDUINOJSON}
    ${SEASOCKS_INC}
    )
set(CMAKE_CXX_IMPLICIT_LINK_DIRECTORIES /usr/local/lib ${CMAKE_CXX_IMPLICIT_LINK_DIRECTORIES})
set(CMAKE_CXX_FLAGS "-Wall -fexceptions --std=c++11")

# Source files
target_sources(sma2redis PRIVATE 
    src/config.cpp 
    ${LIMERO}/linux/Redis.cpp
    ${LIMERO}/linux/Log.cpp
    ${LIMERO}/linux/Sys.cpp
    ${LIMERO}/linux/limero.cpp
    ${LIMERO}/src/printf.c
    ${LIMERO}/src/StringUtility.cpp
    src/sma2redis.cpp 
    src/utils.cpp 
    src/in_bluetooth.cpp 
    src/in_smadata2plus.cpp
    ) 

target_link_libraries(sma2redis 
    -lpthread  
    -L${HIREDIS} -l:libhiredis.a
    -lrt  
    -lm 
    -lbluetooth
    -latomic)
# TARGET_LINK_LIBRARIES(sma2redis m bluetooth)
SET_TARGET_PROPERTIES(sma2redis PROPERTIES LINKER_LANGUAGE CXX)

# add the install targets
install (TARGETS sma2redis DESTINATION /usr/local/bin)



