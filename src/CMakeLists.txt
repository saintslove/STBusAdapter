link_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../muduo/Debug/  ${CMAKE_CURRENT_SOURCE_DIR}/../../BATNetSDK/Debug/ ${CMAKE_CURRENT_SOURCE_DIR}/../../STGPSProtocol/build/lib/)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../STGPSProtocol/src  ${CMAKE_CURRENT_SOURCE_DIR}/../../Include ${CMAKE_CURRENT_SOURCE_DIR}/../../muduo/)
set(SOURCE_FILES STBusAdapter.cpp STBusAdapter.h base.h xmlParser.cpp xmlParser.h Server.cpp base.cpp)
add_executable(STBusAdapter ${SOURCE_FILES})
target_link_libraries(STBusAdapter STGPSProtocol BATNetSDK muduo pthread)
set(EXECUTABLE_OUTPUT_PATH  ${PROJECT_BINARY_DIR}/bin)
