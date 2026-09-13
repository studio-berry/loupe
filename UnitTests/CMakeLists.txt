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

add_executable(UnitTests
	tst_lexicalanalyzertest.cpp
)

target_link_libraries(UnitTests PRIVATE LoopLibCore Qt6::Core Qt6::Gui Qt6::Test)

set_target_properties(UnitTests PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
)
add_test(UnitTests  "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTests")

add_executable(UnitTestsIncrementalSave
    tst_incrementalsavetest.cpp
)

target_link_libraries(UnitTestsIncrementalSave PRIVATE LoopLibCore Qt6::Core Qt6::Gui Qt6::Test)

set_target_properties(UnitTestsIncrementalSave PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
)
add_test(UnitTestsIncrementalSave "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsIncrementalSave")

add_executable(UnitTestsProcessingBudget
    tst_processingbudgettest.cpp
)

target_link_libraries(UnitTestsProcessingBudget PRIVATE LoopLibCore Qt6::Core Qt6::Gui Qt6::Test)

set_target_properties(UnitTestsProcessingBudget PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
)

add_test(UnitTestsProcessingBudget "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsProcessingBudget")

add_executable(UnitTestsJobScheduler
    tst_jobschedulertest.cpp
)

target_link_libraries(UnitTestsJobScheduler PRIVATE LoopLibCore Qt6::Core Qt6::Gui Qt6::Test)

set_target_properties(UnitTestsJobScheduler PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
)

add_test(UnitTestsJobScheduler "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsJobScheduler")

add_executable(UnitTestsBlockingThreadGuard
    tst_blockingthreadguardtest.cpp
)

target_link_libraries(UnitTestsBlockingThreadGuard PRIVATE LoopLibCore Qt6::Core Qt6::Gui Qt6::Test)

set_target_properties(UnitTestsBlockingThreadGuard PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
)

add_test(UnitTestsBlockingThreadGuard "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsBlockingThreadGuard")

add_executable(UnitTestsRevisionStress
    tst_revisionstresstest.cpp
)

target_link_libraries(UnitTestsRevisionStress PRIVATE LoopLibCore Qt6::Core Qt6::Gui Qt6::Test)

set_target_properties(UnitTestsRevisionStress PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
)

add_test(UnitTestsRevisionStress "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsRevisionStress")

include(${CMAKE_CURRENT_SOURCE_DIR}/phase4-tests.cmake)


add_executable(UnitTestsImageOptimizer
	tst_imageoptimizertest.cpp
)

target_link_libraries(UnitTestsImageOptimizer PRIVATE LoopLibCore Qt6::Core Qt6::Gui Qt6::Test)

set_target_properties(UnitTestsImageOptimizer PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
)
add_test(UnitTestsImageOptimizer  "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsImageOptimizer")

add_executable(UnitTestsImageDownsampleFixup
    tst_imagedownsamplefixuptest.cpp
)

target_link_libraries(UnitTestsImageDownsampleFixup PRIVATE LoopLibCore Qt6::Core Qt6::Gui Qt6::Test)

set_target_properties(UnitTestsImageDownsampleFixup PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
)
add_test(UnitTestsImageDownsampleFixup "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsImageDownsampleFixup")

add_executable(UnitTestsBleedFixup
	tst_bleedfixuptest.cpp
)

target_link_libraries(UnitTestsBleedFixup PRIVATE LoopLibCore Qt6::Core Qt6::Gui Qt6::Test)

add_executable(UnitTestsDocumentSession
	tst_documentsessiontest.cpp
)

target_link_libraries(UnitTestsDocumentSession PRIVATE LoopLibCore Qt6::Core Qt6::Gui Qt6::Test)

set_target_properties(UnitTestsDocumentSession PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
)


add_executable(UnitTestsFontEncoding
	tst_fontencodingtest.cpp
)

target_link_libraries(UnitTestsFontEncoding PRIVATE LoopLibCore Qt6::Core Qt6::Gui Qt6::Test)

set_target_properties(UnitTestsFontEncoding PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
)
add_test(UnitTestsFontEncoding  "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsFontEncoding")

add_executable(UnitTestsAuthorSettings
	tst_authorsettingstest.cpp
)

target_link_libraries(UnitTestsAuthorSettings PRIVATE LoopLibCore Qt6::Core Qt6::Gui Qt6::Test)

set_target_properties(UnitTestsAuthorSettings PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
)
add_test(UnitTestsAuthorSettings  "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsAuthorSettings")

add_executable(UnitTestsDiagnostics
	tst_diagnosticstest.cpp
)

target_link_libraries(UnitTestsDiagnostics PRIVATE LoopLibCore Qt6::Core Qt6::Gui Qt6::Test)
target_include_directories(UnitTestsDiagnostics PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}/UnitTestsDiagnostics_autogen/include)

set_target_properties(UnitTestsDiagnostics PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
)
# Test name is lower-case (unlike sibling targets) so it matches
# `ctest -R diagnostics` as documented in docs/PRODUCTION_RUNBOOK.md.
add_test(diagnostics  "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsDiagnostics")
# Alias matching the target name so agent-fast policy entry
# `UnitTestsDiagnostics` both builds and selects this test via
# `ctest -R ^UnitTestsDiagnostics$`. Keeps the documented `diagnostics`
# name intact while letting check-change.py (which uses one list for
# both `cmake --build --target <name>` and `ctest -R <name>`) cover it.
add_test(UnitTestsDiagnostics "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsDiagnostics")

add_executable(UnitTestsContentEditor
	tst_contenteditortest.cpp
)

target_link_libraries(UnitTestsContentEditor PRIVATE LoopLibCore Qt6::Core Qt6::Gui Qt6::Test)

set_target_properties(UnitTestsContentEditor PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
)
add_test(UnitTestsContentEditor  "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsContentEditor")


add_test(UnitTestsDocumentSession "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsDocumentSession")

add_executable(UnitTestsRepairDiff
    tst_repairdifftest.cpp
)

target_link_libraries(UnitTestsRepairDiff PRIVATE LoopLibCore Qt6::Core Qt6::Gui Qt6::Test)

set_target_properties(UnitTestsRepairDiff PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
)

add_test(UnitTestsRepairDiff "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsRepairDiff")

add_executable(UnitTestsRepairOperation
    tst_repairoperationtest.cpp
)

target_link_libraries(UnitTestsRepairOperation PRIVATE LoopLibCore Qt6::Core Qt6::Gui Qt6::Test)

set_target_properties(UnitTestsRepairOperation PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
)

add_test(UnitTestsRepairOperation "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsRepairOperation")

add_executable(UnitTestsActionList
    tst_actionlisttest.cpp
)

target_link_libraries(UnitTestsActionList PRIVATE LoopLibCore Qt6::Core Qt6::Gui Qt6::Test)

set_target_properties(UnitTestsActionList PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
)

add_test(UnitTestsActionList "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsActionList")

add_executable(UnitTestsOperationHistory
    tst_operationhistorytest.cpp
)

target_link_libraries(UnitTestsOperationHistory PRIVATE LoopLibCore Qt6::Core Qt6::Gui Qt6::Sql Qt6::Test)

set_target_properties(UnitTestsOperationHistory PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
)

add_executable(UnitTestsProductionGeometry
    tst_productiongeometrytest.cpp
)

target_link_libraries(UnitTestsProductionGeometry PRIVATE LoopLibCore Qt6::Core Qt6::Gui Qt6::Test)

set_target_properties(UnitTestsProductionGeometry PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
)

add_test(UnitTestsOperationHistory "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsOperationHistory")
add_test(UnitTestsProductionGeometry "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsProductionGeometry")

add_executable(UnitTestsPreflightEngine
	tst_preflightenginetest.cpp
)

target_link_libraries(UnitTestsPreflightEngine PRIVATE LoopLibCore Qt6::Core Qt6::Gui Qt6::Test)
target_compile_definitions(UnitTestsPreflightEngine PRIVATE
    LOOP_PREFLIGHT_SOURCE_DIR="${CMAKE_SOURCE_DIR}/loop-preflight")

set_target_properties(UnitTestsPreflightEngine PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
)
add_test(UnitTestsPreflightEngine "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsPreflightEngine")

add_executable(UnitTestsPreflightVerdict
    tst_preflightverdicttest.cpp
)

target_link_libraries(UnitTestsPreflightVerdict PRIVATE LoopLibCore LoopLibInteraction Qt6::Core Qt6::Gui Qt6::Test)
target_include_directories(UnitTestsPreflightVerdict PRIVATE
    ${CMAKE_SOURCE_DIR}/LoopLibInteraction/sources)

set_target_properties(UnitTestsPreflightVerdict PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
)
add_test(UnitTestsPreflightVerdict "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsPreflightVerdict")

add_executable(UnitTestsPreflightInteraction
    tst_preflightinteraction.cpp
)

target_link_libraries(UnitTestsPreflightInteraction PRIVATE LoopLibCore LoopLibInteraction Qt6::Core Qt6::Gui Qt6::Test)
target_include_directories(UnitTestsPreflightInteraction PRIVATE
    ${CMAKE_SOURCE_DIR}/LoopLibInteraction/sources)

set_target_properties(UnitTestsPreflightInteraction PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
)
add_test(UnitTestsPreflightInteraction "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsPreflightInteraction")

add_executable(UnitTestsSchemaEvolution
    tst_schemaevolutiontest.cpp
)
target_link_libraries(UnitTestsSchemaEvolution PRIVATE LoopLibCore Qt6::Core Qt6::Gui Qt6::Test)
target_compile_definitions(UnitTestsSchemaEvolution PRIVATE
    LOOP_PREFLIGHT_SOURCE_DIR="${CMAKE_SOURCE_DIR}/loop-preflight")
set_target_properties(UnitTestsSchemaEvolution PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
)
add_test(UnitTestsSchemaEvolution "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsSchemaEvolution")

add_executable(UnitTestsEvidenceGraph
    tst_evidencegraphtest.cpp
)
target_link_libraries(UnitTestsEvidenceGraph PRIVATE LoopLibCore Qt6::Core Qt6::Gui Qt6::Test)
target_compile_definitions(UnitTestsEvidenceGraph PRIVATE
    LOOP_PREFLIGHT_SOURCE_DIR="${CMAKE_SOURCE_DIR}/loop-preflight")
set_target_properties(UnitTestsEvidenceGraph PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
)
add_test(UnitTestsEvidenceGraph "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsEvidenceGraph")

add_executable(UnitTestsProfileIdentity
    tst_preflightprofileidentitytest.cpp
)
target_link_libraries(UnitTestsProfileIdentity PRIVATE LoopLibCore Qt6::Core Qt6::Gui Qt6::Test)
target_compile_definitions(UnitTestsProfileIdentity PRIVATE
    LOOP_PREFLIGHT_SOURCE_DIR="${CMAKE_SOURCE_DIR}/loop-preflight"
    LOOP_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
set_target_properties(UnitTestsProfileIdentity PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
)
add_test(UnitTestsProfileIdentity "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsProfileIdentity")

add_executable(UnitTestsOperationImpact
    tst_operationimpacttest.cpp
)
target_link_libraries(UnitTestsOperationImpact PRIVATE LoopLibCore Qt6::Core Qt6::Gui Qt6::Test)
set_target_properties(UnitTestsOperationImpact PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
)
add_test(UnitTestsOperationImpact "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsOperationImpact")

add_executable(UnitTestsStandardOracle
    tst_standardoracletest.cpp
)
target_link_libraries(UnitTestsStandardOracle PRIVATE LoopLibCore Qt6::Core Qt6::Gui Qt6::Test)
set_target_properties(UnitTestsStandardOracle PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
)
add_test(UnitTestsStandardOracle "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsStandardOracle")

add_executable(UnitTestsPluginAbi
    tst_pluginabitest.cpp
)
target_link_libraries(UnitTestsPluginAbi PRIVATE LoopLibCore Qt6::Core Qt6::Gui Qt6::Test)
set_target_properties(UnitTestsPluginAbi PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
)
add_test(UnitTestsPluginAbi "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsPluginAbi")

add_executable(UnitTestsLifecycle
    tst_lifecycletest.cpp
)
target_link_libraries(UnitTestsLifecycle PRIVATE LoopLibCore Qt6::Core Qt6::Gui Qt6::Sql Qt6::Test)
target_compile_definitions(UnitTestsLifecycle PRIVATE
    LOOP_UNITTEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
set_target_properties(UnitTestsLifecycle PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
)
add_test(UnitTestsLifecycle "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsLifecycle")

add_executable(UnitTestsBenchmarkIdentity
    tst_benchmarkidentitytest.cpp
)
target_link_libraries(UnitTestsBenchmarkIdentity PRIVATE LoopLibCore Qt6::Core Qt6::Gui Qt6::Test)
set_target_properties(UnitTestsBenchmarkIdentity PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
)
add_test(UnitTestsBenchmarkIdentity "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsBenchmarkIdentity")

add_executable(UnitTestsConversionOracle
    tst_conversionoracletest.cpp
)
target_link_libraries(UnitTestsConversionOracle PRIVATE LoopLibCore Qt6::Core Qt6::Gui Qt6::Test)
target_compile_definitions(UnitTestsConversionOracle PRIVATE
    LOOP_PREFLIGHT_SOURCE_DIR="${CMAKE_SOURCE_DIR}/loop-preflight")
set_target_properties(UnitTestsConversionOracle PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
)
add_test(UnitTestsConversionOracle "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsConversionOracle")

add_executable(UnitTestsHugeDocumentEnvelope
    tst_hugedocumentenvelopetest.cpp
)
target_link_libraries(UnitTestsHugeDocumentEnvelope PRIVATE LoopLibCore Qt6::Core Qt6::Gui Qt6::Test)
set_target_properties(UnitTestsHugeDocumentEnvelope PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
)
add_test(UnitTestsHugeDocumentEnvelope "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsHugeDocumentEnvelope")

add_executable(UnitTestsPreflightProfileResolver
    tst_preflightprofileresolvertest.cpp
)

target_link_libraries(UnitTestsPreflightProfileResolver PRIVATE LoopLibCore Qt6::Core Qt6::Gui Qt6::Test)

set_target_properties(UnitTestsPreflightProfileResolver PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
)
add_test(UnitTestsPreflightProfileResolver "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsPreflightProfileResolver")

add_executable(UnitTestsOcrPageGate
    tst_ocrpagegatetest.cpp
)

target_link_libraries(UnitTestsOcrPageGate PRIVATE LoopLibCore Qt6::Core Qt6::Gui Qt6::Test)
target_compile_definitions(UnitTestsOcrPageGate PRIVATE
    LOOP_PREFLIGHT_SOURCE_DIR="${CMAKE_SOURCE_DIR}/loop-preflight")

set_target_properties(UnitTestsOcrPageGate PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
)
add_test(UnitTestsOcrPageGate "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsOcrPageGate")

add_executable(UnitTestsOcrCli
    tst_ocrclitest.cpp
)

target_link_libraries(UnitTestsOcrCli PRIVATE Qt6::Core Qt6::Test)
add_dependencies(UnitTestsOcrCli PdfTool)
target_compile_definitions(UnitTestsOcrCli PRIVATE
    LOOP_PREFLIGHT_SOURCE_DIR="${CMAKE_SOURCE_DIR}/loop-preflight"
    LOOP_OCR_SOURCE_DIR="${CMAKE_SOURCE_DIR}/loop-ocr"
    PDFTOOL_EXECUTABLE_PATH="$<TARGET_FILE:PdfTool>")

set_target_properties(UnitTestsOcrCli PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
)
add_test(UnitTestsOcrCli "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsOcrCli")

add_executable(UnitTestsOcrContract
    tst_ocrcontracttest.cpp
)

target_link_libraries(UnitTestsOcrContract PRIVATE Qt6::Core Qt6::Test)
target_include_directories(UnitTestsOcrContract PRIVATE
    ${CMAKE_SOURCE_DIR}/PdfTool)

set_target_properties(UnitTestsOcrContract PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
)
add_test(UnitTestsOcrContract "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsOcrContract")

add_executable(UnitTestsPdfToolContract
    tst_pdftoolcontract.cpp
)

target_link_libraries(UnitTestsPdfToolContract PRIVATE Qt6::Core Qt6::Test)
add_dependencies(UnitTestsPdfToolContract PdfTool)
target_compile_definitions(UnitTestsPdfToolContract PRIVATE
    PDFTOOL_EXECUTABLE_PATH="$<TARGET_FILE:PdfTool>"
    LOOP_PREFLIGHT_SOURCE_DIR="${CMAKE_SOURCE_DIR}/loop-preflight")

set_target_properties(UnitTestsPdfToolContract PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
)
add_test(UnitTestsPdfToolContract "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsPdfToolContract")

add_executable(UnitTestsBleedMarginProbe
	tst_bleedmarginprobetest.cpp
)

target_link_libraries(UnitTestsBleedMarginProbe PRIVATE LoopLibCore Qt6::Core Qt6::Gui Qt6::Test)

set_target_properties(UnitTestsBleedMarginProbe PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
)
add_test(UnitTestsBleedMarginProbe "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsBleedMarginProbe")

set_target_properties(UnitTestsBleedFixup PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
)
add_test(UnitTestsBleedFixup  "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsBleedFixup")

add_executable(UnitTestsRgbToCmyk
    tst_rgbtocmykfixup.cpp
)
target_link_libraries(UnitTestsRgbToCmyk PRIVATE LoopLibCore Qt6::Core Qt6::Gui Qt6::Test)
set_target_properties(UnitTestsRgbToCmyk PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
)
add_test(UnitTestsRgbToCmyk "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsRgbToCmyk")

add_executable(UnitTestsOverprint
	tst_overprinttest.cpp
)

target_link_libraries(UnitTestsOverprint PRIVATE LoopLibCore Qt6::Core Qt6::Gui Qt6::Test)
target_compile_definitions(UnitTestsOverprint PRIVATE
    LOOP_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

set_target_properties(UnitTestsOverprint PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
)
add_test(UnitTestsOverprint "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsOverprint")

if(NOT LOOP_BUILD_ONLY_CORE_LIBRARY)
    add_executable(UnitTestsOverprintRender
        tst_overprintrendertest.cpp
    )

    target_link_libraries(UnitTestsOverprintRender PRIVATE LoopLibCore Qt6::Core Qt6::Gui Qt6::Test)
    target_compile_definitions(UnitTestsOverprintRender PRIVATE
        LOOP_PREFLIGHT_SOURCE_DIR="${CMAKE_SOURCE_DIR}/loop-preflight")

    set_target_properties(UnitTestsOverprintRender PROPERTIES
        WIN32_EXECUTABLE OFF
        MACOSX_BUNDLE OFF
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
    )
    add_test(UnitTestsOverprintRender "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsOverprintRender")
endif()

# Guarded the same way LoopLibQuick's own add_subdirectory() is (see the top-level
# CMakeLists.txt): the tools/legacy-host build (LOOP_BUILD_ONLY_CORE_LIBRARY, or
# LOOP_BUILD_QUICK_CANVAS off) must not descend into anything that requires it.
#
# This compiles the design-system token/state-mapping sources directly rather
# than linking the LoopLibQuick target: they depend on QColor only, not on
# Qt Quick/Qml, and linking the SHARED LoopLibQuick library from a plain
# add_executable() test would be the first such link edge in this file --
# every other LoopLibQuick consumer uses qt_add_executable() plus
# qt_import_qml_plugins() (see ProductQuickAccessibilitySmoke/CMakeLists.txt),
# neither of which this test needs.
if(NOT LOOP_BUILD_ONLY_CORE_LIBRARY AND LOOP_BUILD_QUICK_CANVAS)
    add_executable(UnitTestsLoopStateVisual
        tst_loopstatevisualtest.cpp
        ${CMAKE_SOURCE_DIR}/LoopLibQuick/sources/looptokens.cpp
        ${CMAKE_SOURCE_DIR}/LoopLibQuick/sources/loopstatevisual.cpp
    )

    target_include_directories(UnitTestsLoopStateVisual PRIVATE
        ${CMAKE_SOURCE_DIR}/LoopLibQuick/sources
        ${CMAKE_BINARY_DIR}/${INSTALL_INCLUDEDIR}
    )
    target_compile_definitions(UnitTestsLoopStateVisual PRIVATE LoopLibQuick_EXPORTS)
    target_link_libraries(UnitTestsLoopStateVisual PRIVATE LoopLibCore Qt6::Core Qt6::Gui Qt6::Test)

    set_target_properties(UnitTestsLoopStateVisual PROPERTIES
        WIN32_EXECUTABLE OFF
        MACOSX_BUNDLE OFF
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
    )
    add_test(UnitTestsLoopStateVisual "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsLoopStateVisual")
endif()

add_executable(UnitTestsPageMasterExport
	tst_pagemasterexporttest.cpp
)

target_link_libraries(UnitTestsPageMasterExport PRIVATE LoopLibCore Qt6::Core Qt6::Gui Qt6::Test Qt6::Concurrent)

target_compile_definitions(UnitTestsPageMasterExport PRIVATE
    LOOP_PREFLIGHT_SOURCE_DIR="${CMAKE_SOURCE_DIR}/loop-preflight"
)

set_target_properties(UnitTestsPageMasterExport PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
)
add_test(UnitTestsPageMasterExport "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsPageMasterExport")

add_executable(UnitTestsTransparencyFlattener
    tst_transparencyflattenertest.cpp
)

target_link_libraries(UnitTestsTransparencyFlattener PRIVATE LoopLibCore Qt6::Core Qt6::Gui Qt6::Test)

set_target_properties(UnitTestsTransparencyFlattener PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
)
add_test(UnitTestsTransparencyFlattener "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsTransparencyFlattener")

add_executable(UnitTestsFilenameSanitizer
	tst_filenamesanitizertest.cpp
)

target_link_libraries(UnitTestsFilenameSanitizer PRIVATE LoopLibCore Qt6::Core Qt6::Test)
target_include_directories(UnitTestsFilenameSanitizer PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}/UnitTestsFilenameSanitizer_autogen/include)

set_target_properties(UnitTestsFilenameSanitizer PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
)
add_test(UnitTestsFilenameSanitizer "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsFilenameSanitizer")

add_executable(UnitTestsSafeFileWriter
	tst_safefilewritertest.cpp
)

target_link_libraries(UnitTestsSafeFileWriter PRIVATE LoopLibCore Qt6::Core Qt6::Gui Qt6::Test)
target_include_directories(UnitTestsSafeFileWriter PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}/UnitTestsSafeFileWriter_autogen/include)

set_target_properties(UnitTestsSafeFileWriter PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
)
add_test(UnitTestsSafeFileWriter "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsSafeFileWriter")

add_executable(UnitTestsContentProcessorLimits
	tst_contentprocessorlimitstest.cpp
)

target_link_libraries(UnitTestsContentProcessorLimits PRIVATE LoopLibCore Qt6::Core Qt6::Gui Qt6::Test)

set_target_properties(UnitTestsContentProcessorLimits PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
)
add_test(UnitTestsContentProcessorLimits "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsContentProcessorLimits")

add_executable(UnitTestsJbig2Decoder
	tst_jbig2decodertest.cpp
)

target_link_libraries(UnitTestsJbig2Decoder PRIVATE LoopLibCore Qt6::Core Qt6::Gui Qt6::Test)
target_include_directories(UnitTestsJbig2Decoder PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}/UnitTestsJbig2Decoder_autogen/include)

set_target_properties(UnitTestsJbig2Decoder PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
)
add_test(UnitTestsJbig2Decoder "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsJbig2Decoder")

add_executable(UnitTestsRedactVerifier
    tst_redactverifiertest.cpp
)

target_link_libraries(UnitTestsRedactVerifier PRIVATE LoopLibCore Qt6::Core Qt6::Gui Qt6::Test)

set_target_properties(UnitTestsRedactVerifier PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
)
add_test(UnitTestsRedactVerifier "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsRedactVerifier")

# Pure geometry math for the preflight custom checks (bleed / trim / page-size).
# Header-only, so it only needs the PdfTool include dir - no Core or PdfTool link.
add_executable(UnitTestsPreflightChecks
	tst_preflightchecks.cpp
)

target_link_libraries(UnitTestsPreflightChecks PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
target_include_directories(UnitTestsPreflightChecks PRIVATE
    ${CMAKE_SOURCE_DIR}/PdfTool
    ${CMAKE_SOURCE_DIR}/LoopLibCore/sources)

set_target_properties(UnitTestsPreflightChecks PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
)
add_test(UnitTestsPreflightChecks  "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsPreflightChecks")

# Drives `PdfTool preflight` against the loop-preflight golden corpus, so it needs
# the PdfTool executable, which is not built in LOOP_BUILD_ONLY_CORE_LIBRARY mode.
if(NOT LOOP_BUILD_ONLY_CORE_LIBRARY)
    add_executable(UnitTestsPreflightCorpus
        tst_preflightcorpus.cpp
    )

    target_link_libraries(UnitTestsPreflightCorpus PRIVATE Qt6::Core Qt6::Test)
    add_dependencies(UnitTestsPreflightCorpus PdfTool)
    target_compile_definitions(UnitTestsPreflightCorpus PRIVATE
        PDFTOOL_EXECUTABLE_PATH="$<TARGET_FILE:PdfTool>"
        LOOP_PREFLIGHT_SOURCE_DIR="${CMAKE_SOURCE_DIR}/loop-preflight"
    )

    set_target_properties(UnitTestsPreflightCorpus PROPERTIES
        WIN32_EXECUTABLE OFF
        MACOSX_BUNDLE OFF
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
    )
    add_test(UnitTestsPreflightCorpus  "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsPreflightCorpus")

    add_executable(UnitTestsBleedStress
        tst_bleedstresstest.cpp
    )

    target_link_libraries(UnitTestsBleedStress PRIVATE Qt6::Core Qt6::Test)
    add_dependencies(UnitTestsBleedStress PdfTool)
    target_compile_definitions(UnitTestsBleedStress PRIVATE
        PDFTOOL_EXECUTABLE_PATH="$<TARGET_FILE:PdfTool>"
        LOOP_PREFLIGHT_SOURCE_DIR="${CMAKE_SOURCE_DIR}/loop-preflight"
    )

    set_target_properties(UnitTestsBleedStress PROPERTIES
        WIN32_EXECUTABLE OFF
        MACOSX_BUNDLE OFF
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
    )
    add_test(UnitTestsBleedStress  "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsBleedStress")

    add_executable(UnitTestsOperatorAcceptance
        tst_operatoracceptance.cpp
    )

    target_link_libraries(UnitTestsOperatorAcceptance PRIVATE LoopLibCore Qt6::Core Qt6::Gui Qt6::Test)
    add_dependencies(UnitTestsOperatorAcceptance PdfTool)
    target_compile_definitions(UnitTestsOperatorAcceptance PRIVATE
        PDFTOOL_EXECUTABLE_PATH="$<TARGET_FILE:PdfTool>"
        LOOP_PREFLIGHT_SOURCE_DIR="${CMAKE_SOURCE_DIR}/loop-preflight"
    )
    target_include_directories(UnitTestsOperatorAcceptance PRIVATE
        ${CMAKE_SOURCE_DIR}/LoopLibCore/sources
        ${CMAKE_SOURCE_DIR}/UnitTests/support/preflight
        ${CMAKE_BINARY_DIR}/${INSTALL_INCLUDEDIR})

    set_target_properties(UnitTestsOperatorAcceptance PROPERTIES
        WIN32_EXECUTABLE OFF
        MACOSX_BUNDLE OFF
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
    )
    add_test(UnitTestsOperatorAcceptance  "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsOperatorAcceptance")

    add_executable(UnitTestsRepairOperatorAcceptance
        tst_repairoperatoracceptance.cpp
    )

    target_link_libraries(UnitTestsRepairOperatorAcceptance PRIVATE LoopLibCore Qt6::Core Qt6::Test)
    add_dependencies(UnitTestsRepairOperatorAcceptance PdfTool)
    target_compile_definitions(UnitTestsRepairOperatorAcceptance PRIVATE
        PDFTOOL_EXECUTABLE_PATH="$<TARGET_FILE:PdfTool>"
        LOOP_PREFLIGHT_SOURCE_DIR="${CMAKE_SOURCE_DIR}/loop-preflight"
    )
    target_include_directories(UnitTestsRepairOperatorAcceptance PRIVATE
        ${CMAKE_SOURCE_DIR}/LoopLibCore/sources
        ${CMAKE_BINARY_DIR}/${INSTALL_INCLUDEDIR})

    set_target_properties(UnitTestsRepairOperatorAcceptance PROPERTIES
        WIN32_EXECUTABLE OFF
        MACOSX_BUNDLE OFF
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
    )
    add_test(UnitTestsRepairOperatorAcceptance  "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsRepairOperatorAcceptance")
endif()

# Compiles both identity headers into one TU so the pdf::PDFArtifactIdentity
# redefinition that broke the Core build cannot silently return.
add_executable(UnitTestsIdentitySeparation
	tst_identityseparationtest.cpp
)

target_link_libraries(UnitTestsIdentitySeparation PRIVATE LoopLibCore Qt6::Core Qt6::Gui Qt6::Test)

set_target_properties(UnitTestsIdentitySeparation PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
)
add_test(UnitTestsIdentitySeparation "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsIdentitySeparation")

add_executable(UnitTestsApplicationIdentity
    tst_applicationidentitytest.cpp
)
target_link_libraries(UnitTestsApplicationIdentity PRIVATE LoopLibCore Qt6::Core Qt6::Test)
set_target_properties(UnitTestsApplicationIdentity PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
)
add_test(UnitTestsApplicationIdentity "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsApplicationIdentity")

add_executable(UnitTestsBudgetExhaustion
    tst_budgetexhaustiontest.cpp
)
target_link_libraries(UnitTestsBudgetExhaustion PRIVATE LoopLibCore Qt6::Core Qt6::Gui Qt6::Test)
target_compile_definitions(UnitTestsBudgetExhaustion PRIVATE
    BUDGET_EXHAUSTION_CORPUS_DIR="${CMAKE_SOURCE_DIR}/UnitTests/testdata/budget-exhaustion"
)
set_target_properties(UnitTestsBudgetExhaustion PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
)
add_test(UnitTestsBudgetExhaustion "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsBudgetExhaustion")
set_tests_properties(UnitTestsBudgetExhaustion PROPERTIES TIMEOUT 10)

# gh-243: synthetic adversarial PDF corpus, one fixture per PDFProcessingBudget
# dimension, read through PDFDocumentReader/PreflightEngine like a real upload.
# Fixtures and manifest.json are generated (not hand-written) by
# scripts/resource_envelope/budget_exhaustion_corpus.py; see
# UnitTests/testdata/budget_exhaustion/manifest.json and tst_budgetcorpustest.cpp.
add_executable(UnitTestsBudgetCorpus
    tst_budgetcorpustest.cpp
)
target_link_libraries(UnitTestsBudgetCorpus PRIVATE LoopLibCore Qt6::Core Qt6::Gui Qt6::Test)
target_compile_definitions(UnitTestsBudgetCorpus PRIVATE
    BUDGET_CORPUS_DIR="${CMAKE_SOURCE_DIR}/UnitTests/testdata/budget_exhaustion"
)
set_target_properties(UnitTestsBudgetCorpus PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
)
add_test(UnitTestsBudgetCorpus "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsBudgetCorpus")

add_executable(UnitTestsResourceBudget
    tst_resourcebudgettest.cpp
)
target_link_libraries(UnitTestsResourceBudget PRIVATE LoopLibCore Qt6::Core Qt6::Gui Qt6::Test)
set_target_properties(UnitTestsResourceBudget PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
)
add_test(UnitTestsResourceBudget "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsResourceBudget")

add_executable(UnitTestsWorkloadEnvelope
    tst_workloadenvelopetest.cpp
)
target_link_libraries(UnitTestsWorkloadEnvelope PRIVATE LoopLibCore Qt6::Core Qt6::Gui Qt6::Test)
set_target_properties(UnitTestsWorkloadEnvelope PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}
)
add_test(UnitTestsWorkloadEnvelope "${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR}/UnitTestsWorkloadEnvelope")

# windeployqt copies Core/Gui/etc. but not Qt Test; all UnitTests* targets share one output dir.
if(WIN32)
    add_custom_command(TARGET UnitTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:Qt6::Test>
            $<TARGET_FILE_DIR:UnitTests>
        COMMENT "Copy Qt6Test runtime for unit tests")
endif()
