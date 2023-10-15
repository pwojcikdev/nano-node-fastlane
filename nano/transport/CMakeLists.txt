add_library(
        transport
        transport.hpp
        tcp.hpp
        tcp.cpp)

target_link_libraries(
        transport
        nano_messages
        Boost::circular_buffer
        Boost::endian
        Boost::filesystem
        Boost::iostreams
        Boost::multiprecision
        Boost::pool
        Boost::stacktrace
        Boost::variant)

include_directories(${BOOST_LIBRARY_INCLUDES})
include_directories(${CMAKE_SOURCE_DIR}/submodules)