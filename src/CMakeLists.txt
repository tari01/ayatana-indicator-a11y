# libayatanaindicatora11yservice.a

add_definitions (-DG_LOG_DOMAIN="ayatana-indicator-a11y")
add_library ("ayatanaindicatora11yservice" STATIC service.c)
include_directories (${CMAKE_SOURCE_DIR})
link_directories (${SERVICE_DEPS_LIBRARY_DIRS})

# ayatana-indicator-a11y-service

add_executable ("ayatana-indicator-a11y-service" main.c)
target_link_libraries ("ayatana-indicator-a11y-service" "ayatanaindicatora11yservice" "${SERVICE_DEPS_LIBRARIES} -ldl -lm")
install (TARGETS "ayatana-indicator-a11y-service" RUNTIME DESTINATION "${CMAKE_INSTALL_FULL_LIBEXECDIR}/${CMAKE_PROJECT_NAME}")
