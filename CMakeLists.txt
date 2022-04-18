CMAKE_MINIMUM_REQUIRED(VERSION 3.0)
PROJECT(vapi)

include_directories(${PROJECT_SOURCE_DIR} )
SET(LIB_SRC src/bufprintf.c
            src/vdbg.cpp
            src/verror.c
            src/vfs.c
            src/vevent.c
            src/vlog_syslog.c
            src/vlog_vapi.c
            #src/vlog.c
            #src/vlog_opentracing.c
            src/vloop_demand_event.c
            src/vlist.c
            src/vmem.c
            src/vmem_pool.c
            src/vmutex.c
            src/vsignal.c
            src/vsystem.c
            src/vtime.c
            src/vtimer.c
            src/vtnd.c
            src/vtnd_text.c
            src/vtnd_text_dispatch.c
            src/vtnd_console.c
            src/vtnd_file.c
            src/vtnd_log.c
            src/vloop.c)
            #src/unixthread.cpp)

ADD_LIBRARY(vapi_static STATIC ${LIB_SRC})
ADD_LIBRARY(vapi_share SHARED ${LIB_SRC})

SET_TARGET_PROPERTIES(vapi_static PROPERTIES OUTPUT_NAME "vapi")
SET_TARGET_PROPERTIES(vapi_share PROPERTIES OUTPUT_NAME "vapi")

#add_subdirectory (example)
