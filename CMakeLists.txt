cmake_minimum_required(VERSION 3.16)
project(lab_OC_04 C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(child src/child.c)
add_executable(parent src/parent.c)
add_dependencies(parent child)

configure_file(src/input.txt input.txt COPYONLY)

foreach(test_file IN ITEMS
        last_line_no_newline.txt
        5000_ones.txt
        long_line_5000_chars.txt
        empty.txt
        bad_token.txt
        too_long_number.txt
        overflow.txt)
    configure_file(tests/${test_file} tests/${test_file} COPYONLY)
endforeach()
