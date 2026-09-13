# MIT License
#
# Copyright (c) 2018-2025 Jakub Melka and Contributors
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

qt_policy(SET QTP0001 NEW)

qt_add_executable(LoopEditor
    main.cpp
    icon.rc
    app.qrc
    documentviewsession.cpp
    documentviewsession.h
    editorhost.cpp
    editorhost.h
    focusrestoration.cpp
    focusrestoration.h
    quickdocumentmodel.cpp
    quickdocumentmodel.h
)

qt_add_qml_module(LoopEditor
    URI Loop.Quick
    VERSION 1.0
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/Loop/Quick"
    QML_FILES
        qml/Main.qml
        qml/Workspace.qml
        qml/DocumentPane.qml
        qml/CanvasPane.qml
        qml/PreflightPane.qml
        qml/InspectorPane.qml
        qml/WorkspacePlaceholderPane.qml
        qml/ShellToolBar.qml
        qml/ShellMenuBar.qml
        qml/MenuModel.qml
)

target_link_libraries(LoopEditor
    PRIVATE
        LoopLibQuick
        LoopLibInteraction
        LoopLibCore
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::QuickDialogs2
)

target_include_directories(LoopEditor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

qt_import_qml_plugins(LoopEditor)

# Packaging builds LoopEditor explicitly; ensure the Loop.Canvas plugin module is
# built before install copies it into usr/lib/qml/Loop/Canvas.
add_dependencies(LoopEditor LoopLibQuickplugin)

# Headless packaging smoke runs LoopEditor with QT_QPA_PLATFORM=offscreen. A
# WINDOWS-subsystem binary crashes there with 0xC0000005 before Quick startup;
# PdfTool and ProductQuickAccessibilitySmoke already ship as console binaries.
set(_loop_editor_win32_executable ON)
if(WIN32 AND LOOP_LOOP_DISTRIBUTION)
    set(_loop_editor_win32_executable OFF)
endif()

if(LOOP_LOOP_DISTRIBUTION)
    target_compile_definitions(LoopEditor PRIVATE LOOP_LOOP_DISTRIBUTION_BUILD=1)
endif()

set_target_properties(LoopEditor PROPERTIES
    WIN32_EXECUTABLE ${_loop_editor_win32_executable}
    MACOSX_BUNDLE ON
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
)

install(TARGETS LoopEditor
    RUNTIME DESTINATION ${LOOP_INSTALL_BIN_DIR}
    LIBRARY DESTINATION ${LOOP_INSTALL_LIB_DIR}
    ARCHIVE DESTINATION ${LOOP_INSTALL_LIB_DIR}
)

# LoopEditor owns the Loop.Quick module, so Qt can scan its QML import graph
# and install the complete runtime closure beside the product.  In particular,
# this covers transitive controls imports and the dynamically loaded platform
# and SQLite plugins used by the clean-tree smoke.
qt_generate_deploy_qml_app_script(
    TARGET LoopEditor
    OUTPUT_SCRIPT loop_editor_deploy_script
    NO_TRANSLATIONS
    INCLUDE_PLUGIN_TYPES platforms
    INCLUDE_PLUGINS qoffscreen qsqlite
)
# Qt's deployment helpers default to a flat prefix (bin/, lib/, qml/).  Loop
# installs product payloads below usr/, so establish the same prefix-relative
# layout before the generated script computes imports, plugins, and qt.conf.
#
# LOOP_INSTALL_QT_DEPENDENCIES stays authoritative for whether Loop ships its
# own Qt closure at all: the Flatpak manifest, the Dockerfile, the fuzz job and
# the reusable Linux qualification build all configure with it OFF because the
# runtime Qt comes from the platform there.
if(LOOP_INSTALL_QT_DEPENDENCIES)
    install(CODE [[
        set(QT_DEPLOY_BIN_DIR "usr/bin")
        set(QT_DEPLOY_LIB_DIR "usr/lib")
        set(QT_DEPLOY_QML_DIR "usr/lib/qml")
    ]])
    install(SCRIPT ${loop_editor_deploy_script})
    # Packaged Quick smoke resolves Loop.Canvas from the import tree after CI strips
    # developer Qt env vars. install(FILES) copies the plugin without registering an
    # install(TARGETS) entry that Phase 5 would treat as an unmanifested product target.
    install(FILES
        "$<TARGET_FILE:LoopLibQuickplugin>"
        "${CMAKE_BINARY_DIR}/LoopLibQuick/Loop/Canvas/qmldir"
        "${CMAKE_BINARY_DIR}/LoopLibQuick/Loop/Canvas/LoopLibQuick.qmltypes"
        DESTINATION usr/lib/qml/Loop/Canvas
    )
    if(WIN32)
        # Relocated Windows smoke strips developer Qt env vars. windeployqt stages
        # qoffscreen under install-root plugins/, but qt.conf omits Plugins= and
        # PdfTool already copies qoffscreen beside the executable for headless QPA.
        install(CODE [[
            set(_offscreen "${CMAKE_INSTALL_PREFIX}/plugins/platforms/qoffscreen.dll")
            set(_platforms_dir "${CMAKE_INSTALL_PREFIX}/usr/bin/platforms")
            if(EXISTS "${_offscreen}")
                file(MAKE_DIRECTORY "${_platforms_dir}")
                file(COPY "${_offscreen}" DESTINATION "${_platforms_dir}")
            endif()
            set(_qt_conf "${CMAKE_INSTALL_PREFIX}/usr/bin/qt.conf")
            if(EXISTS "${_qt_conf}")
                file(READ "${_qt_conf}" _qt_conf_contents)
                if(NOT _qt_conf_contents MATCHES "[Pp]lugins")
                    file(APPEND "${_qt_conf}" "Plugins = plugins\n")
                endif()
            endif()
        ]])
        install(DIRECTORY "${CMAKE_SOURCE_DIR}/LoopLibCore/liberation-fonts-ttf/"
            DESTINATION usr/lib/fonts
            FILES_MATCHING PATTERN "*.ttf")
    endif()
endif()

if(LOOP_ENABLE_SENTRY)
    loop_deploy_sentry_crashpad(LoopEditor)
endif()
