CMAKE_MINIMUM_REQUIRED(VERSION 3.0)

include_directories(${PROJECT_SOURCE_DIR}/.. )

set(VAPI_LIB ${CMAKE_CURRENT_BINARY_DIR}/../libvapi.a)

add_executable(json_example json_example.cpp ../src/param_json.cpp)
execute_process(COMMAND cp ${CMAKE_CURRENT_SOURCE_DIR}/device.json example/device.json WORKING_DIRECTORY .)

add_executable(vloop_example vloop_example.c)
target_link_libraries(vloop_example ${VAPI_LIB} pthread)

#add_executable(vdbg_example vdbg_example.c)
#target_link_libraries(vdbg_example ${VAPI_LIB} pthread stdc++ m cgroup event zstd)

#add_executable(timer_example timer_example.c)
#target_link_libraries(timer_example ${VAPI_LIB} pthread stdc++ m cgroup event zstd)
