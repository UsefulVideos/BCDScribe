# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "Release")
  file(REMOVE_RECURSE
  "BCDScribeDeviceDescriptorTests_autogen"
  "BCDScribe_autogen"
  "CMakeFiles/BCDScribeDeviceDescriptorTests_autogen.dir/AutogenUsed.txt"
  "CMakeFiles/BCDScribeDeviceDescriptorTests_autogen.dir/ParseCache.txt"
  "CMakeFiles/BCDScribe_autogen.dir/AutogenUsed.txt"
  "CMakeFiles/BCDScribe_autogen.dir/ParseCache.txt"
  )
endif()
