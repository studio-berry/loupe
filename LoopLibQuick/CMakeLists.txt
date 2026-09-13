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

# The Qt Quick presentation host for the canvas (ADR-010 / Phase 4 P4-S5).
#
# This is the only target in Phase 4 that links Qt6::Quick. LoopLibInteraction
# stays host-neutral and links neither Qml nor Quick; everything that needs a
# scene graph lives here, which is why issue #140's trace overlay and present
# timing could not be delivered in P4-S4 and are delivered here.
#
# SHARED, because qt_add_qml_module gives a QML module a backing library and a
# plugin, and a QML type registered from a static archive depends on the linker
# keeping an initializer nothing references. LoopLibInteraction is STATIC and
# is compiled POSITION_INDEPENDENT_CODE for exactly this link edge: a non-PIC
# static archive cannot be linked into a shared object on Linux.
#
# Deliberately not installed until P4-S7 shipped the product Quick shell.
# LoopLibQuick now installs with LoopEditor; see LoopEditor/CMakeLists.txt.
#
# ADR-009 as amended admits the direct QQuickItem and prohibits QQuickPaintedItem,
# QQuickWidget and WindowContainer as shipped product architecture. There is no
# Qt6::QuickWidgets link here and there must never be one.

qt_policy(SET QTP0001 NEW)

qt_add_library(LoopLibQuick SHARED
    sources/loopquickglobal.h
    sources/canvaspalette.cpp
    sources/canvaspalette.h
    sources/canvasnodebuilder.cpp
    sources/canvasnodebuilder.h
    sources/canvaspresentmetrics.cpp
    sources/canvaspresentmetrics.h
    sources/canvastraceoverlay.cpp
    sources/canvastraceoverlay.h
    sources/loopcanvasitem.cpp
    sources/loopcanvasitemscene.cpp
    sources/loopcanvasaccessible.cpp
    sources/loopcanvasaccessible.h
    sources/looptokens.cpp
    sources/looptokens.h
    sources/loopstatevisual.cpp
    sources/loopstatevisual.h
)

# No QML_FILES. LoopCanvasItem is registered from C++ with QML_NAMED_ELEMENT,
# so the module ships a type and no .qml of its own. That is not incidental:
# scripts/verify-quick-shell-policy.py checks every .qml in the repository
# against a fixed import allowlist, and a first-party .qml importing this module
# would have to widen that allowlist to admit itself.
qt_add_qml_module(LoopLibQuick
    URI Loop.Canvas
    VERSION 1.0
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/Loop/Canvas"
    DEPENDENCIES QtQuick
)

# Qt 6.11's generated qmltyperegistrations unit registers LoopCanvasItem and its
# QQuickItem base but does not include either header. Force the canvas header into
# that translation unit only; it already pulls in QQuickItem.
set(_loop_qml_type_registrations "${CMAKE_CURRENT_BINARY_DIR}/looplibquick_qmltyperegistrations.cpp")
if(MSVC)
    set(_loop_qml_reg_compile_options "/FI${CMAKE_CURRENT_SOURCE_DIR}/sources/loopcanvasitem.h")
else()
    set(_loop_qml_reg_compile_options "-include;${CMAKE_CURRENT_SOURCE_DIR}/sources/loopcanvasitem.h")
endif()
set_source_files_properties("${_loop_qml_type_registrations}" PROPERTIES
    GENERATED TRUE
    COMPILE_OPTIONS "${_loop_qml_reg_compile_options}"
)

include(GenerateExportHeader)

GENERATE_EXPORT_HEADER(LoopLibQuick
                       EXPORT_MACRO_NAME
                       LOOPLIBQUICK_EXPORT
                       EXPORT_FILE_NAME "${CMAKE_BINARY_DIR}/${INSTALL_INCLUDEDIR}/looplibquick_export.h")

# LoopLibCore is linked directly rather than inherited: LoopLibInteraction
# links it PRIVATE, so its include directories do not propagate here, and this
# target compiles LoopLibInteraction headers that include pdf:: types. The
# interaction unit tests link the same pair for the same reason.
target_link_libraries(LoopLibQuick
    PRIVATE
        LoopLibInteraction
        LoopLibCore
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
)

target_include_directories(LoopLibQuick PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/sources)
target_include_directories(LoopLibQuick PUBLIC ${CMAKE_BINARY_DIR}/${INSTALL_INCLUDEDIR})

set_target_properties(LoopLibQuick PROPERTIES
                      VERSION ${LOOP_VERSION}
                      SOVERSION ${LOOP_VERSION}
                      LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
                      RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR})

install(TARGETS LoopLibQuick
    RUNTIME DESTINATION ${LOOP_INSTALL_BIN_DIR}
    LIBRARY DESTINATION ${LOOP_INSTALL_LIB_DIR}
    ARCHIVE DESTINATION ${LOOP_INSTALL_LIB_DIR}
)
