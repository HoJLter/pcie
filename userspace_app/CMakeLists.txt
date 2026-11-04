cmake_minimum_required(VERSION 3.22)

project(userspace_app)

find_package(PkgConfig REQUIRED)
pkg_check_modules(TCPREPLAY REQUIRED libtcpreplay)

add_executable(app src/main.c src/sender_socket.c src/sender_tcpreplay.c src/optimization.c)

target_include_directories(app PRIVATE ${TCPREPLAY_STATIC_INCLUDE_DIRS})
target_include_directories(app PRIVATE include)

target_link_directories(app PRIVATE ${TCPREPLAY_STATIC_LIBRARY_DIRS})
target_link_libraries(app PRIVATE ${TCPREPLAY_STATIC_LIBRARIES})
