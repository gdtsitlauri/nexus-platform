find_package(Python3 COMPONENTS Interpreter REQUIRED)

add_subdirectory(unit)
add_subdirectory(integration)
add_subdirectory(golden)
