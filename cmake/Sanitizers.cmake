function(nexus_enable_sanitizers target_name)
  if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    return()
  endif()

  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(
      "${target_name}"
      PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer)
    target_link_options("${target_name}" PRIVATE -fsanitize=address,undefined)
  endif()
endfunction()
