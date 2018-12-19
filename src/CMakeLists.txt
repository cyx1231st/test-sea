# build seastar
macro(find_package name)
  if(NOT TARGET ${name})
    _find_package(${ARGV})
  endif()
endmacro ()
add_subdirectory(seastar/fmt)
include(Buildc-ares)
build_c_ares()
set(Seastar_HWLOC OFF CACHE BOOL "" FORCE)
set(Seastar_STD_OPTIONAL_VARIANT_STRINGVIEW ON CACHE BOOL "" FORCE)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/seastar/gen/include")
add_subdirectory(seastar)

# seastar library
add_library(seastar::cflags INTERFACE IMPORTED)
set_target_properties(seastar::cflags PROPERTIES
    INTERFACE_LINK_LIBRARIES Seastar::seastar)
set(common_seastar_deps
    seastar::cflags ${common_deps})

add_executable(test_socket test_socket.cc)
target_link_libraries(test_socket ${common_seastar_deps})

add_executable(test_ptr test_ptr.cc)
target_link_libraries(test_ptr ${common_seastar_deps})

add_executable(test_native_ptr test_native_ptr.cc)
target_link_libraries(test_ptr ${common_deps})
