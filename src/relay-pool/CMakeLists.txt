project(Rtmp)
cmake_minimum_required (VERSION 2.6) 
find_package (Threads)
ADD_EXECUTABLE( app.out stream.cpp threadpool.cpp relay_pool_proxy.cpp )
target_link_libraries (app.out ${CMAKE_THREAD_LIBS_INIT})

