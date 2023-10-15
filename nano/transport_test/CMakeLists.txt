add_executable(transport_test entry.cpp transport.cpp)

target_link_libraries(transport_test test_common)

include_directories(${CMAKE_SOURCE_DIR}/submodules)
#include_directories(${CMAKE_SOURCE_DIR}/submodules/gtest/googletest/include)
