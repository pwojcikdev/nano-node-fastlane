add_library(nano_messages messages.hpp messages.cpp)

target_link_libraries(nano_messages nano_lib)

include_directories(${BOOST_LIBRARY_INCLUDES})
include_directories(${CMAKE_SOURCE_DIR}/submodules)
