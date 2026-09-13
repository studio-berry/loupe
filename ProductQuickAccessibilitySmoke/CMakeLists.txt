qt_policy(SET QTP0001 NEW)

qt_add_executable(ProductQuickAccessibilitySmoke
    main.cpp
    ../LoopEditor/editorhost.cpp
    ../LoopEditor/editorhost.h
    ../LoopEditor/focusrestoration.cpp
    ../LoopEditor/focusrestoration.h
    ../LoopEditor/quickdocumentmodel.cpp
    ../LoopEditor/quickdocumentmodel.h
    ../LoopEditor/documentviewsession.cpp
    ../LoopEditor/documentviewsession.h
)

# The QML sources below intentionally mirror LoopEditor/qml/ file-for-file.
# QML_FILES must stay inside this directory: a `..` entry makes Qt derive a
# qmlcache path containing `..` (ProductQuickAccessibilitySmoke_.._LoopEditor),
# which MSBuild rejects with MSB3191 and which also misplaces the embedded
# resource (qrc:/qt/qml/Loop/LoopEditor/qml/Main.qml instead of
# qrc:/qt/qml/Loop/Quick/qml/Main.qml, so loadFromModule("Loop.Quick", "Main")
# fails). LoopEditor/qml/ remains the single source of truth; keep every
# mirror byte-identical when either side changes.
qt_add_qml_module(ProductQuickAccessibilitySmoke
    URI Loop.Quick
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/Workspace.qml
        qml/PreflightPane.qml
        qml/InspectorPane.qml
        qml/DocumentPane.qml
        qml/CanvasPane.qml
        qml/WorkspacePlaceholderPane.qml
        qml/ShellToolBar.qml
        qml/ShellMenuBar.qml
        qml/MenuModel.qml
)

target_link_libraries(ProductQuickAccessibilitySmoke
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

target_include_directories(ProductQuickAccessibilitySmoke PRIVATE
    ${CMAKE_SOURCE_DIR}/LoopEditor
)

qt_import_qml_plugins(ProductQuickAccessibilitySmoke)

set_target_properties(ProductQuickAccessibilitySmoke PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
)
