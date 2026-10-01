cmake_minimum_required(VERSION 3.16)
project(BCDScribe CXX)

include(GNUInstallDirs)
find_package(Git QUIET)
find_program(GH_EXECUTABLE gh)

set(BCDSCRIBE_GITHUB_REPOSITORY "UsefulVideos/BCDScribe")
if(GH_EXECUTABLE)
    execute_process(
        COMMAND ${GH_EXECUTABLE} api user --jq .login
        RESULT_VARIABLE GH_USER_RESULT
        OUTPUT_VARIABLE GH_USER
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(GH_USER_RESULT EQUAL 0 AND GH_USER AND NOT GH_USER STREQUAL "UsefulVideos")
        set(BCDSCRIBE_GITHUB_REPOSITORY "${GH_USER}/BCDScribe")
    endif()
endif()

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
target_compile_definitions(BCDScribe PRIVATE
    BCDSCRIBE_GITHUB_REPOSITORY="${BCDSCRIBE_GITHUB_REPOSITORY}"
)

install(TARGETS BCDScribe
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(FILES BCDScribe.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
)
install(FILES packaging/BCDScribe.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND
   CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
    add_custom_target(build_appimage ALL
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/build-appimage.sh
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        VERBATIM
        COMMENT "Building BCDScribe AppImage"
    )
    add_dependencies(build_appimage BCDScribe)
endif()

if(GIT_FOUND)
    add_custom_target(git_update ALL
        COMMAND ${CMAKE_COMMAND} -E env
            BCDSCRIBE_REPOSITORY_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/update-git.sh
        VERBATIM
        COMMENT "Committing and pushing repository changes when origin is configured"
    )
    add_dependencies(git_update BCDScribe)
    if(TARGET build_appimage)
        add_dependencies(git_update build_appimage)

        add_custom_target(github_release ALL
            COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/create-release.sh
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            VERBATIM
            COMMENT "Publishing a GitHub release when main has unreleased commits"
        )
        add_dependencies(github_release git_update)
    endif()
endif()