# CMake generated Testfile for 
# Source directory: /media/Backup/C_Projects/BCDScribe
# Build directory: /media/Backup/C_Projects/BCDScribe/build-appimage-Ninja
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(BCDScribeDeviceDescriptorTests "/media/Backup/C_Projects/BCDScribe/build-appimage-Ninja/BCDScribeDeviceDescriptorTests")
set_tests_properties(BCDScribeDeviceDescriptorTests PROPERTIES  _BACKTRACE_TRIPLES "/media/Backup/C_Projects/BCDScribe/CMakeLists.txt;63;add_test;/media/Backup/C_Projects/BCDScribe/CMakeLists.txt;0;")
add_test(BCDScribeConsoleHelp "/media/Backup/C_Projects/BCDScribe/build-appimage-Ninja/BCDScribe" "--help")
set_tests_properties(BCDScribeConsoleHelp PROPERTIES  _BACKTRACE_TRIPLES "/media/Backup/C_Projects/BCDScribe/CMakeLists.txt;64;add_test;/media/Backup/C_Projects/BCDScribe/CMakeLists.txt;0;")
add_test(BCDScribeConsoleVersion "/media/Backup/C_Projects/BCDScribe/build-appimage-Ninja/BCDScribe" "--version")
set_tests_properties(BCDScribeConsoleVersion PROPERTIES  _BACKTRACE_TRIPLES "/media/Backup/C_Projects/BCDScribe/CMakeLists.txt;65;add_test;/media/Backup/C_Projects/BCDScribe/CMakeLists.txt;0;")
