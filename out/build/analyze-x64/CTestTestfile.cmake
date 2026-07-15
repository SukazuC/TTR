# CMake generated Testfile for 
# Source directory: C:/Users/lenovo/Documents/Projects/TTR
# Build directory: C:/Users/lenovo/Documents/Projects/TTR/out/build/analyze-x64
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
if(CTEST_CONFIGURATION_TYPE MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
  add_test([=[ttr_unit_tests]=] "C:/Users/lenovo/Documents/Projects/TTR/out/build/analyze-x64/Debug/ttr_unit_tests.exe")
  set_tests_properties([=[ttr_unit_tests]=] PROPERTIES  _BACKTRACE_TRIPLES "C:/Users/lenovo/Documents/Projects/TTR/CMakeLists.txt;136;add_test;C:/Users/lenovo/Documents/Projects/TTR/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
  add_test([=[ttr_unit_tests]=] "C:/Users/lenovo/Documents/Projects/TTR/out/build/analyze-x64/Release/ttr_unit_tests.exe")
  set_tests_properties([=[ttr_unit_tests]=] PROPERTIES  _BACKTRACE_TRIPLES "C:/Users/lenovo/Documents/Projects/TTR/CMakeLists.txt;136;add_test;C:/Users/lenovo/Documents/Projects/TTR/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
  add_test([=[ttr_unit_tests]=] "C:/Users/lenovo/Documents/Projects/TTR/out/build/analyze-x64/MinSizeRel/ttr_unit_tests.exe")
  set_tests_properties([=[ttr_unit_tests]=] PROPERTIES  _BACKTRACE_TRIPLES "C:/Users/lenovo/Documents/Projects/TTR/CMakeLists.txt;136;add_test;C:/Users/lenovo/Documents/Projects/TTR/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
  add_test([=[ttr_unit_tests]=] "C:/Users/lenovo/Documents/Projects/TTR/out/build/analyze-x64/RelWithDebInfo/ttr_unit_tests.exe")
  set_tests_properties([=[ttr_unit_tests]=] PROPERTIES  _BACKTRACE_TRIPLES "C:/Users/lenovo/Documents/Projects/TTR/CMakeLists.txt;136;add_test;C:/Users/lenovo/Documents/Projects/TTR/CMakeLists.txt;0;")
else()
  add_test([=[ttr_unit_tests]=] NOT_AVAILABLE)
endif()
