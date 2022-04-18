CMAKE_MINIMUM_REQUIRED(VERSION 3.0)
#PROJECT(vapi)

include_directories(${PROJECT_SOURCE_DIR}/.. )

set(VAPI_LIB ${CMAKE_CURRENT_BINARY_DIR}/../libvapi.a)

add_executable(vloop_example vloop_example.c)
target_link_libraries(vloop_example ${VAPI_LIB} pthread)

add_executable(vdbg_example vdbg_example.c)
target_link_libraries(vdbg_example ${VAPI_LIB} pthread stdc++ m cgroup)

#add_executable(timer_example timer_example.c)
#target_link_libraries(timer_example ${VAPI_LIB} pthread)
