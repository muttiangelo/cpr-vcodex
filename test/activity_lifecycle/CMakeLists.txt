find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(ACTIVITY_MANAGER_METHODS ${CMAKE_CURRENT_BINARY_DIR}/ActivityManagerMethods.inc)
add_custom_command(
  OUTPUT ${ACTIVITY_MANAGER_METHODS}
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/extract_methods.py
          ${REPO_ROOT}/src/activities/ActivityManager.cpp ${ACTIVITY_MANAGER_METHODS}
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/extract_methods.py ${REPO_ROOT}/src/activities/ActivityManager.cpp
  VERBATIM
)
add_executable(ActivityLifecycleTest ActivityLifecycleTest.cpp ${ACTIVITY_MANAGER_METHODS})
target_include_directories(ActivityLifecycleTest PRIVATE ${CMAKE_CURRENT_BINARY_DIR} ${REPO_ROOT}/src)
target_link_libraries(ActivityLifecycleTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(ActivityLifecycleTest)

set(SYNC_DAY_METHODS ${CMAKE_CURRENT_BINARY_DIR}/SyncDayMethods.inc)
set(SYNC_DAY_SOURCE ${REPO_ROOT}/src/util/AutoDaySync.cpp)
add_custom_command(
  OUTPUT ${SYNC_DAY_METHODS}
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/extract_sync_day_methods.py
          ${SYNC_DAY_SOURCE} ${SYNC_DAY_METHODS}
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/extract_sync_day_methods.py ${SYNC_DAY_SOURCE}
  VERBATIM
)
add_executable(SyncDayActivityTest SyncDayActivityTest.cpp ${SYNC_DAY_METHODS})
target_include_directories(SyncDayActivityTest PRIVATE ${CMAKE_CURRENT_BINARY_DIR} ${REPO_ROOT}/src)
target_link_libraries(SyncDayActivityTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(SyncDayActivityTest)
