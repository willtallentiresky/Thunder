#find_package(${NAMESPACE}Core REQUIRED)
#find_package(${NAMESPACE}Cryptalgo REQUIRED)
#find_package(${NAMESPACE}WebSocket REQUIRED)
#find_package(${NAMESPACE}Tracing REQUIRED)

add_executable(proxy_test
    Module.cpp
    main.cpp
)

target_link_libraries(proxy_test
    PRIVATE
        ${NAMESPACE}Core
        ${NAMESPACE}Tracing
)

install(TARGETS proxy_test DESTINATION bin)
