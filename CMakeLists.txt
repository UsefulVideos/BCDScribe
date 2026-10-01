cmake_minimum_required(VERSION 3.16)
project(BCDScribe CXX)

include(GNUInstallDirs)
find_package(Git QUIET)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find Qt6 components
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

add_executable(BCDScribe
    main.cpp
    mainwindow.h
    mainwindow.cpp
    resources.qrc
)

# Link Qt6 and libhivex
target_link_libraries(BCDScribe PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets hivex)

install(TARGETS BCDScribe
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(FILES BCDScribe.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
)
install(FILES packaging/BCDScribe.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
)

if(GIT_FOUND)
    add_custom_target(git_update ALL
        COMMAND ${CMAKE_COMMAND} -E env
            BCDSCRIBE_REPOSITORY_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/update-git.sh
        VERBATIM
        COMMENT "Committing and pushing repository changes when origin is configured"
    )
    add_dependencies(git_update BCDScribe)
endif()