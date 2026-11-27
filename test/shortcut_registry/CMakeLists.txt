find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(I18N_TEST_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${I18N_TEST_DIR}")
file(GLOB I18N_TRANSLATIONS CONFIGURE_DEPENDS ${REPO_ROOT}/lib/I18n/translations/*.yaml)
add_custom_command(
  OUTPUT ${I18N_TEST_DIR}/I18nKeys.h
  COMMAND ${Python3_EXECUTABLE} -X utf8 ${REPO_ROOT}/scripts/gen_i18n.py
          ${REPO_ROOT}/lib/I18n/translations ${I18N_TEST_DIR}
  DEPENDS ${REPO_ROOT}/scripts/gen_i18n.py ${I18N_TRANSLATIONS}
  VERBATIM
)
add_executable(ShortcutRegistryTest ShortcutRegistryTest.cpp ${I18N_TEST_DIR}/I18nKeys.h)
target_include_directories(ShortcutRegistryTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${REPO_ROOT}/test/reader_regressions/fakes
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/I18n
  ${REPO_ROOT}/lib/Epub
  ${I18N_TEST_DIR}
)
target_link_libraries(ShortcutRegistryTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(ShortcutRegistryTest)
