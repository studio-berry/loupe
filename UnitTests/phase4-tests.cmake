# Architecture invariant I21. The absent Qt6::Widgets link is load-bearing: Qt
# scopes its headers per module, so a Widgets or QML include leaking into a
# public LoopLibInteraction header breaks this target's compile.
if(NOT LOOP_BUILD_ONLY_CORE_LIBRARY)
    add_executable(UnitTestsInteractionBoundary
        tst_interactionboundarytest.cpp
    )

    target_link_libraries(UnitTestsInteractionBoundary PRIVATE LoopLibInteraction LoopLibCore Qt6::Core Qt6::Gui Qt6::Test)

    set_target_properties(UnitTestsInteractionBoundary PROPERTIES
        WIN32_EXECUTABLE OFF
        MACOSX_BUNDLE OFF
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
    )
    add_test(UnitTestsInteractionBoundary "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsInteractionBoundary")

    # Architecture invariant I22. Same load-bearing link line: the document
    # lifecycle and the command path must be drivable with no QWidget and no QML
    # engine, which is the P4-S2 exit condition.
    add_executable(UnitTestsDocumentFacade
        tst_documentfacadetest.cpp
    )

    target_link_libraries(UnitTestsDocumentFacade PRIVATE LoopLibInteraction LoopLibCore Qt6::Core Qt6::Gui Qt6::Test)

    set_target_properties(UnitTestsDocumentFacade PROPERTIES
        WIN32_EXECUTABLE OFF
        MACOSX_BUNDLE OFF
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
    )
    add_test(UnitTestsDocumentFacade "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsDocumentFacade")

    # Architecture invariant I23, viewport half. Same load-bearing link line: the
    # viewport, its layout and its transforms must be drivable with no QWidget,
    # no QScreen and no scrollbar, which is half of the P4-S3 exit condition.
    add_executable(UnitTestsViewportController
        tst_viewportcontrollertest.cpp
    )

    target_link_libraries(UnitTestsViewportController PRIVATE LoopLibInteraction LoopLibCore Qt6::Core Qt6::Gui Qt6::Test)

    set_target_properties(UnitTestsViewportController PROPERTIES
        WIN32_EXECUTABLE OFF
        MACOSX_BUNDLE OFF
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
    )
    add_test(UnitTestsViewportController "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsViewportController")

    add_executable(UnitTestsViewportCommands
        tst_viewportcommandbridgetest.cpp
    )

    target_link_libraries(UnitTestsViewportCommands PRIVATE LoopLibInteraction LoopLibCore Qt6::Core Qt6::Gui Qt6::Test)

    set_target_properties(UnitTestsViewportCommands PROPERTIES
        WIN32_EXECUTABLE OFF
        MACOSX_BUNDLE OFF
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
    )
    add_test(UnitTestsViewportCommands "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsViewportCommands")

    if(LOOP_BUILD_QUICK_CANVAS)
        add_executable(UnitTestsEditorHost
            tst_editorhosttest.cpp
            ${CMAKE_SOURCE_DIR}/LoopEditor/app.qrc
            ${CMAKE_SOURCE_DIR}/LoopEditor/editorhost.cpp
            ${CMAKE_SOURCE_DIR}/LoopEditor/editorhost.h
            ${CMAKE_SOURCE_DIR}/LoopEditor/documentviewsession.cpp
            ${CMAKE_SOURCE_DIR}/LoopEditor/documentviewsession.h
            ${CMAKE_SOURCE_DIR}/LoopEditor/quickdocumentmodel.cpp
            ${CMAKE_SOURCE_DIR}/LoopEditor/quickdocumentmodel.h
            ${CMAKE_SOURCE_DIR}/LoopEditor/focusrestoration.cpp
            ${CMAKE_SOURCE_DIR}/LoopEditor/focusrestoration.h
        )

        target_link_libraries(UnitTestsEditorHost PRIVATE LoopLibQuick LoopLibInteraction LoopLibCore Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Test)

        target_include_directories(UnitTestsEditorHost PRIVATE ${CMAKE_SOURCE_DIR}/LoopEditor)

        # Issue #195's GUI-to-CLI anti-divergence slot drives the built PdfTool
        # as the CLI oracle, the same way UnitTestsPreflightCorpus does, and
        # locates its fixture through the same source-dir definition.
        add_dependencies(UnitTestsEditorHost PdfTool)
        target_compile_definitions(UnitTestsEditorHost PRIVATE
            LOOP_PREFLIGHT_SOURCE_DIR="${CMAKE_SOURCE_DIR}/loop-preflight"
            PDFTOOL_EXECUTABLE_PATH="$<TARGET_FILE:PdfTool>"
        )

        set_target_properties(UnitTestsEditorHost PROPERTIES
            WIN32_EXECUTABLE OFF
            MACOSX_BUNDLE OFF
            LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
        )
        add_test(UnitTestsEditorHost "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsEditorHost")
        set_tests_properties(UnitTestsEditorHost PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

        add_executable(UnitTestsProductOperatorLoop
            tst_productoperatorloop.cpp
            ${CMAKE_SOURCE_DIR}/LoopEditor/editorhost.cpp
            ${CMAKE_SOURCE_DIR}/LoopEditor/editorhost.h
            ${CMAKE_SOURCE_DIR}/LoopEditor/documentviewsession.cpp
            ${CMAKE_SOURCE_DIR}/LoopEditor/documentviewsession.h
            ${CMAKE_SOURCE_DIR}/LoopEditor/quickdocumentmodel.cpp
            ${CMAKE_SOURCE_DIR}/LoopEditor/quickdocumentmodel.h
            ${CMAKE_SOURCE_DIR}/LoopEditor/focusrestoration.cpp
            ${CMAKE_SOURCE_DIR}/LoopEditor/focusrestoration.h
        )

        target_link_libraries(UnitTestsProductOperatorLoop PRIVATE LoopLibQuick LoopLibInteraction LoopLibCore Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Test)

        target_include_directories(UnitTestsProductOperatorLoop PRIVATE ${CMAKE_SOURCE_DIR}/LoopEditor)

        target_compile_definitions(UnitTestsProductOperatorLoop PRIVATE
            LOOP_UNITTEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
            LOOP_PREFLIGHT_SOURCE_DIR="${CMAKE_SOURCE_DIR}/loop-preflight"
        )

        set_target_properties(UnitTestsProductOperatorLoop PROPERTIES
            WIN32_EXECUTABLE OFF
            MACOSX_BUNDLE OFF
            LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
        )
        add_test(UnitTestsProductOperatorLoop "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsProductOperatorLoop")
        set_tests_properties(UnitTestsProductOperatorLoop PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    endif()

    # Architecture invariant I23, admission half: one render-request path through
    # pdf::PDFJobScheduler, and full-key admission on the owner thread.
    add_executable(UnitTestsPageSurface
        tst_pagesurfacetest.cpp
    )

    target_link_libraries(UnitTestsPageSurface PRIVATE LoopLibInteraction LoopLibCore Qt6::Core Qt6::Gui Qt6::Test)
    target_compile_definitions(UnitTestsPageSurface PRIVATE
        LOOP_PREFLIGHT_SOURCE_DIR="${CMAKE_SOURCE_DIR}/loop-preflight")

    set_target_properties(UnitTestsPageSurface PROPERTIES
        WIN32_EXECUTABLE OFF
        MACOSX_BUNDLE OFF
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
    )
    add_test(UnitTestsPageSurface "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsPageSurface")

    add_executable(UnitTestsPageSurfaceBudget
        tst_pagesurfacebudgettest.cpp
    )

    target_link_libraries(UnitTestsPageSurfaceBudget PRIVATE LoopLibInteraction LoopLibCore Qt6::Core Qt6::Gui Qt6::Test)

    set_target_properties(UnitTestsPageSurfaceBudget PROPERTIES
        WIN32_EXECUTABLE OFF
        MACOSX_BUNDLE OFF
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
    )
    add_test(UnitTestsPageSurfaceBudget "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsPageSurfaceBudget")

    add_executable(UnitTestsDocumentViewSession
        tst_documentviewsessiontest.cpp
        ${CMAKE_SOURCE_DIR}/LoopEditor/documentviewsession.cpp
        ${CMAKE_SOURCE_DIR}/LoopEditor/documentviewsession.h
    )

    target_link_libraries(UnitTestsDocumentViewSession PRIVATE LoopLibInteraction LoopLibCore Qt6::Core Qt6::Gui Qt6::Test)

    target_include_directories(UnitTestsDocumentViewSession PRIVATE ${CMAKE_SOURCE_DIR}/LoopEditor)

    set_target_properties(UnitTestsDocumentViewSession PROPERTIES
        WIN32_EXECUTABLE OFF
        MACOSX_BUNDLE OFF
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
    )
    add_test(UnitTestsDocumentViewSession "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsDocumentViewSession")

    # Architecture invariant I24, interaction half. Same load-bearing link line:
    # transient state, hit testing, cancellation and the interaction trace must be
    # drivable with no QWidget, no QML engine and no event loop, which is half of
    # the P4-S4 exit condition.
    add_executable(UnitTestsInteractionController
        tst_interactioncontrollertest.cpp
    )

    target_link_libraries(UnitTestsInteractionController PRIVATE LoopLibInteraction LoopLibCore Qt6::Core Qt6::Gui Qt6::Test)

    set_target_properties(UnitTestsInteractionController PROPERTIES
        WIN32_EXECUTABLE OFF
        MACOSX_BUNDLE OFF
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
    )
    add_test(UnitTestsInteractionController "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsInteractionController")

    # Issue #146 AC7: the fixed-order contract evaluator a trace harness folds
    # its recorded outcomes into. No InteractionController, no replay, no
    # scheduler -- just the ordered checklist and the phase-attribution table
    # from docs/INTERACTION_CONTRACT.md.
    add_executable(UnitTestsInteractionTraceContract
        tst_interactiontracecontracttest.cpp
    )

    target_link_libraries(UnitTestsInteractionTraceContract PRIVATE LoopLibInteraction LoopLibCore Qt6::Core Qt6::Gui Qt6::Test)

    set_target_properties(UnitTestsInteractionTraceContract PROPERTIES
        WIN32_EXECUTABLE OFF
        MACOSX_BUNDLE OFF
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
    )
    add_test(UnitTestsInteractionTraceContract "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsInteractionTraceContract")

    # Issue #145: the spatial index used by EvidenceHitTestSource and
    # FindingListHitTestSource, and their hit-testing/precedence contracts
    # once queries are index-backed instead of a linear scan.
    add_executable(UnitTestsHitTestSource
        tst_hittestsourcetest.cpp
    )

    target_link_libraries(UnitTestsHitTestSource PRIVATE LoopLibInteraction LoopLibCore Qt6::Core Qt6::Gui Qt6::Test)

    set_target_properties(UnitTestsHitTestSource PROPERTIES
        WIN32_EXECUTABLE OFF
        MACOSX_BUNDLE OFF
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
    )
    add_test(UnitTestsHitTestSource "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsHitTestSource")

    # Architecture invariant I24, overlay half: deterministic z-order, page-space
    # geometry aligned with the page surfaces, and invalidation independent of
    # them.
    add_executable(UnitTestsOverlayFrame
        tst_overlayframetest.cpp
    )

    target_link_libraries(UnitTestsOverlayFrame PRIVATE LoopLibInteraction LoopLibCore Qt6::Core Qt6::Gui Qt6::Test)

    set_target_properties(UnitTestsOverlayFrame PROPERTIES
        WIN32_EXECUTABLE OFF
        MACOSX_BUNDLE OFF
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
    )
    add_test(UnitTestsOverlayFrame "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsOverlayFrame")

    add_executable(UnitTestsP4S9Interaction
        tst_p4s9interaction.cpp
    )

    target_link_libraries(UnitTestsP4S9Interaction PRIVATE LoopLibInteraction LoopLibCore Qt6::Core Qt6::Gui Qt6::Test)

    set_target_properties(UnitTestsP4S9Interaction PROPERTIES
        WIN32_EXECUTABLE OFF
        MACOSX_BUNDLE OFF
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
    )
    add_test(UnitTestsP4S9Interaction "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsP4S9Interaction")

    if(LOOP_BUILD_QUICK_CANVAS)
        add_executable(UnitTestsQuickDocumentModel
            tst_quickdocumentmodeltest.cpp
            ${CMAKE_SOURCE_DIR}/LoopEditor/quickdocumentmodel.cpp
            ${CMAKE_SOURCE_DIR}/LoopEditor/quickdocumentmodel.h
        )

        target_link_libraries(UnitTestsQuickDocumentModel PRIVATE LoopLibCore Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Test)

        target_include_directories(UnitTestsQuickDocumentModel PRIVATE ${CMAKE_SOURCE_DIR}/LoopEditor)

        set_target_properties(UnitTestsQuickDocumentModel PROPERTIES
            WIN32_EXECUTABLE OFF
            MACOSX_BUNDLE OFF
            LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
        )
        add_test(UnitTestsQuickDocumentModel "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsQuickDocumentModel")
        set_tests_properties(UnitTestsQuickDocumentModel PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

        add_executable(UnitTestsQuickAccessibility
            tst_quickaccessibilitytest.cpp
            ${CMAKE_SOURCE_DIR}/LoopEditor/focusrestoration.cpp
            ${CMAKE_SOURCE_DIR}/LoopEditor/focusrestoration.h
        )

        target_link_libraries(UnitTestsQuickAccessibility PRIVATE LoopLibQuick LoopLibInteraction LoopLibCore Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Test)

        target_include_directories(UnitTestsQuickAccessibility PRIVATE ${CMAKE_SOURCE_DIR}/LoopEditor)

        set_target_properties(UnitTestsQuickAccessibility PROPERTIES
            WIN32_EXECUTABLE OFF
            MACOSX_BUNDLE OFF
            LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
        )
        add_test(UnitTestsQuickAccessibility "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsQuickAccessibility")
        set_tests_properties(UnitTestsQuickAccessibility PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

        add_executable(UnitTestsShellKeyboard
            tst_shellkeyboardtest.cpp
            ${CMAKE_SOURCE_DIR}/LoopEditor/editorhost.cpp
            ${CMAKE_SOURCE_DIR}/LoopEditor/editorhost.h
            ${CMAKE_SOURCE_DIR}/LoopEditor/documentviewsession.cpp
            ${CMAKE_SOURCE_DIR}/LoopEditor/documentviewsession.h
            ${CMAKE_SOURCE_DIR}/LoopEditor/quickdocumentmodel.cpp
            ${CMAKE_SOURCE_DIR}/LoopEditor/quickdocumentmodel.h
            ${CMAKE_SOURCE_DIR}/LoopEditor/focusrestoration.cpp
            ${CMAKE_SOURCE_DIR}/LoopEditor/focusrestoration.h
        )

        target_link_libraries(UnitTestsShellKeyboard PRIVATE LoopLibQuick LoopLibInteraction LoopLibCore Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Test)

        target_include_directories(UnitTestsShellKeyboard PRIVATE ${CMAKE_SOURCE_DIR}/LoopEditor)

        set_target_properties(UnitTestsShellKeyboard PROPERTIES
            WIN32_EXECUTABLE OFF
            MACOSX_BUNDLE OFF
            LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
        )
        add_test(UnitTestsShellKeyboard "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsShellKeyboard")

        add_executable(UnitTestsShellWorkspace
            tst_shellworkspacetest.cpp
            ${CMAKE_SOURCE_DIR}/LoopEditor/editorhost.cpp
            ${CMAKE_SOURCE_DIR}/LoopEditor/editorhost.h
            ${CMAKE_SOURCE_DIR}/LoopEditor/documentviewsession.cpp
            ${CMAKE_SOURCE_DIR}/LoopEditor/documentviewsession.h
            ${CMAKE_SOURCE_DIR}/LoopEditor/quickdocumentmodel.cpp
            ${CMAKE_SOURCE_DIR}/LoopEditor/quickdocumentmodel.h
            ${CMAKE_SOURCE_DIR}/LoopEditor/focusrestoration.cpp
            ${CMAKE_SOURCE_DIR}/LoopEditor/focusrestoration.h
        )

        target_link_libraries(UnitTestsShellWorkspace PRIVATE LoopLibQuick LoopLibInteraction LoopLibCore Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Test)

        target_include_directories(UnitTestsShellWorkspace PRIVATE ${CMAKE_SOURCE_DIR}/LoopEditor)

        set_target_properties(UnitTestsShellWorkspace PROPERTIES
            WIN32_EXECUTABLE OFF
            MACOSX_BUNDLE OFF
            LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
        )
        add_test(UnitTestsShellWorkspace "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsShellWorkspace")

        add_executable(UnitTestsShellInspectorDispatch
            tst_shellinspectordispatch.cpp
            ${CMAKE_SOURCE_DIR}/LoopEditor/editorhost.cpp
            ${CMAKE_SOURCE_DIR}/LoopEditor/editorhost.h
            ${CMAKE_SOURCE_DIR}/LoopEditor/documentviewsession.cpp
            ${CMAKE_SOURCE_DIR}/LoopEditor/documentviewsession.h
            ${CMAKE_SOURCE_DIR}/LoopEditor/quickdocumentmodel.cpp
            ${CMAKE_SOURCE_DIR}/LoopEditor/quickdocumentmodel.h
            ${CMAKE_SOURCE_DIR}/LoopEditor/focusrestoration.cpp
            ${CMAKE_SOURCE_DIR}/LoopEditor/focusrestoration.h
        )

        target_link_libraries(UnitTestsShellInspectorDispatch PRIVATE LoopLibQuick LoopLibInteraction LoopLibCore Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Test)

        target_include_directories(UnitTestsShellInspectorDispatch PRIVATE ${CMAKE_SOURCE_DIR}/LoopEditor)

        set_target_properties(UnitTestsShellInspectorDispatch PROPERTIES
            WIN32_EXECUTABLE OFF
            MACOSX_BUNDLE OFF
            LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
        )
        add_test(UnitTestsShellInspectorDispatch "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsShellInspectorDispatch")
    endif()

    # Architecture invariant I25. The inverse of the five targets above: this one
    # links LoopLibQuick and therefore Qt6::Quick, because it is the admitted
    # presentation host and has to be able to name a QQuickItem. What it pins is
    # that the host stayed presentation -- Quick events in, neutral intents out,
    # neutral values in, scene-graph geometry out -- and that issue #140's trace
    # overlay reports no document payload.
    #
    # Qt6::Widgets is still absent, and still deliberately: ADR-009 as amended
    # prohibits QQuickWidget and WindowContainer as product architecture, and a
    # Widgets link here is what would make either reachable.
    if(LOOP_BUILD_QUICK_CANVAS)
        add_executable(UnitTestsQuickCanvas
            tst_quickcanvastest.cpp
            quickcanvastestfakes.h
        )

        target_link_libraries(UnitTestsQuickCanvas PRIVATE LoopLibQuick LoopLibInteraction LoopLibCore Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Test)

        set_target_properties(UnitTestsQuickCanvas PROPERTIES
            WIN32_EXECUTABLE OFF
            MACOSX_BUNDLE OFF
            LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
        )
        add_test(UnitTestsQuickCanvas "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsQuickCanvas")

        # The test constructs QQuickItems and needs a QGuiApplication, but never a
        # visible window. GitHub's Linux runners have no display at all.
        #
        # QT_QUICK_BACKEND=software joined it in P4-S6, when the scene-graph
        # lifecycle cases arrived. Those cases need a scene graph that actually
        # renders -- releaseResources, scene-graph invalidation and texture
        # retention have no observable behaviour without one -- and the software
        # backend is the one that renders the same way on a GPU-less runner as on
        # a developer machine. ADR-010 is right that offscreen is not by itself
        # scene-graph evidence; the native and software smoke runs are that. This
        # is what makes node and texture lifetime deterministic.
        set_tests_properties(UnitTestsQuickCanvas PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")

        # Architecture invariant I26. This target carries the retained
        # Quick-native geometry, interaction, and presentation checks. Its
        # expected geometry is explicit in the test cases; it has no dependency
        # on the retired Widgets host or a second rendering implementation.
        add_executable(UnitTestsCanvasParity
            tst_canvasparitytest.cpp
        )

        target_link_libraries(UnitTestsCanvasParity PRIVATE LoopLibQuick LoopLibInteraction LoopLibCore Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Test)

        target_compile_definitions(UnitTestsCanvasParity PRIVATE
            LOOP_UNITTEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
        )

        set_target_properties(UnitTestsCanvasParity PROPERTIES
            WIN32_EXECUTABLE OFF
            MACOSX_BUNDLE OFF
            LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
        )
        add_test(UnitTestsCanvasParity "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsCanvasParity")

        set_tests_properties(UnitTestsCanvasParity PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    endif()
endif()
