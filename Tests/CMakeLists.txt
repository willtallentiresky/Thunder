option(LOADER_TEST "Utility to load a plugin in isolation." OFF)
option(HTTPSCLIENT_TEST "Example how to do https requests with Thunder." OFF)
option(PROXYTYPE_TEST "Sample program to test the Proxy Type Additional space usage." OFF)

if(BUILD_TESTS)
    add_subdirectory(unit)
endif()

if(HTTPSCLIENT_TEST) 
    add_subdirectory(httpsclient)
endif()

if(PROXYTYPE_TEST)
add_subdirectory(proxytype_test)
endif()

if(LOADER_TEST) 
    add_subdirectory(loader)
endif()
