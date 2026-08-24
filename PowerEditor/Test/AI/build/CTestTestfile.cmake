# CMake generated Testfile for 
# Source directory: C:/Users/azureuser/Desktop/notepad/notepad_ai/PowerEditor/Test/AI
# Build directory: C:/Users/azureuser/Desktop/notepad/notepad_ai/PowerEditor/Test/AI/build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
if(CTEST_CONFIGURATION_TYPE MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
  add_test("powereditor_ai_core_tests" "C:/Users/azureuser/Desktop/notepad/notepad_ai/PowerEditor/Test/AI/build/Debug/powereditor_ai_core_tests.exe")
  set_tests_properties("powereditor_ai_core_tests" PROPERTIES  _BACKTRACE_TRIPLES "C:/Users/azureuser/Desktop/notepad/notepad_ai/PowerEditor/Test/AI/CMakeLists.txt;56;add_test;C:/Users/azureuser/Desktop/notepad/notepad_ai/PowerEditor/Test/AI/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
  add_test("powereditor_ai_core_tests" "C:/Users/azureuser/Desktop/notepad/notepad_ai/PowerEditor/Test/AI/build/Release/powereditor_ai_core_tests.exe")
  set_tests_properties("powereditor_ai_core_tests" PROPERTIES  _BACKTRACE_TRIPLES "C:/Users/azureuser/Desktop/notepad/notepad_ai/PowerEditor/Test/AI/CMakeLists.txt;56;add_test;C:/Users/azureuser/Desktop/notepad/notepad_ai/PowerEditor/Test/AI/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
  add_test("powereditor_ai_core_tests" "C:/Users/azureuser/Desktop/notepad/notepad_ai/PowerEditor/Test/AI/build/MinSizeRel/powereditor_ai_core_tests.exe")
  set_tests_properties("powereditor_ai_core_tests" PROPERTIES  _BACKTRACE_TRIPLES "C:/Users/azureuser/Desktop/notepad/notepad_ai/PowerEditor/Test/AI/CMakeLists.txt;56;add_test;C:/Users/azureuser/Desktop/notepad/notepad_ai/PowerEditor/Test/AI/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
  add_test("powereditor_ai_core_tests" "C:/Users/azureuser/Desktop/notepad/notepad_ai/PowerEditor/Test/AI/build/RelWithDebInfo/powereditor_ai_core_tests.exe")
  set_tests_properties("powereditor_ai_core_tests" PROPERTIES  _BACKTRACE_TRIPLES "C:/Users/azureuser/Desktop/notepad/notepad_ai/PowerEditor/Test/AI/CMakeLists.txt;56;add_test;C:/Users/azureuser/Desktop/notepad/notepad_ai/PowerEditor/Test/AI/CMakeLists.txt;0;")
else()
  add_test("powereditor_ai_core_tests" NOT_AVAILABLE)
endif()
