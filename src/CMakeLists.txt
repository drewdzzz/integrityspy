add_subdirectory(lib)
add_executable(integrityspy main.c)
target_link_libraries(integrityspy lib)
