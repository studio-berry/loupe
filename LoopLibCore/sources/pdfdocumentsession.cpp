// MIT License
//
// Copyright (c) 2018-2025 Jakub Melka and Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "pdfdocumentsession.h"

#include "pdfconstants.h"
#include "pdfcms.h"
#include "pdffont.h"
#include "pdfoptionalcontent.h"
#include "pdfpagecachebudget.h"
#include "pdfprocessingbudget.h"

#include <functional>
#include <limits>
#include <tuple>
#include <unordered_set>
#include <QtGlobal>

namespace pdf
{

constexpr size_t PDFDocumentSession::CompileCacheLimit;
constexpr size_t PDFDocumentSession::StreamCacheLimit;
constexpr size_t PDFDocumentSession::ShedCompileCacheLimit;
constexpr size_t PDFDocumentSession::ShedStreamCacheLimit;
constexpr int PDFDocumentSession::ShedQualityPercent;
constexpr qsizetype PDFDocumentSession::CompiledCacheByteLimitDefault;
constexpr qsizetype PDFDocumentSession::ShedCompiledCacheByteLimit;

namespace
{

qsizetype estimateDocumentModelBytesImpl(const PDFDocument* document)
{
    if (!document)
    {
        return 0;
    }

    quint64 total = sizeof(PDFDocument);
    const PDFObjectStorage::PDFObjects& objects = document->getStorage().getObjects();

    std::unordered_set<const PDFObjectContent*> visitedContents;

    const auto addProduct = [&total](quint64 count, quint64 size)
    {
        if (count > 0 && size > std::numeric_limits<quint64>::max() / count)
        {
            total = std::numeric_limits<quint64>::max();
            return;
        }

        const quint64 product = count * size;
        if (product > std::numeric_limits<quint64>::max() - total)
        {
            total = std::numeric_limits<quint64>::max();
        }
        else
        {
            total += product;
        }
    };

    const auto addBytes = [&total](quint64 bytes)
    {
        if (bytes > std::numeric_limits<quint64>::max() - total)
        {
            total = std::numeric_limits<quint64>::max();
        }
        else
        {
            total += bytes;
        }
    };

    std::function<void(const PDFObject&)> estimateObject;
    std::function<void(const PDFDictionary*, bool)> estimateDictionary;

    estimateDictionary = [&](const PDFDictionary* dictionary, bool includeObject)
    {
        if (!dictionary || !visitedContents.insert(dictionary).second)
        {
            return;
        }

        if (includeObject)
        {
            addBytes(sizeof(PDFDictionary));
        }
        addProduct(static_cast<quint64>(dictionary->getCapacity()), sizeof(PDFDictionary::DictionaryEntry));

        for (size_t i = 0; i < dictionary->getCount(); ++i)
        {
            const QByteArray key = dictionary->getKey(i).getString();
            if (!dictionary->getKey(i).isInplace())
            {
                addBytes(static_cast<quint64>(key.capacity()));
            }
            estimateObject(dictionary->getValue(i));
        }
    };

    estimateObject = [&](const PDFObject& object)
    {
        switch (object.getType())
        {
            case PDFObject::Type::String:
            case PDFObject::Type::Name:
            {
                const PDFStringRef string = object.getStringObject();
                if (string.memoryString && visitedContents.insert(string.memoryString).second)
                {
                    addBytes(sizeof(PDFString));
                    addBytes(static_cast<quint64>(string.memoryString->getString().capacity()));
                }
                break;
            }
            case PDFObject::Type::Array:
            {
                const PDFArray* array = object.getArray();
                if (!array || !visitedContents.insert(array).second)
                {
                    break;
                }
                addBytes(sizeof(PDFArray));
                addProduct(static_cast<quint64>(array->getCapacity()), sizeof(PDFObject));
                for (size_t i = 0; i < array->getCount(); ++i)
                {
                    estimateObject(array->getItem(i));
                }
                break;
            }
            case PDFObject::Type::Dictionary:
                estimateDictionary(object.getDictionary(), true);
                break;
            case PDFObject::Type::Stream:
            {
                const PDFStream* stream = object.getStream();
                if (!stream || !visitedContents.insert(stream).second)
                {
                    break;
                }
                addBytes(sizeof(PDFStream));
                addBytes(static_cast<quint64>(stream->getContent()->capacity()));
                estimateDictionary(stream->getDictionary(), false);
                break;
            }
            default:
                break;
        }
    };

    addProduct(static_cast<quint64>(objects.capacity()), sizeof(PDFObjectStorage::Entry));

    for (const PDFObjectStorage::Entry& entry : objects)
    {
        estimateObject(entry.object);
    }
    estimateObject(document->getStorage().getTrailerDictionary());

    const PDFCatalog* catalog = document->getCatalog();
    if (catalog)
    {
        addBytes(static_cast<quint64>(catalog->getMemoryConsumptionEstimate()));
    }
    return total > static_cast<quint64>(std::numeric_limits<qsizetype>::max())
               ? std::numeric_limits<qsizetype>::max()
               : static_cast<qsizetype>(total);
}

}   // namespace

qsizetype PDFDocumentSession::estimateDocumentModelBytes(const PDFDocument* document)
{
    return estimateDocumentModelBytesImpl(document);
}

PDFDocumentSession::PDFDocumentSession(PDFDocument* document,
                                       PDFDocumentContext* context,
                                       std::shared_ptr<PDFPageCacheBudget> pageCacheBudget,
                                       PDFDocumentSessionAdmission admission) :
    m_document(document),
    m_context(context),
    m_localDocumentIdentity(PDFDocumentIdentity::fromDocument(document)),
    m_features(PDFRenderer::getDefaultFeatures()),
    m_processingBudget(std::make_unique<PDFProcessingBudget>()),
    m_resourceBudget(admission == PDFDocumentSessionAdmission::Managed ? std::make_shared<PDFResourceBudget>() : nullptr),
    m_pageCacheBudget(admission == PDFDocumentSessionAdmission::Managed
                          ? (pageCacheBudget ? std::move(pageCacheBudget) : std::make_shared<PDFPageCacheBudget>())
                          : nullptr),
    m_admission(admission),
    m_compiledCachePressureLimit(admission == PDFDocumentSessionAdmission::Managed ? m_pageCacheBudget->compiledLimit()
                                                                                   : CompiledCacheByteLimitDefault)
{
    if (admission == PDFDocumentSessionAdmission::Managed)
    {
        // The page-cache budget is the authority for the combined compiled-page
        // and surface ceiling. The broader resource envelope still records those
        // bytes alongside document and stream resources, but its matching pool
        // limit follows the authoritative page-cache partition.
        m_resourceBudget->setLimit(PDFResourcePool::CompiledEvidenceCache, m_pageCacheBudget->compiledLimit());
        m_streamCacheByteLimit = m_resourceBudget->limit(PDFResourcePool::DecodedStreamImageCache);

        const qsizetype modelBytes = estimateDocumentModelBytes(m_document);
        if (modelBytes > 0)
        {
            m_documentModelReservation = m_resourceBudget->reserveShared(m_resourceBudget,
                                                                         PDFResourcePool::ActiveDocumentModel,
                                                                         modelBytes,
                                                                         PDFResourcePriority::Interaction,
                                                                         QStringLiteral("active document model"));
        }
    }
    else
    {
        m_streamCacheByteLimit = 256 * PDFResourceBudgetConfig::MiB;
    }
}

bool PDFDocumentSession::hasManagedAdmission() const noexcept
{
    return m_admission == PDFDocumentSessionAdmission::Managed;
}

PDFDocumentSession::~PDFDocumentSession()
{
    // The session can be replaced while its shared budget remains owned by the
    // document context. Release every compiled-page reservation before the old
    // cache storage disappears, otherwise the replacement inherits phantom
    // resident bytes and eventually refuses valid work.
    clearCompiledCache();
    clearDecodedStreamCache();
}

PDFDocumentSession* PDFDocumentSession::create(PDFDocument* document,
                                               PDFDocumentContext* context,
                                               std::shared_ptr<PDFPageCacheBudget> pageCacheBudget)
{
    return new PDFDocumentSession(document, context, std::move(pageCacheBudget));
}

PDFDocumentSession* PDFDocumentSession::createForInspection(PDFDocument* document)
{
    return new PDFDocumentSession(document, nullptr, nullptr, PDFDocumentSessionAdmission::Inspection);
}

void PDFDocumentSession::destroy(PDFDocumentSession* session) noexcept
{
    delete session;
}

PDFDocument* PDFDocumentSession::getDocument() const
{
    return m_document;
}

PDFRevisionIdentity PDFDocumentSession::getRevision() const
{
    if (m_context)
    {
        return m_context->getRevision();
    }

    PDFRevisionIdentity revision;
    revision.document = m_localDocumentIdentity;
    revision.documentRevision = m_localDocumentRevision;
    revision.cacheGeneration = m_localCacheGeneration;
    return revision;
}

bool PDFDocumentSession::isCurrent(const PDFRevisionIdentity& revision) const
{
    return revision == getRevision();
}

bool PDFDocumentSession::isValid() const
{
    return m_document != nullptr;
}

void PDFDocumentSession::setRendererFeatures(PDFRenderer::Features features)
{
    if (m_features == features)
    {
        return;
    }

    m_features = features;
    if (m_context)
    {
        m_context->invalidateCaches();
    }
    else
    {
        ++m_localCacheGeneration;
        clearCompiledCache();
    }

    if (m_renderer)
    {
        PDFMeshQualitySettings meshQualitySettings;
        m_renderer = std::make_unique<PDFRenderer>(m_document,
                                                   m_fontCache.get(),
                                                   m_cms.get(),
                                                   m_optionalContentActivity.get(),
                                                   m_features,
                                                   meshQualitySettings,
                                                   m_processingBudget.get());
    }
}

PDFRenderer::Features PDFDocumentSession::getRendererFeatures() const
{
    return m_features;
}

PDFProcessingBudget* PDFDocumentSession::getProcessingBudget() const
{
    return m_processingBudget.get();
}

PDFResourceBudget* PDFDocumentSession::getResourceBudget() const
{
    return m_resourceBudget.get();
}

std::shared_ptr<PDFResourceBudget> PDFDocumentSession::getSharedResourceBudget() const
{
    return m_resourceBudget;
}

const PDFProcessingLimits& PDFDocumentSession::getProcessingLimits() const
{
    return m_processingBudget->limits();
}

void PDFDocumentSession::setProcessingLimits(const PDFProcessingLimits& limits)
{
    m_processingBudget = std::make_unique<PDFProcessingBudget>(limits);
    if (m_context)
    {
        m_context->invalidateCaches();
    }
    else
    {
        invalidate();
    }
    m_renderer.reset();
    m_fontCache.reset();
    m_cms.reset();
    m_cmsManager.reset();
    m_optionalContentActivity.reset();
    initializeRendering();
}

void PDFDocumentSession::resetProcessingBudget()
{
    m_processingBudget->reset();
}

void PDFDocumentSession::shedPrefetchAndQuality()
{
    m_prefetchEnabled = false;
    m_qualityPercent = qMin(m_qualityPercent, ShedQualityPercent);
    m_qualityPrefetchShed = true;
    m_compileCacheLimit = qMin(m_compileCacheLimit, ShedCompileCacheLimit);
    m_streamCacheLimit = qMin(m_streamCacheLimit, ShedStreamCacheLimit);
    m_compiledCachePressureLimit = qMin(m_compiledCachePressureLimit, static_cast<qsizetype>(ShedCompiledCacheByteLimit));
    m_streamCacheByteLimit = qMin(m_streamCacheByteLimit, 16 * PDFResourceBudgetConfig::MiB);
    trimCachesToLimits();
}

qsizetype PDFDocumentSession::compiledCacheBytes() const
{
    return m_pageCacheBudget->usage(PDFPageCacheBudget::Pool::CompiledPages);
}

qsizetype PDFDocumentSession::compiledCacheByteLimit() const
{
    return qMin(m_pageCacheBudget->compiledLimit(), m_compiledCachePressureLimit);
}

qsizetype PDFDocumentSession::decodedStreamCacheBytes() const
{
    return m_streamCacheBytes;
}

qsizetype PDFDocumentSession::decodedStreamCacheByteLimit() const
{
    return m_streamCacheByteLimit;
}

void PDFDocumentSession::setCompiledCacheByteLimit(qsizetype bytes)
{
    bytes = qMax<qsizetype>(0, bytes);
    const qsizetype maximum = std::numeric_limits<qsizetype>::max();
    const qsizetype total = bytes > maximum / 2 ? maximum : bytes * 2;
    m_pageCacheBudget->setTotal(total);
    m_resourceBudget->setLimit(PDFResourcePool::CompiledEvidenceCache, m_pageCacheBudget->compiledLimit());
    m_compiledCachePressureLimit = m_pageCacheBudget->compiledLimit();
    trimCachesToLimits();
}

void PDFDocumentSession::setCacheLimit(qsizetype totalBytes)
{
    m_pageCacheBudget->setTotal(PDFPageCacheBudget::total(totalBytes));
    m_resourceBudget->setLimit(PDFResourcePool::CompiledEvidenceCache, m_pageCacheBudget->compiledLimit());
    m_compiledCachePressureLimit = m_pageCacheBudget->compiledLimit();
    trimCachesToLimits();
}

qsizetype PDFDocumentSession::cacheLimit() const
{
    return m_pageCacheBudget->total();
}

void PDFDocumentSession::trimCachesToLimits()
{
    while (!m_compileCacheOrder.empty() && (m_compileCacheOrder.size() > m_compileCacheLimit || compiledCacheBytes() > compiledCacheByteLimit()))
    {
        const PageCacheKey key = m_compileCacheOrder.front();
        auto bytesIt = m_compileCacheBytes.find(key);
        if (bytesIt != m_compileCacheBytes.end())
        {
            const qsizetype bytes = bytesIt->second;
            m_pageCacheBudget->release(PDFPageCacheBudget::Pool::CompiledPages, bytes);
            m_resourceBudget->release(PDFResourcePool::CompiledEvidenceCache, bytes);
            m_resourceBudget->recordEviction(PDFResourcePool::CompiledEvidenceCache, bytes);
            m_compileCacheBytes.erase(bytesIt);
        }
        m_compileCache.erase(key);
        m_compileCacheOrder.pop_front();
    }
    while (!m_streamCacheOrder.empty() &&
           (m_streamCacheOrder.size() > m_streamCacheLimit ||
            m_streamCacheBytes > m_streamCacheByteLimit))
    {
        const StreamCacheKey key = m_streamCacheOrder.front();
        auto it = m_streamCache.find(key);
        if (it != m_streamCache.end())
        {
            const qsizetype bytes = it->second.size();
            m_streamCacheBytes -= bytes;
            m_resourceBudget->release(PDFResourcePool::DecodedStreamImageCache, bytes);
            m_resourceBudget->recordEviction(PDFResourcePool::DecodedStreamImageCache, bytes);
            m_streamCache.erase(it);
        }
        m_streamCacheOrder.pop_front();
    }
}

void PDFDocumentSession::clearCompiledCache()
{
    if (!hasManagedAdmission())
    {
        m_compileCache.clear();
        m_compileCacheOrder.clear();
        m_compileCacheBytes.clear();
        return;
    }

    for (const auto& [key, bytes] : m_compileCacheBytes)
    {
        Q_UNUSED(key);
        m_pageCacheBudget->release(PDFPageCacheBudget::Pool::CompiledPages, bytes);
        m_resourceBudget->release(PDFResourcePool::CompiledEvidenceCache, bytes);
    }
    m_compileCache.clear();
    m_compileCacheOrder.clear();
    m_compileCacheBytes.clear();
}

void PDFDocumentSession::clearDecodedStreamCache()
{
    if (!hasManagedAdmission())
    {
        m_streamCache.clear();
        m_streamCacheOrder.clear();
        m_streamCacheBytes = 0;
        return;
    }

    for (const auto& [key, decoded] : m_streamCache)
    {
        Q_UNUSED(key);
        m_resourceBudget->release(PDFResourcePool::DecodedStreamImageCache, decoded.size());
    }
    m_streamCache.clear();
    m_streamCacheOrder.clear();
    m_streamCacheBytes = 0;
}

const PDFPrecompiledPage* PDFDocumentSession::compilePage(size_t pageIndex)
{
    if (!isValid())
    {
        return nullptr;
    }

    ensureRenderingInitialized();

    const PDFCatalog* catalog = m_document->getCatalog();
    if (!catalog || pageIndex >= static_cast<size_t>(catalog->getPageCount()))
    {
        return nullptr;
    }

    if (!hasManagedAdmission())
    {
        PDFPrecompiledPage compiledPage;
        m_renderer->compile(&compiledPage, pageIndex);
        if (!compiledPage.isValid())
        {
            return nullptr;
        }

        m_inspectionCompileScratch = std::move(compiledPage);
        return &m_inspectionCompileScratch;
    }

    const PageCacheKey key{ getRevision(), pageIndex };
    auto it = m_compileCache.find(key);
    if (it != m_compileCache.cend())
    {
        return &it->second;
    }

    PDFPrecompiledPage compiledPage;
    m_renderer->compile(&compiledPage, pageIndex);

    qsizetype estimate = static_cast<qsizetype>(compiledPage.getMemoryConsumptionEstimate());
    if (estimate <= 0)
    {
        // A finalized precompiled page always reports at least sizeof(*this).
        // Do not admit an entry whose resident size cannot be accounted for.
        return nullptr;
    }

    const qsizetype compiledLimit = compiledCacheByteLimit();
    if (m_compileCacheLimit == 0 || estimate > compiledLimit)
    {
        // An entry larger than its entire partition cannot be made resident
        // without violating the shared byte cap. Returning nullptr is safe for
        // callers: no cache entry or dangling pointer is exposed.
        return nullptr;
    }

    // Evict before inserting, so the pointer returned below is never the entry
    // this call dropped. Evict by both entry count and byte budget.
    while (!m_compileCacheOrder.empty() &&
           (m_compileCacheOrder.size() >= m_compileCacheLimit ||
            compiledCacheBytes() > compiledLimit - estimate))
    {
        const PageCacheKey evictKey = m_compileCacheOrder.front();
        auto bytesIt = m_compileCacheBytes.find(evictKey);
        if (bytesIt != m_compileCacheBytes.end())
        {
            const qsizetype bytes = bytesIt->second;
            m_pageCacheBudget->release(PDFPageCacheBudget::Pool::CompiledPages, bytes);
            m_resourceBudget->release(PDFResourcePool::CompiledEvidenceCache, bytes);
            m_resourceBudget->recordEviction(PDFResourcePool::CompiledEvidenceCache, bytes);
            m_compileCacheBytes.erase(bytesIt);
        }
        m_compileCache.erase(evictKey);
        m_compileCacheOrder.pop_front();
    }

    if (!m_pageCacheBudget->tryReserve(PDFPageCacheBudget::Pool::CompiledPages, estimate))
    {
        // The shared authority may also account for admitted page surfaces. A
        // failed reservation is therefore a real combined-budget refusal, not
        // a reason to insert an unaccounted compiled page.
        return nullptr;
    }

    if (!m_resourceBudget->tryReserve(PDFResourcePool::CompiledEvidenceCache,
                                      estimate,
                                      PDFResourcePriority::Visible,
                                      QStringLiteral("compiled page cache")))
    {
        m_pageCacheBudget->release(PDFPageCacheBudget::Pool::CompiledPages, estimate);
        m_resourceBudget->recordShed(PDFResourcePool::CompiledEvidenceCache);
        return nullptr;
    }

    m_compileCacheOrder.push_back(key);
    auto result = m_compileCache.emplace(key, std::move(compiledPage));
    m_compileCacheBytes[key] = estimate;
    return &result.first->second;
}

QByteArray PDFDocumentSession::getDecodedStream(PDFObjectReference reference)
{
    if (!isValid())
    {
        return QByteArray();
    }

    if (!hasManagedAdmission())
    {
        const PDFObject& object = m_document->getObjectByReference(reference);
        if (!object.isStream())
        {
            return QByteArray();
        }

        return m_document->getStorage().getDecodedStream(object.getStream(), m_processingBudget.get());
    }

    const StreamCacheKey key{ getRevision(), reference };
    auto it = m_streamCache.find(key);
    if (it != m_streamCache.cend())
    {
        return it->second;
    }

    const PDFObject& object = m_document->getObjectByReference(reference);
    if (!object.isStream())
    {
        return QByteArray();
    }

    QByteArray decoded = m_document->getStorage().getDecodedStream(object.getStream(), m_processingBudget.get());

    const qsizetype decodedBytes = decoded.size();
    if (m_streamCacheLimit == 0 || decodedBytes > m_streamCacheByteLimit)
    {
        m_resourceBudget->recordShed(PDFResourcePool::DecodedStreamImageCache, PDFResourcePriority::Background);
        return decoded;
    }

    while (!m_streamCacheOrder.empty() &&
           (m_streamCacheOrder.size() >= m_streamCacheLimit ||
            m_streamCacheBytes > m_streamCacheByteLimit - decodedBytes))
    {
        const StreamCacheKey evictKey = m_streamCacheOrder.front();
        auto evictIt = m_streamCache.find(evictKey);
        if (evictIt != m_streamCache.end())
        {
            const qsizetype bytes = evictIt->second.size();
            m_streamCacheBytes -= bytes;
            m_resourceBudget->release(PDFResourcePool::DecodedStreamImageCache, bytes);
            m_resourceBudget->recordEviction(PDFResourcePool::DecodedStreamImageCache, bytes);
            m_streamCache.erase(evictIt);
        }
        m_streamCacheOrder.pop_front();
    }

    if (!m_resourceBudget->tryReserve(PDFResourcePool::DecodedStreamImageCache,
                                      decodedBytes,
                                      PDFResourcePriority::Background,
                                      QStringLiteral("decoded stream cache")))
    {
        return decoded;
    }

    m_streamCacheOrder.push_back(key);
    auto result = m_streamCache.emplace(key, std::move(decoded));
    m_streamCacheBytes += decodedBytes;
    return result.first->second;
}

void PDFDocumentSession::invalidate()
{
    if (!m_context)
    {
        ++m_localCacheGeneration;
    }
    clearCompiledCache();
    clearDecodedStreamCache();
}

PDFRenderer* PDFDocumentSession::getRenderer() const
{
    ensureRenderingInitialized();
    return m_renderer.get();
}

PDFFontCache* PDFDocumentSession::getFontCache() const
{
    ensureRenderingInitialized();
    return m_fontCache.get();
}

PDFCMS* PDFDocumentSession::getCMS() const
{
    ensureRenderingInitialized();
    return m_cms.get();
}

PDFOptionalContentActivity* PDFDocumentSession::getOptionalContentActivity() const
{
    ensureRenderingInitialized();
    return m_optionalContentActivity.get();
}

void PDFDocumentSession::ensureRenderingInitialized() const
{
    if (m_renderingInitialized)
    {
        return;
    }

    const_cast<PDFDocumentSession*>(this)->initializeRendering();
}

void PDFDocumentSession::initializeRendering()
{
    if (m_renderingInitialized || !isValid())
    {
        return;
    }

    m_renderingInitialized = true;

    m_optionalContentActivity = std::make_unique<PDFOptionalContentActivity>(m_document, OCUsage::Export, nullptr);

    m_cmsManager = std::make_unique<PDFCMSManager>(nullptr);
    m_cmsManager->setDocument(m_document, m_processingBudget.get());
    m_cms = m_cmsManager->getCurrentCMS();

    m_fontCache = std::make_unique<PDFFontCache>(DEFAULT_FONT_CACHE_LIMIT, DEFAULT_REALIZED_FONT_CACHE_LIMIT);
    PDFModifiedDocument modifiedDocument(m_document, m_optionalContentActivity.get());
    m_fontCache->setDocument(modifiedDocument);
    m_fontCache->setCacheShrinkEnabled(nullptr, false);

    PDFMeshQualitySettings meshQualitySettings;
    m_renderer = std::make_unique<PDFRenderer>(m_document,
                                               m_fontCache.get(),
                                               m_cms.get(),
                                               m_optionalContentActivity.get(),
                                               m_features,
                                               meshQualitySettings,
                                               m_processingBudget.get());
}

}   // namespace pdf
