/****************************************************************************
**
** Copyright (C) 2020 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the QtCore module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl-3.0.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or (at your option) the GNU General
** Public license version 3 or any later version approved by the KDE Free
** Qt Foundation. The licenses are as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL2 and LICENSE.GPL3
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-2.0.html and
** https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#ifndef QHASH_H
#define QHASH_H

#include <QtCore/qiterator.h>
#include <QtCore/qvector.h>
#include <QtCore/qrefcount.h>
#include <QtCore/qhashfunctions.h>
#include <QtCore/qcontainertools_impl.h>
#include <QtCore/qmath.h>

#include <initializer_list>

QT_BEGIN_NAMESPACE

struct QHashDummyValue
{
    bool operator==(const QHashDummyValue &) const noexcept { return true; }
};

namespace QHashPrivate {

// QHash uses a power of two growth policy.
namespace GrowthPolicy
{
inline constexpr size_t maxNumBuckets() noexcept
{
    return size_t(1) << (8*sizeof(size_t) - 1);
}
inline constexpr size_t bucketsForCapacity(size_t requestedCapacity) noexcept
{
    if (requestedCapacity <= 8)
        return 16;
    if (requestedCapacity >= maxNumBuckets())
        return maxNumBuckets();
    return qNextPowerOfTwo(QIntegerForSize<sizeof(size_t)>::Unsigned(2*requestedCapacity - 1));
}
inline constexpr size_t bucketForHash(size_t nBuckets, size_t hash) noexcept
{
    return hash & (nBuckets - 1);
}
}

template <typename Key, typename T>
struct Node
{
    using KeyType = Key;
    using ValueType = T;

    Key key;
    T value;
    static Node create(Key &&k, T &&t) noexcept(std::is_nothrow_move_assignable_v<Key> && std::is_nothrow_move_assignable_v<T>)
    {
        return Node{ std::move(k), std::move(t) };
    }
    static Node create(const Key &k, const T &t) noexcept(std::is_nothrow_copy_constructible_v<Key> && std::is_nothrow_copy_constructible_v<T>)
    {
        return Node{ k, t };
    }
    void replace(const T &t) noexcept(std::is_nothrow_assignable_v<T>)
    {
        value = t;
    }
    void replace(T &&t) noexcept(std::is_nothrow_move_assignable_v<T>)
    {
        value = std::move(t);
    }
    T &&takeValue() noexcept(std::is_nothrow_move_assignable_v<T>)
    {
        return std::move(value);
    }
};

template <typename T>
struct MultiNodeChain
{
    T value;
    MultiNodeChain *next = nullptr;
    ~MultiNodeChain()
    {
    }
    qsizetype free() noexcept(std::is_nothrow_destructible_v<T>)
    {
        qsizetype nEntries = 0;
        MultiNodeChain *e = this;
        while (e) {
            MultiNodeChain *n = e->next;
            ++nEntries;
            delete e;
            e = n;
        }
        return  nEntries;
    }
    bool contains(const T &val) const noexcept
    {
        const MultiNodeChain *e = this;
        while (e) {
            if (e->value == val)
                return true;
            e = e->next;
        }
        return false;
    }
};

template <typename Key, typename T>
struct MultiNode
{
    using KeyType = Key;
    using ValueType = T;
    using Chain = MultiNodeChain<T>;

    Key key;
    Chain *value;

    static MultiNode create(Key &&k, T &&t)
    {
        Chain *c = new Chain{ std::move(t), nullptr };
        return MultiNode(std::move(k), c);
    }
    static MultiNode create(const Key &k, const T &t)
    {
        Chain *c = new Chain{ t, nullptr };
        return MultiNode(k, c);
    }

    MultiNode(const Key &k, Chain *c)
        : key(k),
          value(c)
    {}
    MultiNode(Key &&k, Chain *c) noexcept(std::is_nothrow_move_assignable_v<Key>)
        : key(std::move(k)),
          value(c)
    {}

    MultiNode(MultiNode &&other)
        : key(other.key),
          value(other.value)
    {
        other.value = nullptr;
    }

    MultiNode(const MultiNode &other)
        : key(other.key)
    {
        Chain *c = other.value;
        Chain **e = &value;
        while (c) {
            Chain *chain = new Chain{ c->value, nullptr };
            *e = chain;
            e = &chain->next;
            c = c->next;
        }
    }
    ~MultiNode()
    {
        if (value)
            value->free();
    }
    static qsizetype freeChain(MultiNode *n) noexcept(std::is_nothrow_destructible_v<T>)
    {
        qsizetype size = n->value->free();
        n->value = nullptr;
        return size;
    }
    void replace(const T &t) noexcept(std::is_nothrow_assignable_v<T, T>)
    {
        value->value = t;
    }
    void replace(T &&t) noexcept(std::is_nothrow_move_assignable_v<T>)
    {
        value->value = std::move(t);
    }
    void insertMulti(const T &t)
    {
        Chain *e = new Chain{ t, nullptr };
        e->next = value;
        value = e;
    }

    // compiler generated move operators are fine
};

template<typename  Node>
constexpr bool isRelocatable()
{
    return QTypeInfo<typename Node::KeyType>::isRelocatable && QTypeInfo<typename Node::ValueType>::isRelocatable;
}

// Regular hash tables consist of a list of buckets that can store Nodes. But simply allocating one large array of buckets
// would waste a lot of memory. To avoid this, we split the vector of buckets up into a vector of Spans. Each Span represents
// NEntries buckets. To quickly find the correct Span that holds a bucket, NEntries must be a power of two.
//
// Inside each Span, there is an offset array that represents the actual buckets. offsets contains either an index into the
// actual storage space for the Nodes (the 'entries' member) or 0xff (UnusedEntry) to flag that the bucket is empty.
// As we have only 128 entries per Span, the offset array can be represented using an unsigned char. This trick makes the hash
// table have a very small memory overhead compared to many other implementations.
template<typename Node>
struct Span {
    enum {
        NEntries = 128,
        LocalBucketMask = (NEntries - 1),
        UnusedEntry = 0xff
    };
    static_assert ((NEntries & LocalBucketMask) == 0, "EntriesPerSpan must be a power of two.");

    // Entry is a slot available for storing a Node. The Span holds a pointer to
    // an array of Entries. Upon construction of the array, those entries are
    // unused, and nextFree() is being used to set up a singly linked list
    // of free entries.
    // When a node gets inserted, the first free entry is being picked, removed
    // from the singly linked list and the Node gets constructed in place.
    struct Entry {
        typename std::aligned_storage<sizeof(Node), alignof(Node)>::type storage;

        unsigned char &nextFree() { return *reinterpret_cast<unsigned char *>(&storage); }
        Node &node() { return *reinterpret_cast<Node *>(&storage); }
    };

    unsigned char offsets[NEntries];
    Entry *entries = nullptr;
    unsigned char allocated = 0;
    unsigned char nextFree = 0;
    Span() noexcept
    {
        memset(offsets, UnusedEntry, sizeof(offsets));
    }
    ~Span()
    {
        freeData();
    }
    void freeData() noexcept(std::is_nothrow_destructible<Node>::value)
    {
        if (entries) {
            if constexpr (!std::is_trivially_destructible<Node>::value) {
                for (auto o : offsets) {
                    if (o != UnusedEntry)
                        entries[o].node().~Node();
                }
            }
            delete [] entries;
            entries = nullptr;
        }
    }
    void insert(size_t i, Node &&n)
    {
        Q_ASSERT(i <= NEntries);
        Q_ASSERT(offsets[i] == UnusedEntry);
        if (nextFree == allocated)
            addStorage();
        unsigned char entry = nextFree;
        Q_ASSERT(entry < allocated);
        nextFree = entries[entry].nextFree();
        offsets[i] = entry;
        new (&entries[entry].node()) Node(std::move(n));
    }
    void erase(size_t bucket) noexcept(std::is_nothrow_destructible<Node>::value)
    {
        Q_ASSERT(bucket <= NEntries);
        Q_ASSERT(offsets[bucket] != UnusedEntry);

        unsigned char entry = offsets[bucket];
        offsets[bucket] = UnusedEntry;

        entries[entry].node().~Node();
        entries[entry].nextFree() = nextFree;
        nextFree = entry;
    }
    size_t offset(size_t i) const noexcept
    {
        return offsets[i];
    }
    bool hasNode(size_t i) const noexcept
    {
        return (offsets[i] != UnusedEntry);
    }
    Node &at(size_t i) noexcept
    {
        Q_ASSERT(i <= NEntries);
        Q_ASSERT(offsets[i] != UnusedEntry);

        return entries[offsets[i]].node();
    }
    const Node &at(size_t i) const noexcept
    {
        Q_ASSERT(i <= NEntries);
        Q_ASSERT(offsets[i] != UnusedEntry);

        return entries[offsets[i]].node();
    }
    Node &atOffset(size_t o) noexcept
    {
        Q_ASSERT(o < allocated);

        return entries[o].node();
    }
    const Node &atOffset(size_t o) const noexcept
    {
        Q_ASSERT(o < allocated);

        return entries[o].node();
    }
    void moveLocal(size_t from, size_t to) noexcept
    {
        Q_ASSERT(offsets[from] != UnusedEntry);
        Q_ASSERT(offsets[to] == UnusedEntry);
        offsets[to] = offsets[from];
        offsets[from] = UnusedEntry;
    }
    void moveFromSpan(Span &fromSpan, size_t fromIndex, size_t to) noexcept(std::is_nothrow_move_constructible_v<Node>)
    {
        Q_ASSERT(to <= NEntries);
        Q_ASSERT(offsets[to] == UnusedEntry);
        Q_ASSERT(fromIndex <= NEntries);
        Q_ASSERT(fromSpan.offsets[fromIndex] != UnusedEntry);
        if (nextFree == allocated)
            addStorage();
        Q_ASSERT(nextFree < allocated);
        offsets[to] = nextFree;
        Entry &toEntry = entries[nextFree];
        nextFree = toEntry.nextFree();

        size_t fromOffset = fromSpan.offsets[fromIndex];
        fromSpan.offsets[fromIndex] = UnusedEntry;
        Entry &fromEntry = fromSpan.entries[fromOffset];

        if constexpr (isRelocatable<Node>()) {
            memcpy(&toEntry, &fromEntry, sizeof(Entry));
        } else {
            new (&toEntry.node()) Node(std::move(fromEntry.node()));
            fromEntry.node().~Node();
        }
        fromEntry.nextFree() = fromSpan.nextFree;
        fromSpan.nextFree = static_cast<unsigned char>(fromOffset);
    }

    void addStorage()
    {
        Q_ASSERT(allocated < NEntries);
        Q_ASSERT(nextFree == allocated);
        // the hash table should always be between 25 and 50% full
        // this implies that we on average have between 32 and 64 entries
        // in here. The likelihood of having below 16 entries is very small,
        // so start with that and increment by 16 each time we need to add
        // some more space
        const size_t increment = NEntries/8;
        size_t alloc = allocated + increment;
        Entry *newEntries = new Entry[alloc];
        // we only add storage if the previous storage was fully filled, so
        // simply copy the old data over
        if constexpr (isRelocatable<Node>()) {
            memcpy(newEntries, entries, allocated*sizeof(Entry));
        } else {
            for (size_t i = 0; i < allocated; ++i) {
                new (&newEntries[i].node()) Node(std::move(entries[i].node()));
                entries[i].node().~Node();
            }
        }
        for (size_t i = allocated; i < allocated + increment; ++i) {
            newEntries[i].nextFree() = uchar(i + 1);
        }
        delete [] entries;
        entries = newEntries;
        allocated = uchar(alloc);
    }
};

template <typename Node>
struct iterator;

template <typename Node>
struct Data
{
    using Key = typename Node::KeyType;
    using T = typename Node::ValueType;
    using Span = QHashPrivate::Span<Node>;
    using iterator = QHashPrivate::iterator<Node>;

    QtPrivate::RefCount ref = {{1}};
    size_t size = 0;
    size_t numBuckets = 0;
    size_t seed = 0;


    Span *spans = nullptr;

    Data(size_t reserve = 0)
    {
        numBuckets = GrowthPolicy::bucketsForCapacity(reserve);
        size_t nSpans = (numBuckets + Span::LocalBucketMask) / Span::NEntries;
        spans = new Span[nSpans];
        seed = qGlobalQHashSeed();
    }
    Data(const Data &other)
        : size(other.size),
          numBuckets(other.numBuckets),
          seed(other.seed)
    {
        size_t nSpans = (other.numBuckets + Span::LocalBucketMask) / Span::NEntries;
        spans = new Span[nSpans];

        for (size_t s = 0; s < nSpans; ++s) {
            const Span &span = other.spans[s];
            for (size_t index = 0; index < Span::NEntries; ++index) {
                if (!span.hasNode(index))
                    continue;
                const Node &n = span.at(index);
                iterator it{ this, s*Span::NEntries + index };
                Q_ASSERT(it.isUnused());

                spans[it.span()].insert(it.index(), std::move(Node(n)));
            }
        }
    }
    Data(const Data &other, size_t reserved)
        : size(other.size),
          seed(other.seed)
    {
        numBuckets = GrowthPolicy::bucketsForCapacity(qMax(size, reserved));
        size_t nSpans = (numBuckets + Span::LocalBucketMask) / Span::NEntries;
        spans = new Span[nSpans];

        for (size_t s = 0; s < nSpans; ++s) {
            const Span &span = other.spans[s];
            for (size_t index = 0; index < Span::NEntries; ++index) {
                if (!span.hasNode(index))
                    continue;
                const Node &n = span.at(index);
                iterator it = find(n.key);
                Q_ASSERT(it.isUnused());
                spans[it.span()].insert(it.index(), std::move(Node(n)));
            }
        }
    }

    static Data *detached(Data *d)
    {
        if (!d)
            return new Data;
        Data *dd = new Data(*d);
        if (!d->ref.deref())
            delete d;
        return dd;
    }
    static Data *detached(Data *d, size_t size)
    {
        if (!d)
            return new Data(size);
        Data *dd = new Data(*d, size);
        if (!d->ref.deref())
            delete d;
        return dd;
    }


    void clear()
    {
        delete [] spans;
        spans = nullptr;
        size = 0;
        numBuckets = 0;
    }

    iterator detachedIterator(iterator other) const noexcept
    {
        return iterator{this, other.bucket};
    }

    iterator begin() const noexcept
    {
        iterator it{ this, 0 };
        if (it.isUnused())
            ++it;
        return it;
    }

    constexpr iterator end() const noexcept
    {
        return iterator();
    }

    void rehash(size_t sizeHint = 0)
    {
        if (sizeHint == 0)
            sizeHint = size;
        size_t newBucketCount = GrowthPolicy::bucketsForCapacity(sizeHint);

        Span *oldSpans = spans;
        size_t oldBucketCount = numBuckets;
        size_t nSpans = (newBucketCount + Span::LocalBucketMask) / Span::NEntries;
        spans = new Span[nSpans];
        numBuckets = newBucketCount;
        size_t oldNSpans = (oldBucketCount + Span::LocalBucketMask) / Span::NEntries;

        for (size_t s = 0; s < oldNSpans; ++s) {
            Span &span = oldSpans[s];
            for (size_t index = 0; index < Span::NEntries; ++index) {
                if (!span.hasNode(index))
                    continue;
                Node &n = span.at(index);
                iterator it = find(n.key);
                Q_ASSERT(it.isUnused());
                spans[it.span()].insert(it.index(), std::move(n));
            }
            span.freeData();
        }
        delete [] oldSpans;
    }

    size_t nextBucket(size_t bucket) const noexcept
    {
        ++bucket;
        if (bucket == numBuckets)
            bucket = 0;
        return bucket;
    }

    float loadFactor() const noexcept
    {
        return float(size)/numBuckets;
    }
    bool shouldGrow() const noexcept
    {
        return size >= (numBuckets >> 1);
    }

    iterator find(const Key &key) const noexcept
    {
        Q_ASSERT(numBuckets > 0);
        size_t hash = qHash(key, seed);
        size_t bucket = GrowthPolicy::bucketForHash(numBuckets, hash);
        // loop over the buckets until we find the entry we search for
        // or an empty slot, in which case we know the entry doesn't exist
        while (true) {
            // Split the bucket into the indexex of span array, and the local
            // offset inside the span
            size_t span = bucket / Span::NEntries;
            size_t index = bucket & Span::LocalBucketMask;
            Span &s = spans[span];
            size_t offset = s.offset(index);
            if (offset == Span::UnusedEntry) {
                return iterator{ this, bucket };
            } else {
                Node &n = s.atOffset(offset);
                if (n.key == key)
                    return iterator{ this, bucket };
            }
            bucket = nextBucket(bucket);
        }
    }

    Node *findNode(const Key &key) const noexcept
    {
        if (!size)
            return nullptr;
        iterator it = find(key);
        if (it.isUnused())
            return nullptr;
        return it.node();
    }

    Node *findAndInsertNode(const Key &key) noexcept
    {
        if (shouldGrow())
            rehash(size + 1);
        iterator it = find(key);
        if (it.isUnused()) {
            spans[it.span()].insert(it.index(), Node::create(key, T()));
            ++size;
        }
        return it.node();
    }


    iterator insert(Node &&n)
    {
        if (shouldGrow())
            rehash(size + 1);
        iterator it = find(n.key);
        if (it.isUnused()) {
            spans[it.span()].insert(it.index(), std::move(n));
            ++size;
        } else {
            it.node()->replace(std::move(n.takeValue()));
        }
        return it;
    }

    iterator insert(const Key &key, const T &value)
    {
        if (shouldGrow())
            rehash(size + 1);
        auto it = find(key);
        if (it.isUnused()) {
            spans[it.span()].insert(it.index(), Node::create(key, value));
            ++size;
        } else {
            it.node()->replace(value);
        }
        return it;
    }

    iterator insertMulti(const Key &key, const T &value)
    {
        if (shouldGrow())
            rehash(size + 1);
        auto it = find(key);
        if (it.isUnused()) {
            spans[it.span()].insert(it.index(), std::move(Node::create(key, value)));
            ++size;
        } else {
            it.node()->insertMulti(value);
        }
        return it;
    }


    iterator erase(iterator it) noexcept(std::is_nothrow_destructible<Node>::value)
    {
        size_t bucket = it.bucket;
        size_t span = bucket / Span::NEntries;
        size_t index = bucket & Span::LocalBucketMask;
        Q_ASSERT(spans[span].hasNode(index));
        spans[span].erase(index);
        --size;

        // re-insert the following entries to avoid holes
        size_t hole = bucket;
        size_t next = bucket;
        while (true) {
            next = nextBucket(next);
            size_t nextSpan = next / Span::NEntries;
            size_t nextIndex = next & Span::LocalBucketMask;
            if (!spans[nextSpan].hasNode(nextIndex))
                break;
            size_t hash = qHash(spans[nextSpan].at(nextIndex).key, seed);
            size_t newBucket = GrowthPolicy::bucketForHash(numBuckets, hash);
            while (true) {
                if (newBucket == next) {
                    // nothing to do, item is at the right plae
                    break;
                } else if (newBucket == hole) {
                    // move into hole
                    size_t holeSpan = hole / Span::NEntries;
                    size_t holeIndex = hole & Span::LocalBucketMask;
                    if (nextSpan == holeSpan) {
                        spans[holeSpan].moveLocal(nextIndex, holeIndex);
                    } else {
                        // move between spans, more expensive
                        spans[holeSpan].moveFromSpan(spans[nextSpan], nextIndex, holeIndex);
                    }
                    hole = next;
                    break;
                }
                newBucket = nextBucket(newBucket);
            }
        }

        // return correct position of the next element
        if (!spans[span].hasNode(index))
            ++it;
        return it;
    }

    ~Data()
    {
        delete [] spans;
    }
};

template <typename Node>
struct iterator {
    using Span = QHashPrivate::Span<Node>;

    const Data<Node> *d = nullptr;
    size_t bucket = 0;

    size_t span() const noexcept { return bucket / Span::NEntries; }
    size_t index() const noexcept { return bucket & Span::LocalBucketMask; }
    inline bool isUnused() const noexcept { return !d->spans[span()].hasNode(index()); }

    inline Node *node() const noexcept
    {
        Q_ASSERT(!isUnused());
        return &d->spans[span()].at(index());
    }
    bool atEnd() const noexcept { return !d; }

    iterator operator++() noexcept
    {
        while (true) {
            ++bucket;
            if (bucket == d->numBuckets) {
                d = nullptr;
                bucket = 0;
                break;
            }
            if (!isUnused())
                break;
        }
        return *this;
    }
    bool operator==(iterator other) const noexcept
    { return d == other.d && bucket == other.bucket; }
    bool operator!=(iterator other) const noexcept
    { return !(*this == other); }
};



} // namespace QHashPrivate

template <class Key, class T>
class QHash
{
    using Node = QHashPrivate::Node<Key, T>;
    using Data = QHashPrivate::Data<Node>;
    friend class QSet<Key>;

    Data *d = nullptr;

public:
    using key_type = Key;
    using mapped_type = T;
    using value_type = T;
    using size_type = qsizetype;
    using difference_type = qsizetype;
    using reference = T &;
    using const_reference = const T &;

    inline QHash() noexcept = default;
    inline QHash(std::initializer_list<std::pair<Key,T> > list)
        : d(new Data(list.size()))
    {
        for (typename std::initializer_list<std::pair<Key,T> >::const_iterator it = list.begin(); it != list.end(); ++it)
            insert(it->first, it->second);
    }
    QHash(const QHash &other) noexcept
        : d(other.d)
    {
        if (d)
            d->ref.ref();
    }
    ~QHash()
    {
        if (d && !d->ref.deref())
            delete d;
    }

    QHash &operator=(const QHash &other) noexcept(std::is_nothrow_destructible<Node>::value)
    {
        if (d != other.d) {
            Data *o = other.d;
            if (o)
                o->ref.ref();
            if (d && !d->ref.deref())
                delete d;
            d = o;
        }
        return *this;
    }

    QHash(QHash &&other) noexcept
        : d(std::exchange(other.d, nullptr))
    {
    }
    QHash &operator=(QHash &&other) noexcept(std::is_nothrow_destructible<Node>::value)
    {
        if (d != other.d) {
            if (d && !d->ref.deref())
                delete d;
            d = std::exchange(other.d, nullptr);
        }
        return *this;
    }
#ifdef Q_QDOC
    template <typename InputIterator>
    QHash(InputIterator f, InputIterator l);
#else
    template <typename InputIterator, QtPrivate::IfAssociativeIteratorHasKeyAndValue<InputIterator> = true>
    QHash(InputIterator f, InputIterator l)
        : QHash()
    {
        QtPrivate::reserveIfForwardIterator(this, f, l);
        for (; f != l; ++f)
            insert(f.key(), f.value());
    }

    template <typename InputIterator, QtPrivate::IfAssociativeIteratorHasFirstAndSecond<InputIterator> = true>
    QHash(InputIterator f, InputIterator l)
        : QHash()
    {
        QtPrivate::reserveIfForwardIterator(this, f, l);
        for (; f != l; ++f)
            insert(f->first, f->second);
    }
#endif
    void swap(QHash &other) noexcept { qSwap(d, other.d); }

    bool operator==(const QHash &other) const noexcept
    {
        if (d == other.d)
            return true;
        if (size() != other.size())
            return false;

        for (const_iterator it = other.begin(); it != other.end(); ++it) {
            const_iterator i = find(it.key());
            if (i == end() || !(i.value() == it.value()))
                return false;
        }
        // all values must be the same as size is the same
        return true;
    }
    bool operator!=(const QHash &other) const noexcept { return !(*this == other); }

    inline qsizetype size() const noexcept { return d ? qsizetype(d->size) : 0; }
    inline bool isEmpty() const noexcept { return !d || d->size == 0; }

    inline qsizetype capacity() const noexcept { return d ? qsizetype(d->numBuckets >> 1) : 0; }
    void reserve(qsizetype size)
    {
        if (isDetached())
            d->rehash(size);
        else
            d = Data::detached(d, size_t(size));
    }
    inline void squeeze() { reserve(0); }

    inline void detach() { if (!d || d->ref.isShared()) d = Data::detached(d); }
    inline bool isDetached() const noexcept { return d && !d->ref.isShared(); }
    bool isSharedWith(const QHash &other) const noexcept { return d == other.d; }

    void clear() noexcept(std::is_nothrow_destructible<Node>::value)
    {
        if (d && !d->ref.deref())
            delete d;
        d = nullptr;
    }

    bool remove(const Key &key)
    {
        if (isEmpty()) // prevents detaching shared null
            return false;
        detach();

        auto it = d->find(key);
        if (it.isUnused())
            return false;
        d->erase(it);
        return true;
    }
    T take(const Key &key)
    {
        if (isEmpty()) // prevents detaching shared null
            return T();
        detach();

        auto it = d->find(key);
        if (it.isUnused())
            return T();
        T value = it.node()->takeValue();
        d->erase(it);
        return value;
    }

    bool contains(const Key &key) const noexcept
    {
        if (!d)
            return false;
        return d->findNode(key) != nullptr;
    }
    qsizetype count(const Key &key) const noexcept
    {
        return contains(key) ? 1 : 0;
    }

    Key key(const T &value, const Key &defaultKey = Key()) const noexcept
    {
        if (d) {
            const_iterator i = begin();
            while (i != end()) {
                if (i.value() == value)
                    return i.key();
                ++i;
            }
        }

        return defaultKey;
    }
    T value(const Key &key, const T &defaultValue = T()) const noexcept
    {
        if (d) {
            Node *n = d->findNode(key);
            if (n)
                return n->value;
        }
        return defaultValue;
    }
    T &operator[](const Key &key)
    {
        detach();
        Node *n = d->findAndInsertNode(key);
        Q_ASSERT(n);
        return n->value;
    }

    const T operator[](const Key &key) const noexcept
    {
        return value(key);
    }

    QVector<Key> keys() const
    {
        return QVector<Key>(keyBegin(), keyEnd());
    }
    QVector<Key> keys(const T &value) const
    {
        QVector<Key> res;
        const_iterator i = begin();
        while (i != end()) {
            if (i.value() == value)
                res.append(i.key());
            ++i;
        }
        return res;
    }
    QVector<T> values() const
    {
        return QVector<T>(begin(), end());
    }

    class const_iterator;

    class iterator
    {
        using piter = typename QHashPrivate::iterator<Node>;
        friend class const_iterator;
        friend class QHash<Key, T>;
        friend class QSet<Key>;
        piter i;
        explicit inline iterator(piter it) noexcept : i(it) { }

    public:
        typedef std::forward_iterator_tag iterator_category;
        typedef qptrdiff difference_type;
        typedef T value_type;
        typedef T *pointer;
        typedef T &reference;

        constexpr iterator() noexcept = default;

        inline const Key &key() const noexcept { return i.node()->key; }
        inline T &value() const noexcept { return i.node()->value; }
        inline T &operator*() const noexcept { return i.node()->value; }
        inline T *operator->() const noexcept { return &i.node()->value; }
        inline bool operator==(const iterator &o) const noexcept { return i == o.i; }
        inline bool operator!=(const iterator &o) const noexcept { return i != o.i; }

        inline iterator &operator++() noexcept
        {
            ++i;
            return *this;
        }
        inline iterator operator++(int) noexcept
        {
            iterator r = *this;
            ++i;
            return r;
        }

        inline bool operator==(const const_iterator &o) const noexcept { return i == o.i; }
        inline bool operator!=(const const_iterator &o) const noexcept { return i != o.i; }
    };
    friend class iterator;

    class const_iterator
    {
        using piter = typename QHashPrivate::iterator<Node>;
        friend class iterator;
        friend class QHash<Key, T>;
        friend class QSet<Key>;
        piter i;
        explicit inline const_iterator(piter it) : i(it) { }

    public:
        typedef std::forward_iterator_tag iterator_category;
        typedef qptrdiff difference_type;
        typedef T value_type;
        typedef const T *pointer;
        typedef const T &reference;

        constexpr const_iterator() noexcept = default;
        inline const_iterator(const iterator &o) noexcept : i(o.i) { }

        inline const Key &key() const noexcept { return i.node()->key; }
        inline const T &value() const noexcept { return i.node()->value; }
        inline const T &operator*() const noexcept { return i.node()->value; }
        inline const T *operator->() const noexcept { return &i.node()->value; }
        inline bool operator==(const const_iterator &o) const noexcept { return i == o.i; }
        inline bool operator!=(const const_iterator &o) const noexcept { return i != o.i; }

        inline const_iterator &operator++() noexcept
        {
            ++i;
            return *this;
        }
        inline const_iterator operator++(int) noexcept
        {
            const_iterator r = *this;
            ++i;
            return r;
        }
    };
    friend class const_iterator;

    class key_iterator
    {
        const_iterator i;

    public:
        typedef typename const_iterator::iterator_category iterator_category;
        typedef qptrdiff difference_type;
        typedef Key value_type;
        typedef const Key *pointer;
        typedef const Key &reference;

        key_iterator() noexcept = default;
        explicit key_iterator(const_iterator o) noexcept : i(o) { }

        const Key &operator*() const noexcept { return i.key(); }
        const Key *operator->() const noexcept { return &i.key(); }
        bool operator==(key_iterator o) const noexcept { return i == o.i; }
        bool operator!=(key_iterator o) const noexcept { return i != o.i; }

        inline key_iterator &operator++() noexcept { ++i; return *this; }
        inline key_iterator operator++(int) noexcept { return key_iterator(i++);}
        const_iterator base() const noexcept { return i; }
    };

    typedef QKeyValueIterator<const Key&, const T&, const_iterator> const_key_value_iterator;
    typedef QKeyValueIterator<const Key&, T&, iterator> key_value_iterator;

    // STL style
    inline iterator begin() { detach(); return iterator(d->begin()); }
    inline const_iterator begin() const noexcept { return d ? const_iterator(d->begin()): const_iterator(); }
    inline const_iterator cbegin() const noexcept { return d ? const_iterator(d->begin()): const_iterator(); }
    inline const_iterator constBegin() const noexcept { return d ? const_iterator(d->begin()): const_iterator(); }
    inline iterator end() noexcept { return iterator(); }
    inline const_iterator end() const noexcept { return const_iterator(); }
    inline const_iterator cend() const noexcept { return const_iterator(); }
    inline const_iterator constEnd() const noexcept { return const_iterator(); }
    inline key_iterator keyBegin() const noexcept { return key_iterator(begin()); }
    inline key_iterator keyEnd() const noexcept { return key_iterator(end()); }
    inline key_value_iterator keyValueBegin() { return key_value_iterator(begin()); }
    inline key_value_iterator keyValueEnd() { return key_value_iterator(end()); }
    inline const_key_value_iterator keyValueBegin() const noexcept { return const_key_value_iterator(begin()); }
    inline const_key_value_iterator constKeyValueBegin() const noexcept { return const_key_value_iterator(begin()); }
    inline const_key_value_iterator keyValueEnd() const noexcept { return const_key_value_iterator(end()); }
    inline const_key_value_iterator constKeyValueEnd() const noexcept { return const_key_value_iterator(end()); }

    iterator erase(const_iterator it)
    {
        Q_ASSERT(it != constEnd());
        detach();
        // ensure a valid iterator across the detach:
        iterator i = iterator{d->detachedIterator(it.i)};

        i.i = d->erase(i.i);
        return i;
    }

    QPair<iterator, iterator> equal_range(const Key &key)
    {
        auto first = find(key);
        auto second = first;
        if (second != iterator())
            ++second;
        return qMakePair(first, second);
    }

    QPair<const_iterator, const_iterator> equal_range(const Key &key) const noexcept
    {
        auto first = find(key);
        auto second = first;
        if (second != iterator())
            ++second;
        return qMakePair(first, second);
    }

    typedef iterator Iterator;
    typedef const_iterator ConstIterator;
    inline qsizetype count() const noexcept { return d ? qsizetype(d->size) : 0; }
    iterator find(const Key &key)
    {
        if (isEmpty()) // prevents detaching shared null
            return end();
        detach();
        auto it = d->find(key);
        if (it.isUnused())
            it = d->end();
        return iterator(it);
    }
    const_iterator find(const Key &key) const noexcept
    {
        if (isEmpty())
            return end();
        auto it = d->find(key);
        if (it.isUnused())
            it = d->end();
        return const_iterator(it);
    }
    const_iterator constFind(const Key &key) const noexcept
    {
        return find(key);
    }
    iterator insert(const Key &key, const T &value)
    {
        detach();

        auto i = d->insert(Node{key, value});
        return iterator(i);
    }
    void insert(const QHash &hash)
    {
        if (d == hash.d || !hash.d)
            return;
        if (!d) {
            *this = hash;
            return;
        }

        detach();

        for (auto it = hash.begin(); it != hash.end(); ++it)
            insert(it.key(), it.value());
    }

    float load_factor() const noexcept { return d ? d->loadFactor() : 0; }
    static float max_load_factor() noexcept { return 0.5; }
    size_t bucket_count() const noexcept { return d ? d->numBuckets : 0; }
    static size_t max_bucket_count() noexcept { return QHashPrivate::GrowthPolicy::maxNumBuckets(); }

    inline bool empty() const noexcept { return isEmpty(); }
};



template <class Key, class T>
class QMultiHash
{
    using Node = QHashPrivate::MultiNode<Key, T>;
    using Data = QHashPrivate::Data<Node>;
    using Chain = QHashPrivate::MultiNodeChain<T>;

    Data  *d = nullptr;
    qsizetype m_size = 0;

public:
    using key_type = Key;
    using mapped_type = T;
    using value_type = T;
    using size_type = qsizetype;
    using difference_type = qsizetype;
    using reference = T &;
    using const_reference = const T &;

    QMultiHash() noexcept = default;
    inline QMultiHash(std::initializer_list<std::pair<Key,T> > list)
        : d(new Data(list.size()))
    {
        for (typename std::initializer_list<std::pair<Key,T> >::const_iterator it = list.begin(); it != list.end(); ++it)
            insert(it->first, it->second);
    }
#ifdef Q_QDOC
    template <typename InputIterator>
    QMultiHash(InputIterator f, InputIterator l);
#else
    template <typename InputIterator, QtPrivate::IfAssociativeIteratorHasKeyAndValue<InputIterator> = true>
    QMultiHash(InputIterator f, InputIterator l)
    {
        QtPrivate::reserveIfForwardIterator(this, f, l);
        for (; f != l; ++f)
            insert(f.key(), f.value());
    }

    template <typename InputIterator, QtPrivate::IfAssociativeIteratorHasFirstAndSecond<InputIterator> = true>
    QMultiHash(InputIterator f, InputIterator l)
    {
        QtPrivate::reserveIfForwardIterator(this, f, l);
        for (; f != l; ++f)
            insert(f->first, f->second);
    }
#endif
    QMultiHash(const QMultiHash &other) noexcept
        : d(other.d), m_size(other.m_size)
    {
        if (d)
            d->ref.ref();
    }
    ~QMultiHash()
    {
        if (d && !d->ref.deref())
            delete d;
    }

    QMultiHash &operator=(const QMultiHash &other) noexcept(std::is_nothrow_destructible<Node>::value)
    {
        if (d != other.d) {
            Data *o = other.d;
            if (o)
                o->ref.ref();
            if (d && !d->ref.deref())
                delete d;
            d = o;
            m_size = other.m_size;
        }
        return *this;
    }
    QMultiHash(QMultiHash &&other) noexcept : d(other.d), m_size(other.m_size)
    {
        other.d = nullptr;
        other.m_size = 0;
    }
    QMultiHash &operator=(QMultiHash &&other) noexcept(std::is_nothrow_destructible<Node>::value)
    {
        QMultiHash moved(std::move(other));
        swap(moved);
        return *this;
    }

    QMultiHash(const QHash<Key, T> &other)
        : QMultiHash(other.begin(), other.end())
    {}
    void swap(QMultiHash &other) noexcept { qSwap(d, other.d); qSwap(m_size, other.m_size); }

    bool operator==(const QMultiHash &other) const noexcept
    {
        if (d == other.d)
            return true;
        if (!d || ! other.d)
            return false;
        if (m_size != other.m_size || d->size != other.d->size)
            return false;
        for (auto it = other.d->begin(); it != other.d->end(); ++it) {
            auto i = d->find(it.node()->key);
            if (i == d->end())
                return false;
            Chain *e = it.node()->value;
            while (e) {
                Chain *oe = i.node()->value;
                while (oe) {
                    if (oe->value == e->value)
                        break;
                    oe = oe->next;
                }
                if (!oe)
                    return false;
                e = e->next;
            }
        }
        // all values must be the same as size is the same
        return true;
    }
    bool operator!=(const QMultiHash &other) const noexcept { return !(*this == other); }

    inline qsizetype size() const noexcept { return m_size; }

    inline bool isEmpty() const noexcept { return !m_size; }

    inline qsizetype capacity() const noexcept { return d ? qsizetype(d->numBuckets >> 1) : 0; }
    void reserve(qsizetype size)
    {
        if (isDetached())
            d->rehash(size);
        else
            d = Data::detached(d, size_t(size));
    }
    inline void squeeze() { reserve(0); }

    inline void detach() { if (!d || d->ref.isShared()) d = Data::detached(d); }
    inline bool isDetached() const noexcept { return d && !d->ref.isShared(); }
    bool isSharedWith(const QMultiHash &other) const noexcept { return d == other.d; }

    void clear() noexcept(std::is_nothrow_destructible<Node>::value)
    {
        if (d && !d->ref.deref())
            delete d;
        d = nullptr;
        m_size = 0;
    }

    qsizetype remove(const Key &key)
    {
        if (isEmpty()) // prevents detaching shared null
            return 0;
        detach();

        auto it = d->find(key);
        if (it.isUnused())
            return 0;
        qsizetype n = Node::freeChain(it.node());
        m_size -= n;
        Q_ASSERT(m_size >= 0);
        d->erase(it);
        return n;
    }
    T take(const Key &key)
    {
        if (isEmpty()) // prevents detaching shared null
            return T();
        detach();

        auto it = d->find(key);
        if (it.isUnused())
            return T();
        Q_ASSERT(it.node()->value.size());
        Chain *e = it.node()->value;
        Q_ASSERT(e);
        if (!e->next)
            d->erase(it);
        else
            it.node()->value = e->next;
        --m_size;
        Q_ASSERT(m_size >= 0);
        T t = std::move(e->value);
        delete e;
        return t;
    }

    bool contains(const Key &key) const noexcept
    {
        if (!d)
            return false;
        return d->findNode(key) != nullptr;
    }

    Key key(const T &value, const Key &defaultKey = Key()) const noexcept
    {
        if (d) {
            auto i = d->begin();
            while (i != d->end()) {
                Chain *e = i.node()->value;
                if (e->contains(value))
                    return i.node()->key;
                ++i;
            }
        }

        return defaultKey;
    }
    T value(const Key &key, const T &defaultValue = T()) const noexcept
    {
        if (d) {
            Node *n = d->findNode(key);
            if (n) {
                Q_ASSERT(n->value);
                return n->value->value;
            }
        }
        return defaultValue;
    }

    T &operator[](const Key &key)
    {
        detach();
        Node *n = d->findAndInsertNode(key);
        Q_ASSERT(n);
        return n->value->value;
    }

    const T operator[](const Key &key) const noexcept
    {
        return value(key);
    }

    QVector<Key> uniqueKeys() const
    {
        QVector<Key> res;
        if (d) {
            auto i = d->begin();
            while (i != d->end()) {
                res.append(i.node()->key);
                ++i;
            }
        }
        return res;
    }

    QVector<Key> keys() const
    {
        return QVector<Key>(keyBegin(), keyEnd());
    }
    QVector<Key> keys(const T &value) const
    {
        QVector<Key> res;
        const_iterator i = begin();
        while (i != end()) {
            if (i.value()->contains(value))
                res.append(i.key());
            ++i;
        }
        return res;
    }
    QVector<T> values() const
    {
        return QVector<T>(begin(), end());
    }
    QVector<T> values(const Key &key) const
    {
        QVector<T> values;
        if (d) {
            Node *n = d->findNode(key);
            if (n) {
                Chain *e = n->value;
                while (e) {
                    values.append(e->value);
                    e = e->next;
                }
            }
        }
        return values;
    }

    class const_iterator;

    class iterator
    {
        using piter = typename QHashPrivate::iterator<Node>;
        friend class const_iterator;
        friend class QMultiHash<Key, T>;
        piter i;
        Chain **e = nullptr;
        explicit inline iterator(piter it, Chain **entry = nullptr) noexcept : i(it), e(entry)
        {
            if (!it.atEnd() && !e) {
                e = &it.node()->value;
                Q_ASSERT(e && *e);
            }
        }

    public:
        typedef std::forward_iterator_tag iterator_category;
        typedef qptrdiff difference_type;
        typedef T value_type;
        typedef T *pointer;
        typedef T &reference;

        constexpr iterator() noexcept = default;

        inline const Key &key() const noexcept { return i.node()->key; }
        inline T &value() const noexcept { return (*e)->value; }
        inline T &operator*() const noexcept { return (*e)->value; }
        inline T *operator->() const noexcept { return &(*e)->value; }
        inline bool operator==(const iterator &o) const noexcept { return e == o.e; }
        inline bool operator!=(const iterator &o) const noexcept { return e != o.e; }

        inline iterator &operator++() noexcept {
            Q_ASSERT(e && *e);
            e = &(*e)->next;
            Q_ASSERT(e);
            if (!*e) {
                ++i;
                e = i.atEnd() ? nullptr : &i.node()->value;
            }
            return *this;
        }
        inline iterator operator++(int) noexcept {
            iterator r = *this;
            ++(*this);
            return r;
        }

        inline bool operator==(const const_iterator &o) const noexcept { return e == o.e; }
        inline bool operator!=(const const_iterator &o) const noexcept { return e != o.e; }
    };
    friend class iterator;

    class const_iterator
    {
        using piter = typename QHashPrivate::iterator<Node>;
        friend class iterator;
        friend class QMultiHash<Key, T>;
        piter i;
        Chain **e = nullptr;
        explicit inline const_iterator(piter it, Chain **entry = nullptr) noexcept : i(it), e(entry)
        {
            if (!it.atEnd() && !e) {
                e = &it.node()->value;
                Q_ASSERT(e && *e);
            }
        }

    public:
        typedef std::forward_iterator_tag iterator_category;
        typedef qptrdiff difference_type;
        typedef T value_type;
        typedef const T *pointer;
        typedef const T &reference;

        constexpr const_iterator() noexcept = default;
        inline const_iterator(const iterator &o) noexcept : i(o.i), e(o.e) { }

        inline const Key &key() const noexcept { return i.node()->key; }
        inline T &value() const noexcept { return (*e)->value; }
        inline T &operator*() const noexcept { return (*e)->value; }
        inline T *operator->() const noexcept { return &(*e)->value; }
        inline bool operator==(const const_iterator &o) const noexcept { return e == o.e; }
        inline bool operator!=(const const_iterator &o) const noexcept { return e != o.e; }

        inline const_iterator &operator++() noexcept {
            Q_ASSERT(e && *e);
            e = &(*e)->next;
            Q_ASSERT(e);
            if (!*e) {
                ++i;
                e = i.atEnd() ? nullptr : &i.node()->value;
            }
            return *this;
        }
        inline const_iterator operator++(int) noexcept
        {
            const_iterator r = *this;
            ++(*this);
            return r;
        }
    };
    friend class const_iterator;

    class key_iterator
    {
        const_iterator i;

    public:
        typedef typename const_iterator::iterator_category iterator_category;
        typedef qptrdiff difference_type;
        typedef Key value_type;
        typedef const Key *pointer;
        typedef const Key &reference;

        key_iterator() noexcept = default;
        explicit key_iterator(const_iterator o) noexcept : i(o) { }

        const Key &operator*() const noexcept { return i.key(); }
        const Key *operator->() const noexcept { return &i.key(); }
        bool operator==(key_iterator o) const noexcept { return i == o.i; }
        bool operator!=(key_iterator o) const noexcept { return i != o.i; }

        inline key_iterator &operator++() noexcept { ++i; return *this; }
        inline key_iterator operator++(int) noexcept { return key_iterator(i++);}
        const_iterator base() const noexcept { return i; }
    };

    typedef QKeyValueIterator<const Key&, const T&, const_iterator> const_key_value_iterator;
    typedef QKeyValueIterator<const Key&, T&, iterator> key_value_iterator;

    // STL style
    inline iterator begin() { detach(); return iterator(d->begin()); }
    inline const_iterator begin() const noexcept { return d ? const_iterator(d->begin()): const_iterator(); }
    inline const_iterator cbegin() const noexcept { return d ? const_iterator(d->begin()): const_iterator(); }
    inline const_iterator constBegin() const noexcept { return d ? const_iterator(d->begin()): const_iterator(); }
    inline iterator end() noexcept { return iterator(); }
    inline const_iterator end() const noexcept { return const_iterator(); }
    inline const_iterator cend() const noexcept { return const_iterator(); }
    inline const_iterator constEnd() const noexcept { return const_iterator(); }
    inline key_iterator keyBegin() const noexcept { return key_iterator(begin()); }
    inline key_iterator keyEnd() const noexcept { return key_iterator(end()); }
    inline key_value_iterator keyValueBegin() noexcept { return key_value_iterator(begin()); }
    inline key_value_iterator keyValueEnd() noexcept { return key_value_iterator(end()); }
    inline const_key_value_iterator keyValueBegin() const noexcept { return const_key_value_iterator(begin()); }
    inline const_key_value_iterator constKeyValueBegin() const noexcept { return const_key_value_iterator(begin()); }
    inline const_key_value_iterator keyValueEnd() const noexcept { return const_key_value_iterator(end()); }
    inline const_key_value_iterator constKeyValueEnd() const noexcept { return const_key_value_iterator(end()); }

    iterator detach(const_iterator it)
    {
        auto i = it.i;
        Chain **e = it.e;
        if (d->ref.isShared()) {
            // need to store iterator position before detaching
            qsizetype n = 0;
            Chain *entry = i.node()->value;
            while (entry != *it.e) {
                ++n;
                entry = entry->next;
            }
            Q_ASSERT(entry);
            detach_helper();

            i = d->detachedIterator(i);
            e = &i.node()->value;
            while (n) {
                e = &(*e)->next;
                --n;
            }
            Q_ASSERT(e && *e);
        }
        return iterator(i, e);
    }

    iterator erase(const_iterator it)
    {
        Q_ASSERT(d);
        iterator i = detach(it);
        Chain *e = *i.e;
        Chain *next = e->next;
        *i.e = next;
        delete e;
        if (!next) {
            if (i.e == &i.i.node()->value) {
                // last remaining entry, erase
                i = iterator(d->erase(i.i));
            } else {
                i = iterator(++it.i);
            }
        }
        --m_size;
        Q_ASSERT(m_size >= 0);
        return i;
    }

    // more Qt
    typedef iterator Iterator;
    typedef const_iterator ConstIterator;
    inline qsizetype count() const noexcept { return size(); }
    iterator find(const Key &key)
    {
        if (isEmpty())
            return end();
        detach();
        auto it = d->find(key);
        if (it.isUnused())
            it = d->end();
        return iterator(it);
    }
    const_iterator find(const Key &key) const noexcept
    {
        return constFind(key);
    }
    const_iterator constFind(const Key &key) const noexcept
    {
        if (isEmpty())
            return end();
        auto it = d->find(key);
        if (it.isUnused())
            it = d->end();
        return const_iterator(it);
    }
    iterator insert(const Key &key, const T &value)
    {
        detach();
        auto it = d->insertMulti(key, value);
        ++m_size;
        return iterator(it);
    }

    float load_factor() const noexcept { return d ? d->loadFactor() : 0; }
    static float max_load_factor() noexcept { return 0.5; }
    size_t bucket_count() const noexcept { return d ? d->numBuckets : 0; }
    static size_t max_bucket_count() noexcept { return QHashPrivate::GrowthPolicy::maxNumBuckets(); }

    inline bool empty() const noexcept { return isEmpty(); }

    inline typename QMultiHash<Key, T>::iterator replace(const Key &key, const T &value)
    {
        detach();
        qsizetype s = d->size;
        auto it = d->insert(key, value);
        m_size += d->size - s;
        return iterator(it);
    }

    inline QMultiHash &operator+=(const QMultiHash &other)
    { this->unite(other); return *this; }
    inline QMultiHash operator+(const QMultiHash &other) const
    { QMultiHash result = *this; result += other; return result; }

    bool contains(const Key &key, const T &value) const noexcept
    {
        if (isEmpty())
            return false;
        auto n = d->findNode(key);
        if (n == nullptr)
            return false;
        return n->value->contains(value);
    }

    qsizetype remove(const Key &key, const T &value)
    {
        if (isEmpty()) // prevents detaching shared null
            return false;
        detach();

        auto it = d->find(key);
        if (it.isUnused())
            return 0;
        qsizetype n = 0;
        Chain **e = &it.node()->value;
        while (*e) {
            Chain *entry = *e;
            if (entry->value == value) {
                *e = entry->next;
                delete entry;
                ++n;
            } else {
                e = &entry->next;
            }
        }
        if (!it.node()->value)
            d->erase(it);
        m_size -= n;
        Q_ASSERT(m_size >= 0);
        return n;
    }

    qsizetype count(const Key &key) const noexcept
    {
        auto it = d->find(key);
        if (it.isUnused())
            return 0;
        qsizetype n = 0;
        Chain *e = it.node()->value;
        while (e) {
            ++n;
            e = e->next;
        }

        return n;
    }

    qsizetype count(const Key &key, const T &value) const noexcept
    {
        auto it = d->find(key);
        if (it.isUnused())
            return 0;
        qsizetype n = 0;
        Chain *e = it.node()->value;
        while (e) {
            if (e->value == value)
                ++n;
            e = e->next;
        }

        return n;
    }

    iterator find(const Key &key, const T &value)
    {
        detach();
        auto it = constFind(key, value);
        return iterator(it.i, it.e);
    }
    const_iterator find(const Key &key, const T &value) const noexcept
    {
        return constFind(key, value);
    }
    const_iterator constFind(const Key &key, const T &value) const noexcept
    {
        const_iterator i(constFind(key));
        const_iterator end(constEnd());
        while (i != end && i.key() == key) {
            if (i.value() == value)
                return i;
            ++i;
        }
        return end;
    }

    QMultiHash &unite(const QMultiHash &other)
    {
        if (isEmpty()) {
            *this = other;
        } else if (other.isEmpty()) {
            ;
        } else {
            QMultiHash copy(other);
            detach();
            for (auto cit = copy.cbegin(); cit != copy.cend(); ++cit)
                insert(cit.key(), *cit);
        }
        return *this;
    }

    QPair<iterator, iterator> equal_range(const Key &key)
    {
        detach();
        auto pair = qAsConst(*this).equal_range(key);
        return qMakePair(iterator(pair.first.i), iterator(pair.second.i));
    }

    QPair<const_iterator, const_iterator> equal_range(const Key &key) const noexcept
    {
        auto it = d->find(key);
        if (it.isUnused())
            return qMakePair(end(), end());
        auto end = it;
        ++end;
        return qMakePair(const_iterator(it), const_iterator(end));
    }

private:
    void detach_helper()
    {
        if (!d) {
            d = new Data;
            return;
        }
        Data *dd = new Data(*d);
        if (!d->ref.deref())
            delete d;
        d = dd;
    }
};

#if !defined(QT_NO_JAVA_STYLE_ITERATORS)
template<class Key, class T>
class QHashIterator
{
    typedef typename QHash<Key, T>::const_iterator const_iterator;
    typedef const_iterator Item;
    QHash<Key, T> c;
    const_iterator i, n;
    inline bool item_exists() const noexcept { return n != c.constEnd(); }

public:
    inline QHashIterator(const QHash<Key, T> &container) noexcept
        : c(container), i(c.constBegin()), n(c.constEnd())
    { }
    inline QHashIterator &operator=(const QHash<Key, T> &container) noexcept
    {
        c = container;
        i = c.constBegin();
        n = c.constEnd();
        return *this;
    }
    inline void toFront() noexcept
    {
        i = c.constBegin();
        n = c.constEnd();
    }
    inline void toBack() noexcept
    {
        i = c.constEnd();
        n = c.constEnd();
    }
    inline bool hasNext() const noexcept { return i != c.constEnd(); }
    inline Item next() noexcept
    {
        n = i++;
        return n;
    }
    inline Item peekNext() const noexcept { return i; }
    inline const T &value() const noexcept
    {
        Q_ASSERT(item_exists());
        return *n;
    }
    inline const Key &key() const noexcept
    {
        Q_ASSERT(item_exists());
        return n.key();
    }
    inline bool findNext(const T &t) noexcept
    {
        while ((n = i) != c.constEnd())
            if (*i++ == t)
                return true;
        return false;
    }
};

template<class Key, class T>
class QMutableHashIterator
{
    typedef typename QHash<Key, T>::iterator iterator;
    typedef typename QHash<Key, T>::const_iterator const_iterator;
    typedef iterator Item;
    QHash<Key, T> *c;
    iterator i, n;
    inline bool item_exists() const noexcept { return const_iterator(n) != c->constEnd(); }

public:
    inline QMutableHashIterator(QHash<Key, T> &container)
        : c(&container)
    {
        i = c->begin();
        n = c->end();
    }
    inline QMutableHashIterator &operator=(QHash<Key, T> &container)
    {
        c = &container;
        i = c->begin();
        n = c->end();
        return *this;
    }
    inline void toFront()
    {
        i = c->begin();
        n = c->end();
    }
    inline void toBack() noexcept
    {
        i = c->end();
        n = c->end();
    }
    inline bool hasNext() const noexcept { return const_iterator(i) != c->constEnd(); }
    inline Item next() noexcept
    {
        n = i++;
        return n;
    }
    inline Item peekNext() const noexcept { return i; }
    inline void remove()
    {
        if (const_iterator(n) != c->constEnd()) {
            i = c->erase(n);
            n = c->end();
        }
    }
    inline void setValue(const T &t)
    {
        if (const_iterator(n) != c->constEnd())
            *n = t;
    }
    inline T &value() noexcept
    {
        Q_ASSERT(item_exists());
        return *n;
    }
    inline const T &value() const noexcept
    {
        Q_ASSERT(item_exists());
        return *n;
    }
    inline const Key &key() const noexcept
    {
        Q_ASSERT(item_exists());
        return n.key();
    }
    inline bool findNext(const T &t) noexcept
    {
        while (const_iterator(n = i) != c->constEnd())
            if (*i++ == t)
                return true;
        return false;
    }
};
#endif // !QT_NO_JAVA_STYLE_ITERATORS

template <class Key, class T>
uint qHash(const QHash<Key, T> &key, uint seed = 0)
    noexcept(noexcept(qHash(std::declval<Key&>())) && noexcept(qHash(std::declval<T&>())))
{
    QtPrivate::QHashCombineCommutative hash;
    for (auto it = key.begin(), end = key.end(); it != end; ++it) {
        const Key &k = it.key();
        const T   &v = it.value();
        seed = hash(seed, std::pair<const Key&, const T&>(k, v));
    }
    return seed;
}

template <class Key, class T>
inline uint qHash(const QMultiHash<Key, T> &key, uint seed = 0)
    noexcept(noexcept(qHash(std::declval<Key&>())) && noexcept(qHash(std::declval<T&>())))
{
    const QHash<Key, T> &key2 = key;
    return qHash(key2, seed);
}

QT_END_NAMESPACE

#endif // QHASH_H
