cmake_minimum_required(VERSION 3.10)
project(pkben C)

set(CMAKE_C_STANDARD 11)

add_executable(pkben main.c sqlite3.c)

if(WIN32)
    target_compile_definitions(pkben PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_link_libraries(pkben pthread dl m)
endif()
