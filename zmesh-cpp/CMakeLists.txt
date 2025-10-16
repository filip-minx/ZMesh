cmake_minimum_required(VERSION 3.16)
project(zmesh-cpp VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(ZeroMQ REQUIRED)
find_package(nlohmann_json 3.2.0 REQUIRED)

find_path(CPPZMQ_INCLUDE_DIR zmq.hpp)
if (NOT CPPZMQ_INCLUDE_DIR)
    message(FATAL_ERROR "Unable to locate zmq.hpp. Install cppzmq or specify CPPZMQ_INCLUDE_DIR.")
endif()

add_library(zmesh STATIC
    src/message_box.cpp
    src/messages.cpp
    src/zmesh.cpp
)

target_include_directories(zmesh
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
        ${CPPZMQ_INCLUDE_DIR}
        ${ZeroMQ_INCLUDE_DIRS}
)

target_link_libraries(zmesh
    PUBLIC
        nlohmann_json::nlohmann_json
)

if (TARGET libzmq)
    target_link_libraries(zmesh PUBLIC libzmq)
elseif (TARGET ZeroMQ::libzmq)
    target_link_libraries(zmesh PUBLIC ZeroMQ::libzmq)
elseif (TARGET libzmq-static)
    target_link_libraries(zmesh PUBLIC libzmq-static)
elseif (DEFINED ZeroMQ_LIBRARIES)
    target_link_libraries(zmesh PUBLIC ${ZeroMQ_LIBRARIES})
else()
    message(FATAL_ERROR "Unable to determine ZeroMQ library target")
endif()

option(ZMESH_BUILD_EXAMPLES "Build zmesh-cpp usage examples" ON)

if (ZMESH_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

install(TARGETS zmesh EXPORT zmeshTargets)
install(DIRECTORY include/ DESTINATION include)

include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/zmeshConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(EXPORT zmeshTargets
    FILE zmeshTargets.cmake
    NAMESPACE zmesh::
    DESTINATION lib/cmake/zmesh
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/zmeshConfigVersion.cmake
    DESTINATION lib/cmake/zmesh
)

