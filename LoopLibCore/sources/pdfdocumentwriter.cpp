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

#include "pdfdocumentwriter.h"
#include "pdfconstants.h"
#include "pdfwriteobjectvisitor_p.h"

#include <QFile>
#include <QBuffer>
#include <QCryptographicHash>
#include <QSaveFile>

#include "pdfdbgheap.h"

namespace pdf
{

namespace
{

bool dictionaryHasName(const PDFDocument* document, const PDFDictionary* dictionary, const char* key, const char* value);
bool isSignedDocument(const PDFDocument* document);
qint64 getPreviousXrefOffset(const QByteArray& data);

struct IncrementalXrefEntry
{
    PDFInteger objectNumber = 0;
    PDFInteger generation = 0;
    qint64 offset = 0;
    bool free = false;
};

}   // namespace

PDFOperationResult PDFDocumentWriter::write(const QString& fileName, const PDFDocument* document, bool safeWrite)
{
    Q_ASSERT(document);

    if (isOperationCancelled())
    {
        return tr("Operation cancelled.");
    }

    const PDFObjectStorage& storage = document->getStorage();
    if (!storage.getSecurityHandler()->isEncryptionAllowed())
    {
        return tr("Writing of encrypted documents is not supported.");
    }

    if (safeWrite)
    {
        QSaveFile file(fileName);
        file.setDirectWriteFallback(false);

        if (file.open(QFile::WriteOnly | QFile::Truncate))
        {
            PDFOperationResult result = write(&file, document);
            if (result)
            {
                if (!file.commit())
                {
                    return tr("File '%1' can't be opened for writing. %2").arg(fileName, file.errorString());
                }
            }
            else
            {
                file.cancelWriting();
            }
            return result;
        }
        else
        {
            return tr("File '%1' can't be opened for writing. %2").arg(fileName, file.errorString());
        }
    }
    else
    {
        QFile file(fileName);

        if (file.open(QFile::WriteOnly | QFile::Truncate))
        {
            PDFOperationResult result = write(&file, document);
            file.close();

            if (!result)
            {
                // If some error occured, then remove invalid file
                file.remove();
            }

            return result;
        }
        else
        {
            return tr("File '%1' can't be opened for writing. %2").arg(fileName, file.errorString());
        }
    }
}

PDFOperationResult PDFDocumentWriter::write(QIODevice* device, const PDFDocument* document)
{
    if (!document)
    {
        return tr("Document is null.");
    }

    if (isOperationCancelled())
    {
        return tr("Operation cancelled.");
    }

    if (!device->isWritable())
    {
        return tr("Device is not writable.");
    }

    const PDFObjectStorage& storage = document->getStorage();
    const PDFObjectStorage::PDFObjects& objects = storage.getObjects();
    const size_t objectCount = objects.size();
    const bool isEncrypted = storage.getSecurityHandler()->getMode() != EncryptionMode::None;
    if (!storage.getSecurityHandler()->isEncryptionAllowed())
    {
        return tr("Writing of encrypted documents is not supported.");
    }

    // Write header
    PDFVersion version = document->getInfo()->version;
    device->write(QString("%PDF-%1.%2").arg(version.major).arg(version.minor).toLatin1());
    writeCRLF(device);
    device->write("% PDF producer: ");
    device->write(PDF_LIBRARY_NAME);
    writeCRLF(device);
    writeCRLF(device);
    writeCRLF(device);

    PDFObjectReference encryptObjectReference;
    PDFObject encryptObject = document->getTrailerDictionary()->get("Encrypt");
    if (encryptObject.isReference())
    {
        encryptObjectReference = encryptObject.getReference();
    }

    // Write objects
    std::vector<PDFInteger> offsets(objectCount, -1);
    for (size_t i = 0; i < objectCount; ++i)
    {
        if (isOperationCancelled())
        {
            return tr("Operation cancelled.");
        }

        const PDFObjectStorage::Entry& entry = objects[i];
        if (entry.object.isNull())
        {
            continue;
        }

        // Jakub Melka: we must mark actual position of object
        offsets[i] = device->pos();

        if (isEncrypted)
        {
            PDFObjectReference reference(i, entry.generation);
            PDFObject objectToWrite = entry.object;

            if (reference != encryptObjectReference)
            {
                objectToWrite = storage.getSecurityHandler()->encryptObject(objectToWrite, reference);
            }

            PDFWriteObjectVisitor visitor(device, m_operationControl);
            writeObjectHeader(device, reference);
            objectToWrite.accept(&visitor);
            if (visitor.isCancelled())
            {
                return tr("Operation cancelled.");
            }
            writeObjectFooter(device);
        }
        else
        {
            PDFWriteObjectVisitor visitor(device, m_operationControl);
            writeObjectHeader(device, PDFObjectReference(i, entry.generation));
            entry.object.accept(&visitor);
            if (visitor.isCancelled())
            {
                return tr("Operation cancelled.");
            }
            writeObjectFooter(device);
        }
    }

    // Write cross-reference table
    PDFInteger xrefOffset = device->pos();
    device->write("xref");
    writeCRLF(device);
    device->write(QString("0 %1").arg(objectCount).toLatin1());
    writeCRLF(device);

    for (size_t i = 0; i < objectCount; ++i)
    {
        if (isOperationCancelled())
        {
            return tr("Operation cancelled.");
        }

        const PDFObjectStorage::Entry& entry = objects[i];
        PDFInteger generation = entry.generation;

        if (i == 0)
        {
            generation = 65535;
        }

        PDFInteger offset = offsets[i];
        if (offset == -1)
        {
            offset = 0;
        }

        QString offsetString = QString::number(offset).rightJustified(10, QChar('0'), true);
        QString generationString = QString::number(generation).rightJustified(5, QChar('0'), true);

        device->write(offsetString.toLatin1());
        device->write(" ");
        device->write(generationString.toLatin1());
        device->write(" ");
        device->write(entry.object.isNull() ? "f" : "n");
        writeCRLF(device);
    }

    // Jakub Melka: Adjust trailer dictionary, to be really dictionary, not a stream
    PDFDictionary trailerDictionary = *document->getTrailerDictionary();
    PDFDictionary newTrailerDictionary;

    for (const char* entry : { "Size", "Root", "Encrypt", "Info", "ID" })
    {
        PDFObject object = trailerDictionary.get(entry);
        if (!object.isNull())
        {
            newTrailerDictionary.addEntry(PDFInplaceOrMemoryString(entry), qMove(object));
        }
    }

    PDFObject trailerDictionaryObject = PDFObject::createDictionary(std::make_shared<PDFDictionary>(qMove(newTrailerDictionary)));

    device->write("trailer");
    writeCRLF(device);
    PDFWriteObjectVisitor trailerVisitor(device, m_operationControl);
    trailerDictionaryObject.accept(&trailerVisitor);
    if (trailerVisitor.isCancelled() || isOperationCancelled())
    {
        return tr("Operation cancelled.");
    }
    writeCRLF(device);
    device->write("startxref");
    writeCRLF(device);
    device->write(QString::number(xrefOffset).toLatin1());
    writeCRLF(device);

    // Write footer
    device->write("%%EOF");

    return true;
}

PDFOperationResult PDFDocumentWriter::writeIncremental(const QString& fileName,
                                                       const PDFDocument* originalDocument,
                                                       const PDFDocument* document,
                                                       bool safeWrite)
{
    return writeIncremental(fileName, originalDocument, document, safeWrite, nullptr);
}

PDFOperationResult PDFDocumentWriter::writeIncremental(const QString& fileName,
                                                       const PDFDocument* originalDocument,
                                                       const PDFDocument* document,
                                                       bool safeWrite,
                                                       IncrementalWriteOutcome* outcome)
{
    if (!originalDocument || !document)
    {
        return tr("Original and modified documents are required for an incremental save.");
    }

    QFile sourceFile(fileName);
    if (!sourceFile.open(QIODevice::ReadOnly))
    {
        return tr("Source file '%1' can't be opened for incremental save. %2").arg(fileName, sourceFile.errorString());
    }

    const QByteArray originalData = sourceFile.readAll();
    sourceFile.close();

    if (safeWrite)
    {
        QSaveFile targetFile(fileName);
        targetFile.setDirectWriteFallback(false);
        if (!targetFile.open(QFile::WriteOnly | QFile::Truncate))
        {
            return tr("File '%1' can't be opened for incremental save. %2").arg(fileName, targetFile.errorString());
        }

        // The nested write reports what it did on success, but a successful
        // write is not a successful save: commit() can still fail. Hold the
        // outcome locally and publish it only once the rename has landed.
        IncrementalWriteOutcome nestedOutcome = IncrementalWriteOutcome::CopiedUnchanged;
        const PDFOperationResult result = writeIncremental(&targetFile, originalData, originalDocument, document, &nestedOutcome);
        if (!result)
        {
            targetFile.cancelWriting();
            return result;
        }
        if (!targetFile.commit())
        {
            return tr("File '%1' can't be committed after incremental save. %2").arg(fileName, targetFile.errorString());
        }
        if (outcome)
        {
            *outcome = nestedOutcome;
        }
        return result;
    }

    QFile targetFile(fileName);
    if (!targetFile.open(QFile::WriteOnly | QFile::Truncate))
    {
        return tr("File '%1' can't be opened for incremental save. %2").arg(fileName, targetFile.errorString());
    }

    IncrementalWriteOutcome nestedOutcome = IncrementalWriteOutcome::CopiedUnchanged;
    const PDFOperationResult result = writeIncremental(&targetFile, originalData, originalDocument, document, &nestedOutcome);
    targetFile.close();
    if (result && outcome)
    {
        *outcome = nestedOutcome;
    }
    return result;
}

PDFOperationResult PDFDocumentWriter::writeIncremental(QIODevice* device,
                                                       const QByteArray& originalData,
                                                       const PDFDocument* originalDocument,
                                                       const PDFDocument* document)
{
    return writeIncremental(device, originalData, originalDocument, document, nullptr);
}

PDFOperationResult PDFDocumentWriter::writeIncremental(QIODevice* device,
                                                       const QByteArray& originalData,
                                                       const PDFDocument* originalDocument,
                                                       const PDFDocument* document,
                                                       IncrementalWriteOutcome* outcome)
{
    if (!device || !device->isWritable() || !originalDocument || !document)
    {
        return tr("A writable device and both original and modified documents are required for an incremental save.");
    }

    if (originalData.isEmpty() || !originalData.startsWith("%PDF-"))
    {
        return tr("The original PDF bytes are missing or invalid.");
    }

    const QByteArray sourceDataHash = originalDocument->getSourceDataHash();
    if (!sourceDataHash.isEmpty() && QCryptographicHash::hash(originalData, QCryptographicHash::Sha256) != sourceDataHash)
    {
        return tr("The source PDF changed before the incremental save; refusing to append to a different file.");
    }

    const qint64 previousXrefOffset = getPreviousXrefOffset(originalData);
    if (previousXrefOffset < 0)
    {
        return tr("The original PDF does not contain a readable startxref offset.");
    }

    const PDFObjectStorage::PDFObjects& originalObjects = originalDocument->getStorage().getObjects();
    const PDFObjectStorage::PDFObjects& modifiedObjects = document->getStorage().getObjects();
    if (modifiedObjects.size() < originalObjects.size())
    {
        return tr("The modified document removed object slots; a full rewrite is required.");
    }

    if (originalDocument->getStorage().getSecurityHandler()->getMode() != document->getStorage().getSecurityHandler()->getMode())
    {
        return tr("The encryption state changed; a full rewrite is required.");
    }

    std::vector<IncrementalXrefEntry> changedObjects;
    changedObjects.reserve(modifiedObjects.size());
    for (size_t i = 0; i < modifiedObjects.size(); ++i)
    {
        const bool changed = i >= originalObjects.size() || modifiedObjects[i] != originalObjects[i];
        if (!changed)
        {
            continue;
        }

        const PDFObjectStorage::Entry& entry = modifiedObjects[i];
        IncrementalXrefEntry xrefEntry;
        xrefEntry.objectNumber = static_cast<PDFInteger>(i);
        xrefEntry.generation = entry.generation;
        xrefEntry.free = entry.object.isNull();
        changedObjects.emplace_back(xrefEntry);
    }

    if (isSignedDocument(originalDocument))
    {
        for (const IncrementalXrefEntry& xrefEntry : changedObjects)
        {
            if (xrefEntry.free)
            {
                continue;
            }

            const PDFObject& object = document->getObjectByReference(PDFObjectReference(xrefEntry.objectNumber, xrefEntry.generation));
            if (object.isDictionary() && (dictionaryHasName(document, object.getDictionary(), "Type", "Sig") ||
                                          dictionaryHasName(document, object.getDictionary(), "FT", "Sig")))
            {
                return tr("The signature object changed; refusing an incremental save that cannot preserve signature coverage.");
            }
        }
    }

    if (changedObjects.empty())
    {
        // Nothing changed, so there is nothing to append. The bytes are copied
        // verbatim - which is the right output - but it is not an append, and a
        // caller that asked for one is told so through \p outcome.
        if (device->write(originalData) != originalData.size())
        {
            return PDFOperationResult(tr("Failed to copy the original PDF bytes."));
        }

        if (outcome)
        {
            *outcome = IncrementalWriteOutcome::CopiedUnchanged;
        }

        return PDFOperationResult(true);
    }

    if (device->write(originalData) != originalData.size())
    {
        return tr("Failed to copy the original PDF bytes before incremental save.");
    }

    if (!originalData.endsWith('\n') && !originalData.endsWith('\r'))
    {
        writeCRLF(device);
    }

    const bool isEncrypted = document->getStorage().getSecurityHandler()->getMode() != EncryptionMode::None;
    PDFObjectReference encryptObjectReference;
    const PDFObject encryptObject = document->getTrailerDictionary()->get("Encrypt");
    if (encryptObject.isReference())
    {
        encryptObjectReference = encryptObject.getReference();
    }

    for (IncrementalXrefEntry& xrefEntry : changedObjects)
    {
        const PDFObjectStorage::Entry& entry = modifiedObjects.at(static_cast<size_t>(xrefEntry.objectNumber));
        if (xrefEntry.free)
        {
            continue;
        }

        xrefEntry.offset = device->pos();
        if (xrefEntry.offset < 0 || xrefEntry.offset > 9999999999LL)
        {
            return tr("The incremental xref offset cannot be represented by a classic xref table.");
        }

        PDFObject objectToWrite = entry.object;
        const PDFObjectReference reference(xrefEntry.objectNumber, xrefEntry.generation);
        if (isEncrypted && reference != encryptObjectReference)
        {
            objectToWrite = document->getStorage().getSecurityHandler()->encryptObject(objectToWrite, reference);
        }

        writeObjectHeader(device, reference);
        device->write(getSerializedObject(objectToWrite));
        writeObjectFooter(device);
    }

    const qint64 xrefOffset = device->pos();
    if (xrefOffset < 0 || xrefOffset > 9999999999LL)
    {
        return tr("The incremental xref offset cannot be represented by a classic xref table.");
    }

    device->write("xref");
    writeCRLF(device);

    for (size_t i = 0; i < changedObjects.size();)
    {
        size_t end = i + 1;
        while (end < changedObjects.size() && changedObjects[end].objectNumber == changedObjects[end - 1].objectNumber + 1)
        {
            ++end;
        }

        device->write(QString("%1 %2").arg(changedObjects[i].objectNumber).arg(end - i).toLatin1());
        writeCRLF(device);

        for (; i < end; ++i)
        {
            const IncrementalXrefEntry& xrefEntry = changedObjects[i];
            const QString offset = QString::number(xrefEntry.free ? 0 : xrefEntry.offset).rightJustified(10, QChar('0'), true);
            const QString generation = QString::number(xrefEntry.generation).rightJustified(5, QChar('0'), true);
            device->write(offset.toLatin1());
            device->write(" ");
            device->write(generation.toLatin1());
            device->write(xrefEntry.free ? " f" : " n");
            writeCRLF(device);
        }
    }

    PDFDictionary trailer;
    const PDFDictionary* modifiedTrailer = document->getTrailerDictionary();
    for (const char* key : { "Root", "Encrypt", "Info", "ID" })
    {
        const PDFObject object = modifiedTrailer->get(key);
        if (!object.isNull())
        {
            trailer.addEntry(PDFInplaceOrMemoryString(key), PDFObject(object));
        }
    }
    trailer.setEntry(PDFInplaceOrMemoryString("Size"), PDFObject::createInteger(static_cast<PDFInteger>(modifiedObjects.size())));
    trailer.setEntry(PDFInplaceOrMemoryString("Prev"), PDFObject::createInteger(previousXrefOffset));

    device->write("trailer");
    writeCRLF(device);
    device->write(getSerializedObject(PDFObject::createDictionary(std::make_shared<PDFDictionary>(std::move(trailer)))));
    writeCRLF(device);
    device->write("startxref");
    writeCRLF(device);
    device->write(QString::number(xrefOffset).toLatin1());
    writeCRLF(device);
    device->write("%%EOF");

    if (outcome)
    {
        *outcome = IncrementalWriteOutcome::Appended;
    }

    return true;
}

PDFDocumentWriter::WriteMode PDFDocumentWriter::getRecommendedWriteMode(const PDFDocument* sourceDocument,
                                                                        bool requiresFullRewrite,
                                                                        bool saveAsNewOutput)
{
    return getRecommendedWriteMode(sourceDocument,
                                   requiresFullRewrite
                                       ? PDFOperationSavePolicy::fullRewrite(QStringLiteral("legacy full-rewrite request"))
                                       : PDFOperationSavePolicy::incrementalAppend(QStringLiteral("legacy incremental eligibility request")),
                                   saveAsNewOutput);
}

PDFDocumentWriter::WriteMode PDFDocumentWriter::getRecommendedWriteMode(const PDFDocument* sourceDocument,
                                                                        const PDFOperationSavePolicy& policy,
                                                                        bool saveAsNewOutput)
{
    if (policy.mode != PDFSaveMode::IncrementalAppend || saveAsNewOutput || !sourceDocument)
    {
        return WriteMode::FullRewrite;
    }

    if (isSignedDocument(sourceDocument) || sourceDocument->getTrailerDictionary()->hasKey("Prev"))
    {
        return WriteMode::Incremental;
    }

    return WriteMode::FullRewrite;
}

void PDFDocumentWriter::writeCRLF(QIODevice* device)
{
    device->write("\x0D\x0A");
}

void PDFDocumentWriter::writeObjectHeader(QIODevice* device, PDFObjectReference reference)
{
    QString objectHeader = QString("%1 %2 obj").arg(QString::number(reference.objectNumber), QString::number(reference.generation));
    device->write(objectHeader.toLatin1());
    writeCRLF(device);
}

void PDFDocumentWriter::writeObjectFooter(QIODevice* device)
{
    device->write("endobj");
    writeCRLF(device);
}

namespace
{

bool dictionaryHasName(const PDFDocument* document, const PDFDictionary* dictionary, const char* key, const char* value)
{
    if (!dictionary || !dictionary->hasKey(key))
    {
        return false;
    }

    const PDFObject& object = document->getObject(dictionary->get(key));
    return object.isName() && object.getString() == value;
}

bool isSignedDocument(const PDFDocument* document)
{
    if (!document)
    {
        return false;
    }

    for (const PDFObjectStorage::Entry& entry : document->getStorage().getObjects())
    {
        if (!entry.object.isDictionary())
        {
            continue;
        }

        const PDFDictionary* dictionary = entry.object.getDictionary();
        if (dictionaryHasName(document, dictionary, "Type", "Sig") ||
            dictionaryHasName(document, dictionary, "FT", "Sig"))
        {
            return true;
        }
    }

    return false;
}

qint64 getPreviousXrefOffset(const QByteArray& data)
{
    const QByteArray marker = QByteArrayLiteral("startxref");
    const qsizetype markerPosition = data.lastIndexOf(marker);
    if (markerPosition < 0)
    {
        return -1;
    }

    qsizetype position = markerPosition + marker.size();
    while (position < data.size() && (data.at(position) == '\r' || data.at(position) == '\n' || data.at(position) == ' ' || data.at(position) == '\t'))
    {
        ++position;
    }

    const qsizetype end = data.indexOf('\n', position);
    const QByteArray value = data.mid(position, end < 0 ? -1 : end - position).trimmed();
    bool ok = false;
    const qint64 result = value.toLongLong(&ok);
    return ok ? result : -1;
}

}   // namespace

class PDFSizeCounterIODevice : public QIODevice
{
public:
    explicit PDFSizeCounterIODevice(QObject* parent) :
        QIODevice(parent)
    {
    }

    virtual bool isSequential() const override;
    virtual bool open(OpenMode mode) override;
    virtual void close() override;
    virtual qint64 pos() const override;
    virtual qint64 size() const override;
    virtual bool seek(qint64 pos) override;
    virtual bool atEnd() const override;
    virtual bool reset() override;
    virtual qint64 bytesAvailable() const override;
    virtual qint64 bytesToWrite() const override;
    virtual bool canReadLine() const override;
    virtual bool waitForReadyRead(int msecs) override;
    virtual bool waitForBytesWritten(int msecs) override;

protected:
    virtual qint64 readData(char* data, qint64 maxlen) override;
    virtual qint64 readLineData(char* data, qint64 maxlen) override;
    virtual qint64 writeData(const char* data, qint64 len) override;

private:
    OpenMode m_openMode = NotOpen;
    qint64 m_fileSize = 0;
};

bool PDFSizeCounterIODevice::isSequential() const
{
    return true;
}

bool PDFSizeCounterIODevice::open(OpenMode mode)
{
    if (m_openMode == NotOpen)
    {
        setOpenMode(mode);
        return true;
    }
    else
    {
        return false;
    }
}

void PDFSizeCounterIODevice::close()
{
    setOpenMode(NotOpen);
}

qint64 PDFSizeCounterIODevice::pos() const
{
    return m_fileSize;
}

qint64 PDFSizeCounterIODevice::size() const
{
    return m_fileSize;
}

bool PDFSizeCounterIODevice::seek(qint64 pos)
{
    Q_UNUSED(pos);

    return false;
}

bool PDFSizeCounterIODevice::atEnd() const
{
    return true;
}

bool PDFSizeCounterIODevice::reset()
{
    return false;
}

qint64 PDFSizeCounterIODevice::bytesAvailable() const
{
    return 0;
}

qint64 PDFSizeCounterIODevice::bytesToWrite() const
{
    return 0;
}

bool PDFSizeCounterIODevice::canReadLine() const
{
    return false;
}

bool PDFSizeCounterIODevice::waitForReadyRead(int msecs)
{
    Q_UNUSED(msecs);

    return false;
}

bool PDFSizeCounterIODevice::waitForBytesWritten(int msecs)
{
    Q_UNUSED(msecs);

    return false;
}

qint64 PDFSizeCounterIODevice::readData(char* data, qint64 maxlen)
{
    Q_UNUSED(data);
    Q_UNUSED(maxlen);

    return 0;
}

qint64 PDFSizeCounterIODevice::readLineData(char* data, qint64 maxlen)
{
    Q_UNUSED(data);
    Q_UNUSED(maxlen);

    return 0;
}

qint64 PDFSizeCounterIODevice::writeData(const char* data, qint64 len)
{
    Q_UNUSED(data);

    m_fileSize += len;
    return len;
}

qint64 PDFDocumentWriter::getDocumentFileSize(const PDFDocument* document)
{
    PDFSizeCounterIODevice device(nullptr);
    PDFDocumentWriter writer(nullptr);

    device.open(QIODevice::WriteOnly);

    if (writer.write(&device, document))
    {
        device.close();
        return device.pos();
    }

    device.close();
    return -1;
}

qint64 PDFDocumentWriter::getObjectSize(const PDFDocument* document, PDFObjectReference reference)
{
    const PDFObject& object = document->getObjectByReference(reference);

    if (object.isNull())
    {
        return 0;
    }

    PDFSizeCounterIODevice device(nullptr);

    device.open(QIODevice::WriteOnly);

    PDFWriteObjectVisitor visitor(&device);
    writeObjectHeader(&device, reference);
    object.accept(&visitor);
    writeObjectFooter(&device);

    device.close();
    return device.pos();
}

QByteArray PDFDocumentWriter::getSerializedObject(const PDFObject& object)
{
    QBuffer buffer;

    if (buffer.open(QBuffer::WriteOnly))
    {
        PDFWriteObjectVisitor visitor(&buffer);
        object.accept(&visitor);

        buffer.close();
    }

    return buffer.data();
}

}   // namespace pdf
