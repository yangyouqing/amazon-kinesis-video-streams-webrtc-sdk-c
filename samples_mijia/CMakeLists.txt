cmake_minimum_required(VERSION 3.6.3)

project(KinesisVideoMijiaVideoSender LANGUAGES C)

#set(OPEN_SRC_INSTALL_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/open-source" CACHE PATH "Libraries will be downloaded and build in this directory.")

message("OPEN_SRC_INSTALL_PREFIX=${OPEN_SRC_INSTALL_PREFIX}")

find_package(PkgConfig REQUIRED)


include_directories(${OPEN_SRC_INSTALL_PREFIX}/include)
include_directories(${OPEN_SRC_INCLUDE_DIRS})
link_directories(${OPEN_SRC_INSTALL_PREFIX}/lib)


add_executable(
  kvsMijiaVideoSender
  streamfetch.c
  Common.c
  kvsWebRTCClientMaster.c)
target_link_libraries(kvsMijiaVideoSender kvsWebrtcClient kvsWebrtcSignalingClient kvsCommonLws kvspicUtils websockets shbf shbfev ev)




install(TARGETS kvsMijiaVideoSender
  RUNTIME DESTINATION bin
)
