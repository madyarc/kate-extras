# Rewrites a plugin's library references so it loads the Qt and KDE Frameworks that
# ship inside Kate.app, the same way Kate's own bundled plugins do.
#
# Usage: cmake -DFILE=<plugin.so> -P BundleLinks.cmake

execute_process(COMMAND otool -L "${FILE}" OUTPUT_VARIABLE libs COMMAND_ERROR_IS_FATAL ANY)
string(REPLACE "\n" ";" libs "${libs}")

set(args)
foreach(line IN LISTS libs)
    string(STRIP "${line}" line)
    string(REGEX REPLACE " \\(compatibility.*" "" lib "${line}")
    if(lib MATCHES "^/(System|usr)/" OR NOT lib MATCHES "^[/@]")
        continue()
    endif()
    if(lib MATCHES "([A-Za-z0-9_]+\\.framework/.*)$")
        set(name "${CMAKE_MATCH_1}")
    else()
        get_filename_component(name "${lib}" NAME)
    endif()
    list(APPEND args -change "${lib}" "@executable_path/../Frameworks/${name}")
endforeach()

execute_process(COMMAND otool -l "${FILE}" OUTPUT_VARIABLE loadCommands COMMAND_ERROR_IS_FATAL ANY)
string(REGEX MATCHALL "path [^\n]+ \\(offset" rpaths "${loadCommands}")
foreach(rpath IN LISTS rpaths)
    string(REGEX REPLACE "^path (.+) \\(offset$" "\\1" rpath "${rpath}")
    list(APPEND args -delete_rpath "${rpath}")
endforeach()

if(args)
    execute_process(COMMAND install_name_tool ${args} "${FILE}" COMMAND_ERROR_IS_FATAL ANY)
endif()

# install_name_tool invalidates the linker's signature; arm64 refuses to load unsigned code
execute_process(COMMAND codesign --force --sign - "${FILE}" OUTPUT_QUIET ERROR_QUIET COMMAND_ERROR_IS_FATAL ANY)
