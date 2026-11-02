# Unit tests for app code that only needs QtCore. They compile the real sources, not copies, so they
# test what ships. Nothing here is installed, so none of it reaches the installer.
# How to run them: nm-pbx-docs/tests.md

set(APP_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/..")

qt6_add_executable(LimitProxyTest
	tst_LimitProxy.cpp
	${APP_SOURCE_DIR}/core/proxy/LimitProxy.cpp
	${APP_SOURCE_DIR}/core/proxy/LimitProxy.hpp
	${APP_SOURCE_DIR}/core/proxy/SortFilterProxy.cpp
	${APP_SOURCE_DIR}/core/proxy/SortFilterProxy.hpp
)
set_target_properties(LimitProxyTest PROPERTIES AUTOMOC ON)
target_include_directories(LimitProxyTest PRIVATE "${APP_SOURCE_DIR}")
target_link_libraries(LimitProxyTest PRIVATE Qt6::Core Qt6::Test)

add_test(NAME LimitProxyTest COMMAND LimitProxyTest)
# The Qt DLLs are not deployed next to the test, so put Qt's own bin directory on the path.
set_tests_properties(LimitProxyTest PROPERTIES
	ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
)

# Header only, and kept free of the SDK so it can be tested here.
qt6_add_executable(PhoneNumberIndexTest
	tst_PhoneNumberIndex.cpp
	${APP_SOURCE_DIR}/model/friend/PhoneNumberIndex.hpp
)
set_target_properties(PhoneNumberIndexTest PROPERTIES AUTOMOC ON)
target_include_directories(PhoneNumberIndexTest PRIVATE "${APP_SOURCE_DIR}")
target_link_libraries(PhoneNumberIndexTest PRIVATE Qt6::Core Qt6::Test)

add_test(NAME PhoneNumberIndexTest COMMAND PhoneNumberIndexTest)
set_tests_properties(PhoneNumberIndexTest PROPERTIES
	ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
)
