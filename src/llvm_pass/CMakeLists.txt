# LLVM pass plugin (NEY709): built only when LLVM's development files are found (see the top-level option).
add_library(NexusPasses MODULE NexusPasses.cpp)
target_include_directories(NexusPasses SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})
separate_arguments(NEXUS_LLVM_DEFINITIONS NATIVE_COMMAND "${LLVM_DEFINITIONS}")
target_compile_definitions(NexusPasses PRIVATE ${NEXUS_LLVM_DEFINITIONS})
target_compile_features(NexusPasses PRIVATE cxx_std_17)
if(NOT LLVM_ENABLE_RTTI)
  target_compile_options(NexusPasses PRIVATE -fno-rtti)
endif()
# opt already holds the LLVM symbols the plugin uses.
set_target_properties(NexusPasses PROPERTIES PREFIX "" LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
