# Build-contract checks for the experimental WoW64 front end.  Runtime
# PE32 execution stays in the external GitLab gate because macOS Docker is
# not reliable enough for that path.

if(NOT TARGET pipeasio32 OR NOT TARGET pipeasio64-helper)
    message(STATUS "WoW64 artifacts not available - skipping tests/wow64")
    return()
endif()

find_program(FILE_EXECUTABLE file)
find_program(BASH_EXECUTABLE bash)
find_program(I686_W64_MINGW_GCC i686-w64-mingw32-gcc)
find_program(X86_64_W64_MINGW_GCC x86_64-w64-mingw32-gcc)
find_program(I686_W64_MINGW_OBJDUMP i686-w64-mingw32-objdump)
find_program(X86_64_W64_MINGW_OBJDUMP x86_64-w64-mingw32-objdump)

if(NOT FILE_EXECUTABLE OR NOT BASH_EXECUTABLE
   OR NOT I686_W64_MINGW_GCC OR NOT X86_64_W64_MINGW_GCC
   OR NOT I686_W64_MINGW_OBJDUMP OR NOT X86_64_W64_MINGW_OBJDUMP)
    message(WARNING "bash, file, or mingw tools missing - skipping WoW64 tests.")
    return()
endif()

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/check_artifacts.sh.in"
    "${CMAKE_CURRENT_BINARY_DIR}/check_artifacts.sh"
    @ONLY
)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/check_ipc_layout.sh.in"
    "${CMAKE_CURRENT_BINARY_DIR}/check_ipc_layout.sh"
    @ONLY
)

add_test(NAME wow64_artifacts
         COMMAND "${BASH_EXECUTABLE}" "${CMAKE_CURRENT_BINARY_DIR}/check_artifacts.sh")
set_tests_properties(wow64_artifacts PROPERTIES
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    TIMEOUT 20
    LABELS "wow64;abi")

add_test(NAME wow64_ipc_layout
         COMMAND "${BASH_EXECUTABLE}" "${CMAKE_CURRENT_BINARY_DIR}/check_ipc_layout.sh")
set_tests_properties(wow64_ipc_layout PROPERTIES
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    TIMEOUT 20
    LABELS "wow64;abi")
