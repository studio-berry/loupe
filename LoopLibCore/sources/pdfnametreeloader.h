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

#ifndef PDFNAMETREELOADER_H
#define PDFNAMETREELOADER_H

#include "pdfdocument.h"

#include <map>
#include <functional>
#include <set>

namespace pdf
{

/// This class can load a number tree into the array
template <typename Type>
class PDFNameTreeLoader
{
public:
    explicit PDFNameTreeLoader() = delete;

    using MappedObjects = std::map<QByteArray, Type>;
    using LoadMethod = std::function<Type(const PDFObjectStorage*, const PDFObject&)>;

    /// Longest accepted key in a name tree. Keys are attacker-controlled strings
    /// that are stored verbatim in the document model (named destinations,
    /// embedded-file names, multimedia assets), and nothing downstream bounds
    /// them. A key past this length is not a name, it is a payload.
    static constexpr int MAXIMUM_NAME_LENGTH = 4096;

    /// Largest accepted number of entries across the whole tree. Bounds the
    /// "many small names" shape that the per-name cap alone does not.
    static constexpr size_t MAXIMUM_ENTRY_COUNT = 65536;

    /// Deepest accepted Kids nesting. Together with the visited-node set below
    /// this keeps a malformed tree from recursing without bound.
    static constexpr int MAXIMUM_TREE_DEPTH = 64;

    /// Parses the name tree and loads its items into the map. Some errors are ignored,
    /// e.g. when kid is null. Objects are retrieved by \p loadMethod.
    ///
    /// The tree is traversed defensively: a Kids chain that points back at a node
    /// it already visited (directly or through a cycle) is not followed a second
    /// time, nesting is bounded, and over-long or over-numerous keys are skipped.
    /// A malformed name tree therefore costs a truncated map rather than
    /// unbounded recursion or unbounded memory.
    /// \param storage Object storage
    /// \param root Root of the name tree
    /// \param loadMethod Parsing method, which retrieves parsed object
    static MappedObjects parse(const PDFObjectStorage* storage, const PDFObject& root, const LoadMethod& loadMethod)
    {
        MappedObjects result;
        std::set<PDFObjectReference> visitedNodes;
        parseImpl(result, storage, root, loadMethod, visitedNodes, 0);
        return result;
    }

private:
    static void parseImpl(MappedObjects& objects,
                          const PDFObjectStorage* storage,
                          const PDFObject& root,
                          const LoadMethod& loadMethod,
                          std::set<PDFObjectReference>& visitedNodes,
                          int depth)
    {
        if (depth > MAXIMUM_TREE_DEPTH)
        {
            return;
        }

        if (root.isReference() && !visitedNodes.insert(root.getReference()).second)
        {
            // Already expanded this node: the tree is cyclic.
            return;
        }

        if (const PDFDictionary* dictionary = storage->getDictionaryFromObject(root))
        {
            // Jakub Melka: First, load the objects into the map
            const PDFObject& namedItems = storage->getObject(dictionary->get("Names"));
            if (namedItems.isArray())
            {
                const PDFArray* namedItemsArray = namedItems.getArray();
                const size_t count = namedItemsArray->getCount() / 2;
                for (size_t i = 0; i < count; ++i)
                {
                    const size_t numberIndex = 2 * i;
                    const size_t valueIndex = 2 * i + 1;

                    const PDFObject& name = storage->getObject(namedItemsArray->getItem(numberIndex));
                    if (!name.isString())
                    {
                        continue;
                    }

                    const QByteArray key = name.getString();
                    if (key.size() > MAXIMUM_NAME_LENGTH)
                    {
                        continue;
                    }

                    if (objects.size() >= MAXIMUM_ENTRY_COUNT && !objects.count(key))
                    {
                        continue;
                    }

                    objects[key] = loadMethod(storage, namedItemsArray->getItem(valueIndex));
                }
            }

            // Then, follow the kids
            const PDFObject& kids = storage->getObject(dictionary->get("Kids"));
            if (kids.isArray())
            {
                const PDFArray* kidsArray = kids.getArray();
                const size_t count = kidsArray->getCount();
                for (size_t i = 0; i < count; ++i)
                {
                    parseImpl(objects, storage, kidsArray->getItem(i), loadMethod, visitedNodes, depth + 1);
                }
            }
        }
    }
};

}   // namespace pdf

#endif   // PDFNAMETREELOADER_H
