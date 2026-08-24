# CMake generated Testfile for 
# Source directory: C:/Users/azureuser/Desktop/notepad/notepad_ai/PowerEditor/Test/FormatOnSave
# Build directory: C:/Users/azureuser/Desktop/notepad/notepad_ai/PowerEditor/Test/FormatOnSave/build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
if(CTEST_CONFIGURATION_TYPE MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
  add_test("powereditor_format_on_save_tests" "C:/Users/azureuser/Desktop/notepad/notepad_ai/PowerEditor/Test/FormatOnSave/build/Debug/powereditor_format_on_save_tests.exe")
  set_tests_properties("powereditor_format_on_save_tests" PROPERTIES  _BACKTRACE_TRIPLES "C:/Users/azureuser/Desktop/notepad/notepad_ai/PowerEditor/Test/FormatOnSave/CMakeLists.txt;35;add_test;C:/Users/azureuser/Desktop/notepad/notepad_ai/PowerEditor/Test/FormatOnSave/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
  add_test("powereditor_format_on_save_tests" "C:/Users/azureuser/Desktop/notepad/notepad_ai/PowerEditor/Test/FormatOnSave/build/Release/powereditor_format_on_save_tests.exe")
  set_tests_properties("powereditor_format_on_save_tests" PROPERTIES  _BACKTRACE_TRIPLES "C:/Users/azureuser/Desktop/notepad/notepad_ai/PowerEditor/Test/FormatOnSave/CMakeLists.txt;35;add_test;C:/Users/azureuser/Desktop/notepad/notepad_ai/PowerEditor/Test/FormatOnSave/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
  add_test("powereditor_format_on_save_tests" "C:/Users/azureuser/Desktop/notepad/notepad_ai/PowerEditor/Test/FormatOnSave/build/MinSizeRel/powereditor_format_on_save_tests.exe")
  set_tests_properties("powereditor_format_on_save_tests" PROPERTIES  _BACKTRACE_TRIPLES "C:/Users/azureuser/Desktop/notepad/notepad_ai/PowerEditor/Test/FormatOnSave/CMakeLists.txt;35;add_test;C:/Users/azureuser/Desktop/notepad/notepad_ai/PowerEditor/Test/FormatOnSave/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
  add_test("powereditor_format_on_save_tests" "C:/Users/azureuser/Desktop/notepad/notepad_ai/PowerEditor/Test/FormatOnSave/build/RelWithDebInfo/powereditor_format_on_save_tests.exe")
  set_tests_properties("powereditor_format_on_save_tests" PROPERTIES  _BACKTRACE_TRIPLES "C:/Users/azureuser/Desktop/notepad/notepad_ai/PowerEditor/Test/FormatOnSave/CMakeLists.txt;35;add_test;C:/Users/azureuser/Desktop/notepad/notepad_ai/PowerEditor/Test/FormatOnSave/CMakeLists.txt;0;")
else()
  add_test("powereditor_format_on_save_tests" NOT_AVAILABLE)
endif()
