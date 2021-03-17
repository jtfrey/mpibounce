cmake_minimum_required(VERSION 3.9)

project(mpibounce LANGUAGES C)

find_package(MPI REQUIRED)

# Use imported targets would make things much eazier. Thanks Levi for pointing it out.
add_executable(mpibounce mpibounce.c)
target_link_libraries(mpibounce PRIVATE MPI::MPI_C)
install(TARGETS mpibounce DESTINATION ${CMAKE_INSTALL_PREFIX}/bin COMPONENT binaries)
