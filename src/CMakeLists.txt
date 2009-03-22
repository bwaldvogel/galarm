add_executable(galarm main.c)

include(FindPkgConfig)
include(CheckIncludeFile)
include(CheckFunctionExists)

pkg_check_modules (GLIB2        REQUIRED    glib-2.0>=2.14)
pkg_check_modules (GTK          REQUIRED    gtk+-2.0>=2.12)
pkg_check_modules (LIBNOTIFY    REQUIRED    libnotify>=0.3.2)
pkg_check_modules (LIBCANBERRA  REQUIRED    libcanberra>=0.10)

# for config.h
include_directories(${CMAKE_CURRENT_BINARY_DIR}/..)

include_directories(${GLIB2_INCLUDE_DIRS})
include_directories(${GTK_INCLUDE_DIRS})
include_directories(${LIBNOTIFY_INCLUDE_DIRS})
include_directories(${LIBCANBERRA_INCLUDE_DIRS})

target_link_libraries(galarm ${GLIB2_LIBRARIES})
target_link_libraries(galarm ${GTK_LIBRARIES})
target_link_libraries(galarm ${LIBNOTIFY_LIBRARIES})
target_link_libraries(galarm ${LIBCANBERRA_LIBRARIES})

macro(require_include_file _header)
    check_include_file(${_header} ${_header}_INCLUDE_FILE)
    if (NOT ${_header}_INCLUDE_FILE)
        message(FATAL_ERROR "${_header} was not found")
    endif (NOT ${_header}_INCLUDE_FILE)
endmacro(require_include_file)

macro(require_function_exists _function)
    check_function_exists(${_function} ${_function}_FUNCTION_EXISTS)
    if (NOT ${_function}_FUNCTION_EXISTS)
        message(FATAL_ERROR "${_function} does not exist")
    endif (NOT ${_function}_FUNCTION_EXISTS)
endmacro(require_function_exists)

install(
  TARGETS
    galarm
  RUNTIME DESTINATION bin
)

require_include_file(unistd.h)
require_include_file(stdlib.h)
require_include_file(math.h)
require_include_file(sys/stat.h)
require_include_file(errno.h)
require_include_file(signal.h)
require_include_file(time.h)
require_include_file(fcntl.h)

# ln -s /usr/bin/galarm /usr/bin/ga
install(CODE "execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink \"${BIN_INSTALL_DIR}/galarm\" \"${BIN_INSTALL_DIR}/ga\" )" )

# vim: set ts=4 sw=4 et:
