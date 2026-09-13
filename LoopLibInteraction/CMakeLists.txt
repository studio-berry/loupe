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

# Host-neutral product interaction layer (ADR-010 / Phase 4 P4-S1).
#
# STATIC and deliberately not installed. A SHARED library with exported headers
# would become an accidental public ABI for a layer whose contents still move
# through P4-S2/S3/S4, so there is no GenerateExportHeader and no install rule
# here. Both are expected to stay absent until the Phase 4 exit gate.
#
# This target must never link Qt6::Widgets, Qt6::Qml, Qt6::Quick, or
# Qt6::QuickControls2. Qt scopes its headers per module, so not linking them is
# what makes a Widgets or QML include here a compile error rather than a review
# comment. scripts/verify-interaction-boundary.py enforces the same rule
# statically; architecture invariant I21 binds it to a test target.

add_library(LoopLibInteraction STATIC
    sources/interactionglobal.h
    sources/documentcontextsource.cpp
    sources/documentcontextsource.h
    sources/jobsubmitter.cpp
    sources/jobsubmitter.h
    sources/jobrelay.cpp
    sources/jobrelay.h
    sources/commanddescriptor.cpp
    sources/commanddescriptor.h
    sources/commandcatalog.cpp
    sources/commandcatalog.h
    sources/commandcatalogresource.cpp
    sources/commandcatalogresource_p.h
    sources/documentloader.cpp
    sources/documentloader.h
    sources/documentfacade.cpp
    sources/documentfacade.h
    sources/documentfacadestate.cpp
    sources/pagesurfacekey.cpp
    sources/pagesurfacekey.h
    sources/viewportcontroller.cpp
    sources/viewportcontroller.h
    sources/viewportcommandbridge.cpp
    sources/viewportcommandbridge.h
    sources/pagesurfacerenderer.cpp
    sources/pagesurfacerenderer.h
    sources/canvassnapshot.cpp
    sources/canvassnapshot.h
    sources/pagesurfacecoordinator.cpp
    sources/pagesurfacecoordinator.h
    sources/renderpresentationpolicy.h
    sources/inputintent.cpp
    sources/inputintent.h
    sources/interactiontarget.cpp
    sources/interactiontarget.h
    sources/interactionstate.cpp
    sources/interactionstate.h
    sources/hittestsource.cpp
    sources/hittestsource.h
    sources/pagespatialindex.cpp
    sources/pagespatialindex.h
    sources/overlayframe.cpp
    sources/overlayframe.h
    sources/overlaybuilder.cpp
    sources/overlaybuilder.h
    sources/interactiontrace.cpp
    sources/interactiontrace.h
    sources/interactiontracecontract.cpp
    sources/interactiontracecontract.h
    sources/interactioncontroller.cpp
    sources/interactioncontroller.h
    sources/preflightcontroller.cpp
    sources/preflightcontroller.h
    sources/preflightfindingsmodel.cpp
    sources/preflightfindingsmodel.h
    sources/preflightoverlaybridge.cpp
    sources/preflightoverlaybridge.h
    sources/inspectormodel.cpp
    sources/inspectormodel.h
    sources/previewstatemodel.cpp
    sources/previewstatemodel.h
    sources/pagesmodel.cpp
    sources/pagesmodel.h
    sources/productionmodel.cpp
    sources/productionmodel.h
)

target_link_libraries(LoopLibInteraction PRIVATE LoopLibCore Qt6::Core Qt6::Gui)

target_include_directories(LoopLibInteraction INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/sources)

# The command catalog reads docs/loop-shell-actions.json — the same file the
# Editor action policy verifier checks against pdfeditormainwindow.ui — so there
# is one command registry rather than a copy that can drift. It is embedded
# rather than read from disk: the neutral layer must not need a filesystem
# service to know what commands exist.
#
# qt_add_resources is used rather than listing a .qrc among the sources above.
# This target is STATIC, where an unreferenced AUTORCC initializer can be dropped
# by the linker and the catalog would silently load empty; the target-based
# signature propagates a resource init object to consumers instead.
# CommandCatalog reports command-catalog/resource-unavailable rather than
# starting with an empty command set if this ever regresses.
set(LOOP_INTERACTION_COMMAND_CONTRACT "${CMAKE_CURRENT_SOURCE_DIR}/../docs/loop-shell-actions.json")

set_source_files_properties(${LOOP_INTERACTION_COMMAND_CONTRACT}
    PROPERTIES QT_RESOURCE_ALIAS "loop-shell-actions.json"
)

qt_add_resources(LoopLibInteraction "commandcatalog"
    PREFIX "/loop"
    FILES ${LOOP_INTERACTION_COMMAND_CONTRACT}
)

# qt_add_resources keeps the qInitResources_* TU in a sibling target. For this
# STATIC archive that initializer must live in libLoopLibInteraction.a itself.
get_target_property(_loop_cmdcat_init_cpp LoopLibInteraction_resources_1 SOURCES)
if(_loop_cmdcat_init_cpp)
    target_sources(LoopLibInteraction PRIVATE ${_loop_cmdcat_init_cpp})
endif()
unset(_loop_cmdcat_init_cpp)

# LoopLibQuick is SHARED in P4-S5; a non-PIC static archive cannot be linked
# into a shared object on Linux.
set_target_properties(LoopLibInteraction PROPERTIES POSITION_INDEPENDENT_CODE ON)
