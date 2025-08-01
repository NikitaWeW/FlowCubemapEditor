# This line will be changed if reconfiguration is required       
cmake_minimum_required(VERSION 3.10)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g")

# thanks to https://stackoverflow.com/a/27206982/23847181
function(create_resources dir output)
    # Create empty output file
    file(WRITE ${output} "")
    # Collect input files
    file(GLOB bins ${dir}/*)
    # Iterate through input files
    foreach(bin ${bins})
        # Get short filename
        string(REGEX MATCH "([^/]+)$" filename ${bin})
        # Replace filename spaces & extension separator for C compatibility
        string(REGEX REPLACE "\\.| |-" "_" filename ${filename})
        # Read hex data from file
        file(READ ${bin} filedata HEX)
        # Convert hex data for C compatibility
        string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," filedata ${filedata})
        # Append data to output file
        file(APPEND ${output} "const unsigned char ${filename}[] = {${filedata}};\nconst unsigned ${filename}_size = sizeof(${filename});\n")
    endforeach()
endfunction()

project(Flow_Cubemap_Editor C CXX)

file(GLOB_RECURSE SOURCES "src/*.cpp")
file(GLOB_RECURSE DEPENDENCIES_SOURCES "dependencies/compile/*")

if(WIN32)
    file(GLOB_RECURSE LIBRARIES "dependencies/lib/windows/*")
elseif(UNIX AND NOT APPLE) # linux
    file(GLOB_RECURSE LIBRARIES "dependencies/lib/linux/*")
elseif(APPLE)
    message(WARNING "mac users will have to set LIBRARIES variable manualy via adding -DLIBRRAIES=\"all the necessary library files\" to cmake configure command.")
    # "Poor mac ysers" -- DEA__TH (Cosmic Horizons Dev) - 3/21/25, 5:30 PM
endif() 

install(DIRECTORY res               DESTINATION .)
install(DIRECTORY shaders           DESTINATION .)

# TODO: embed resources
# create_resources("res",     "${CMAKE_BINARY_DIR}/res.c");
# create_resources("shaders", "${CMAKE_BINARY_DIR}/shaders.c");

add_executable(flow_cubemap_editor ${SOURCES} ${DEPENDENCIES_SOURCES})

target_link_libraries(flow_cubemap_editor PRIVATE ${LIBRARIES})
target_include_directories(flow_cubemap_editor PRIVATE dependencies/include dependencies/include/imgui src)

install(TARGETS flow_cubemap_editor DESTINATION .)
install(FILES LICENSE.txt README.md DESTINATION .)
