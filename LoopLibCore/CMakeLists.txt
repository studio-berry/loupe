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

add_library(LoopLibCore SHARED
    sources/pdfglobal.h
    sources/pdfaction.cpp
    sources/pdfaction.h
    sources/pdfalgorithmlcs.cpp
    sources/pdfalgorithmlcs.h
    sources/pdfannotation.cpp
    sources/pdfannotation.h
    sources/pdfblendfunction.cpp
    sources/pdfblendfunction.h
    sources/pdfccittfaxdecoder.cpp
    sources/pdfccittfaxdecoder.h
    sources/pdfcertificatemanager.cpp
    sources/pdfcertificatemanager.h
    sources/pdfcms.cpp
    sources/pdfcms.h
    sources/pdfcolorinventory.cpp
    sources/pdfcolorinventory.h
    sources/pdfdiff.cpp
    sources/pdfdiff.h
    sources/pdfdocumentbuilder.cpp
    sources/pdfdocumentbuilder.h
    sources/pdfdocumentmanipulator.cpp
    sources/pdfdocumentmanipulator.h
    sources/pdfpagegeometry.cpp
    sources/pdfpagegeometry.h
    sources/pdfbleedfixup.cpp
    sources/pdfbleedfixup.h
    sources/pdfcontourbleedfixup.cpp
    sources/pdfcontourbleedfixup.h
    sources/pdfproductiongeometry.cpp
    sources/pdfproductiongeometry.h
    sources/pdfproductionrepair.cpp
    sources/pdfrepairdiff.cpp
    sources/pdfrepairdiff.h
    sources/pdfrepairoperation.cpp
    sources/pdfrepairoperation.h
    sources/pdfrepairprimitives.cpp
    sources/pdfstandardconversion.cpp
    sources/pdfstandardconversion.h
    sources/pdfactionlist.cpp
    sources/pdfactionlist.h
    sources/pdffixupregistry.cpp
    sources/pdffixupregistry.h
    sources/pdfimagedownsamplefixup.cpp
    sources/pdfimagedownsamplefixup.h
    sources/pdfrgbtocmykfixup.cpp
    sources/pdfrgbtocmykfixup.h
    sources/pdfpagemasterexport.cpp
    sources/pdfpagemasterexport.h
    sources/pdfbleedmarginprobe.cpp
    sources/pdfbleedmarginprobe.h
    sources/pdfinkcoverageprobe.cpp
    sources/pdfinkcoverageprobe.h
    sources/pdfthinpartprobe.cpp
    sources/pdfthinpartprobe.h
    sources/pdfocrpagegate.cpp
    sources/pdfocrpagegate.h
    sources/pdfocrreport.cpp
    sources/pdfocrreport.h
    sources/pdfpagecachebudget.cpp
    sources/pdfpagecachebudget.h
    sources/pdfresourcebudget.cpp
    sources/pdfresourcebudget.h
    sources/pdfdocumentsession.cpp
    sources/pdfdocumentsession.h
    sources/pdfworkloadenvelope.cpp
    sources/pdfworkloadenvelope.h
    sources/pdfdocumentcontext.cpp
    sources/pdfdocumentcontext.h
    sources/pdfdocumentsearch.cpp
    sources/pdfdocumentsearch.h
    sources/pdfprocessingbudget.cpp
    sources/pdfprocessingbudget.h
    sources/pdfjobscheduler.cpp
    sources/pdfjobscheduler.h
    sources/pdfblockingthreadguard.cpp
    sources/pdfblockingthreadguard.h
    sources/pdfschemaversion.cpp
    sources/pdfschemaversion.h
    sources/pdfevidencegraph.cpp
    sources/pdfevidencegraph.h
    sources/pdfoperationimpact.cpp
    sources/pdfoperationimpact.h
    sources/preflightengine.cpp
    sources/preflightengine.h
    sources/preflightclirun.cpp
    sources/preflightclirun.h
    sources/pdfpreflightverdict.cpp
    sources/pdfpreflightverdict.h
    sources/preflightprofileresolver.cpp
    sources/preflightprofileresolver.h
    sources/pdfpreflightchecks.h
    sources/pdfdocumenttextflow.cpp
    sources/pdfdocumenttextflow.h
    sources/pdfdocumenttextfloweditormodel.cpp
    sources/pdfdocumenttextfloweditormodel.h
    sources/pdfdocumentwriter.cpp
    sources/pdfdocumentwriter.h
    sources/pdfwriteobjectvisitor.cpp
    sources/pdfwriteobjectvisitor_p.h
    sources/pdfexecutionpolicy.cpp
    sources/pdfexecutionpolicy.h
    sources/pdffile.cpp
    sources/pdffile.h
    sources/pdffilenamesanitizer.cpp
    sources/pdffilenamesanitizer.h
    sources/pdfsafefilewriter.cpp
    sources/pdfsafefilewriter.h
    sources/pdfform.cpp
    sources/pdfform.h
    sources/pdficontheme.cpp
    sources/pdficontheme.h
    sources/pdfjavascriptscanner.cpp
    sources/pdfjavascriptscanner.h
    sources/pdfjbig2decoder.cpp
    sources/pdfjbig2decoder.h
    sources/pdfmultimedia.cpp
    sources/pdfmultimedia.h
    sources/pdfobject.cpp
    sources/pdfobject.h
    sources/pdfobjecteditormodel.cpp
    sources/pdfobjecteditormodel.h
    sources/pdfobjectutils.cpp
    sources/pdfobjectutils.h
    sources/pdfoptimizer.cpp
    sources/pdfoptimizer.h
    sources/pdfoptionalcontent.cpp
    sources/pdfoptionalcontent.h
    sources/pdfoutline.cpp
    sources/pdfoutline.h
    sources/pdfpagenavigation.cpp
    sources/pdfpagenavigation.h
    sources/pdfpagetransition.cpp
    sources/pdfpagetransition.h
    sources/pdfpainterutils.cpp
    sources/pdfpainterutils.h
    sources/pdfparser.cpp
    sources/pdfparser.h
    sources/pdfdocument.cpp
    sources/pdfdocument.h
    sources/pdfdocumentreader.cpp
    sources/pdfdocumentreaderio.cpp
    sources/pdfdocumentreader.h
    sources/pdfpattern.cpp
    sources/pdfpattern.h
    sources/pdfplugin.cpp
    sources/pdfplugin.h
    sources/pdfprogress.cpp
    sources/pdfprogress.h
    sources/pdfredact.cpp
    sources/pdfredact.h
    sources/pdfredactverifier.cpp
    sources/pdfredactverifier.h
    sources/pdfsecurityhandler.cpp
    sources/pdfsecurityhandler.h
    sources/pdfsignaturehandler.cpp
    sources/pdfsignaturehandler.h
    sources/pdfsnapper.cpp
    sources/pdfsnapper.h
    sources/pdfstructuretree.cpp
    sources/pdfstructuretree.h
    sources/pdftextlayout.cpp
    sources/pdftextlayout.h
    sources/pdftransparencyrenderer.cpp
    sources/pdftransparencyflattener.cpp
    sources/pdftransparencyrenderer.h
    sources/pdftransparencyflattener.h
    sources/pdfutils.cpp
    sources/pdfutils.h
    sources/pdfxfaengine.cpp
    sources/pdfxfaengine.h
    sources/pdfxreftable.cpp
    sources/pdfxreftable.h
    sources/pdfvisitor.cpp
    sources/pdfvisitor.h
    sources/pdfencoding.cpp
    sources/pdfencoding.h
    sources/pdfcatalog.cpp
    sources/pdfcatalog.h
    sources/pdfpage.cpp
    sources/pdfpage.h
    sources/pdfstreamfilters.cpp
    sources/pdfstreamfilters.h
    sources/pdfcolorspaces.cpp
    sources/pdfcolorspaces.h
    sources/pdfrenderer.cpp
    sources/pdfrenderer.h
    sources/pdfpagecontentprocessor.cpp
    sources/pdfpagecontentprocessor.h
    sources/pdfpainter.cpp
    sources/pdfpainter.h
    sources/pdffunction.cpp
    sources/pdffunction.h
    sources/pdfnametounicode.cpp
    sources/pdfnametounicode.h
    sources/pdffont.cpp
    sources/pdffont.h
    sources/pdffontintegrity.cpp
    sources/pdffontintegrity.h
    sources/pdfimagecompressor.cpp
    sources/pdfimagecompressor.h
    sources/pdfimageoptimizer.cpp
    sources/pdfimageoptimizer.h
    sources/pdfimage.cpp
    sources/pdfimage.h
    sources/pdfdocumentsanitizer.h
    sources/pdfdocumentsanitizer.cpp
    sources/pdfimageconversion.h
    sources/pdfimageconversion.cpp
    sources/pdfcolorconvertor.h
    sources/pdfcolorconvertor.cpp
    sources/pdftextlayoutgenerator.h
    sources/pdftextlayoutgenerator.cpp
    sources/pdfwidgetsnapshot.cpp
    sources/pdfwidgetsnapshot.h
    cmaps.qrc
    schema-compatibility.qrc
    aatl.qrc
    sources/pdfcertificatestore.h
    sources/pdfcertificatestore.cpp
    sources/pdfblpainter.h
    sources/pdfblpainter.cpp
    sources/pdfpagecontenteditorprocessor.h
    sources/pdfpagecontenteditorprocessor.cpp
    sources/pdfpagecontenteditorcontentstreambuilder.h
    sources/pdfpagecontenteditorcontentstreambuilder.cpp
    sources/pdfeditorfallbackfont.h
    sources/pdfeditorfallbackfont.cpp
    sources/pdfapplicationtranslator.h
    sources/pdfapplicationtranslator.cpp
    sources/pdfapplicationidentity.h
    sources/pdfapplicationidentity.cpp
    sources/pdfsettings.h
    sources/pdfsettings.cpp
    sources/pdfsentry.h
    sources/pdfsentry.cpp
    sources/pdflogger.h
    sources/pdflogger.cpp
    sources/pdflogscrubber.h
    sources/pdflogscrubber.cpp
    sources/pdfdiagnostics.h
    sources/pdfdiagnostics.cpp
    sources/pdfartifactidentity.h
    sources/pdfartifactidentity.cpp
    sources/pdfartifactstore.h
    sources/pdfartifactstore.cpp
    sources/pdfoperationhistory.h
    sources/pdfoperationhistory.cpp
    sources/pdfoperationhistorystore.h
    sources/pdfoperationhistorystore.cpp
    sources/pdfsavepolicy.h
    sources/pdfsavepolicy.cpp
    fonts.qrc
)

include(GenerateExportHeader)

GENERATE_EXPORT_HEADER(LoopLibCore
                       EXPORT_MACRO_NAME
                       LOOPLIBCORESHARED_EXPORT
                       EXPORT_FILE_NAME "${CMAKE_BINARY_DIR}/${INSTALL_INCLUDEDIR}/looplibcore_export.h")

target_link_libraries(LoopLibCore PRIVATE Qt6::Core Qt6::Gui Qt6::Xml Qt6::Svg Qt6::Sql)
target_link_libraries(LoopLibCore PRIVATE ${LCMS2_LIBRARIES})
target_link_libraries(LoopLibCore PRIVATE OpenSSL::SSL OpenSSL::Crypto)
target_link_libraries(LoopLibCore PRIVATE ZLIB::ZLIB)
target_link_libraries(LoopLibCore PRIVATE Freetype::Freetype)
target_link_libraries(LoopLibCore PRIVATE openjp2)
target_link_libraries(LoopLibCore PRIVATE JPEG::JPEG)

if(LOOP_USE_TBB)
    target_link_libraries(LoopLibCore PUBLIC TBB::tbb)
endif()

if(MINGW)
    target_link_libraries(LoopLibCore PRIVATE Secur32 Mscms Gdi32 User32 crypt32)
endif()

if(WIN32)
    target_link_libraries(LoopLibCore PRIVATE Psapi shell32)
endif()

target_include_directories(LoopLibCore INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/sources)
target_include_directories(LoopLibCore PRIVATE ${CMAKE_BINARY_DIR})
target_include_directories(LoopLibCore PUBLIC ${CMAKE_BINARY_DIR}/${INSTALL_INCLUDEDIR})

target_link_libraries(LoopLibCore PRIVATE blend2d::blend2d)

if(LOOP_ENABLE_SENTRY)
    target_compile_definitions(LoopLibCore PUBLIC LOOP_ENABLE_SENTRY)
    target_compile_definitions(LoopLibCore PRIVATE LOOP_ENABLE_SENTRY_IMPL)
    if(LOOP_SENTRY_DSN)
        target_compile_definitions(LoopLibCore PRIVATE LOOP_SENTRY_DSN="${LOOP_SENTRY_DSN}")
    endif()
    target_link_libraries(LoopLibCore PRIVATE sentry::sentry)
endif()

find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE LOOP_GIT_COMMIT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE LOOP_GIT_COMMIT_RESULT)
    if(LOOP_GIT_COMMIT AND LOOP_GIT_COMMIT_RESULT EQUAL 0)
        target_compile_definitions(LoopLibCore PRIVATE LOOP_GIT_COMMIT="${LOOP_GIT_COMMIT}")
    endif()
endif()

set_target_properties(LoopLibCore PROPERTIES
                      VERSION ${LOOP_VERSION}
                      SOVERSION ${LOOP_VERSION}
                      LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_LIB_DIR}
                      RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LOOP_INSTALL_BIN_DIR})

install(TARGETS LoopLibCore RUNTIME DESTINATION ${LOOP_INSTALL_BIN_DIR} LIBRARY DESTINATION ${LOOP_INSTALL_LIB_DIR})
