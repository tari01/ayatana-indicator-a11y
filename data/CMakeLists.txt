
# ayatana-indicator-a11y.service

pkg_check_modules (SYSTEMD systemd)

if (${SYSTEMD_FOUND})

    pkg_get_variable (SYSTEMD_USER_DIR systemd systemduserunitdir)
    configure_file ("${CMAKE_CURRENT_SOURCE_DIR}/${CMAKE_PROJECT_NAME}.service.in" "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}.service")
    install (FILES "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}.service" DESTINATION "${SYSTEMD_USER_DIR}")

endif()

# ayatana-indicator-a11y.desktop

configure_file ("${CMAKE_CURRENT_SOURCE_DIR}/${CMAKE_PROJECT_NAME}.desktop.in" "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}.desktop")
install (FILES "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}.desktop" DESTINATION "/etc/xdg/autostart")

# org.ayatana.indicator.a11y

install (FILES "${CMAKE_CURRENT_SOURCE_DIR}/org.ayatana.indicator.a11y" DESTINATION "${CMAKE_INSTALL_FULL_DATADIR}/ayatana/indicators")
