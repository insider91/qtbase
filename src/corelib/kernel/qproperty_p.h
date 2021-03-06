/***************************************************************************
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

#ifndef QPROPERTY_P_H
#define QPROPERTY_P_H

//
//  W A R N I N G
//  -------------
//
// This file is not part of the Qt API.  It exists for the convenience
// of qapplication_*.cpp, qwidget*.cpp and qfiledialog.cpp.  This header
// file may change from version to version without notice, or even be removed.
//
// We mean it.
//

#include <qglobal.h>
#include <qproperty.h>

#include <qvarlengtharray.h>
#include <qscopedpointer.h>
#include <vector>


QT_BEGIN_NAMESPACE

// Keep all classes related to QProperty in one compilation unit. Performance of this code is crucial and
// we need to allow the compiler to inline where it makes sense.

// This is a helper "namespace"
struct Q_AUTOTEST_EXPORT QPropertyBindingDataPointer
{
    const QtPrivate::QPropertyBindingData *ptr = nullptr;

    QPropertyBindingPrivate *bindingPtr() const
    {
        if (ptr->d_ptr & QtPrivate::QPropertyBindingData::BindingBit)
            return reinterpret_cast<QPropertyBindingPrivate*>(ptr->d_ptr & ~QtPrivate::QPropertyBindingData::FlagMask);
        return nullptr;
    }

    void setObservers(QPropertyObserver *observer)
    {
        observer->prev = reinterpret_cast<QPropertyObserver**>(&(ptr->d_ptr));
        ptr->d_ptr = (reinterpret_cast<quintptr>(observer) & ~QtPrivate::QPropertyBindingData::FlagMask);
    }
    void fixupFirstObserverAfterMove() const;
    void addObserver(QPropertyObserver *observer);
    void setFirstObserver(QPropertyObserver *observer);
    QPropertyObserverPointer firstObserver() const;

    int observerCount() const;

    template <typename T>
    static QPropertyBindingDataPointer get(QProperty<T> &property)
    {
        return QPropertyBindingDataPointer{&property.bindingData()};
    }
};

// This is a helper "namespace"
struct QPropertyObserverPointer
{
    QPropertyObserver *ptr = nullptr;

    void unlink();

    void setBindingToMarkDirty(QPropertyBindingPrivate *binding);
    void setChangeHandler(QPropertyObserver::ChangeHandler changeHandler);
    void setAliasedProperty(QUntypedPropertyData *propertyPtr);

    void notify(QPropertyBindingPrivate *triggeringBinding, QUntypedPropertyData *propertyDataPtr, const bool alreadyKnownToHaveChanged = false);
    void observeProperty(QPropertyBindingDataPointer property);

    explicit operator bool() const { return ptr != nullptr; }

    QPropertyObserverPointer nextObserver() const { return {ptr->next.data()}; }
};

class QPropertyBindingErrorPrivate : public QSharedData
{
public:
    QPropertyBindingError::Type type = QPropertyBindingError::NoError;
    QString description;
};

namespace QtPrivate {

struct BindingEvaluationState
{
    BindingEvaluationState(QPropertyBindingPrivate *binding);
    ~BindingEvaluationState()
    {
        *currentState = previousState;
    }

    QPropertyBindingPrivate *binding;
    BindingEvaluationState *previousState = nullptr;
    BindingEvaluationState **currentState = nullptr;
};

struct CurrentCompatProperty
{
    Q_CORE_EXPORT CurrentCompatProperty(QBindingStatus *status, QUntypedPropertyData *property);
    ~CurrentCompatProperty()
    {
        *currentState = previousState;
    }
    QUntypedPropertyData *property;
    CurrentCompatProperty *previousState = nullptr;
    CurrentCompatProperty **currentState = nullptr;
};

}

struct QBindingStatus
{
    QtPrivate::BindingEvaluationState *currentlyEvaluatingBinding = nullptr;
    QtPrivate::CurrentCompatProperty *currentCompatProperty = nullptr;
};

class Q_CORE_EXPORT QPropertyBindingPrivate : public QSharedData
{
private:
    friend struct QPropertyBindingDataPointer;

    using ObserverArray = std::array<QPropertyObserver, 4>;

    // QSharedData is 4 bytes. Use the padding for the bools as we need 8 byte alignment below.

    // a dependent property has changed, and the binding needs to be reevaluated on access
    bool dirty = false;
    // used to detect binding loops for lazy evaluated properties
    bool updating = false;
    bool hasStaticObserver = false;
    bool hasBindingWrapper:1;
    // used to detect binding loops for eagerly evaluated properties
    bool eagerlyUpdating:1;

    QUntypedPropertyBinding::BindingEvaluationFunction evaluationFunction;

    union {
        QtPrivate::QPropertyObserverCallback staticObserverCallback = nullptr;
        QtPrivate::QPropertyBindingWrapper staticBindingWrapper;
    };
    ObserverArray inlineDependencyObservers;

    QPropertyObserverPointer firstObserver;
    QScopedPointer<std::vector<QPropertyObserver>> heapObservers;

    QUntypedPropertyData *propertyDataPtr = nullptr;

    QPropertyBindingSourceLocation location;
    QPropertyBindingError error;

    QMetaType metaType;

public:
    // public because the auto-tests access it, too.
    size_t dependencyObserverCount = 0;

    QPropertyBindingPrivate(QMetaType metaType, QUntypedPropertyBinding::BindingEvaluationFunction evaluationFunction,
                            const QPropertyBindingSourceLocation &location)
        : hasBindingWrapper(false)
        , eagerlyUpdating(false)
        , evaluationFunction(std::move(evaluationFunction))
        , inlineDependencyObservers() // Explicit initialization required because of union
        , location(location)
        , metaType(metaType)
    {}
    virtual ~QPropertyBindingPrivate();

    void setDirty(bool d) { dirty = d; }
    void setProperty(QUntypedPropertyData *propertyPtr) { propertyDataPtr = propertyPtr; }
    void setStaticObserver(QtPrivate::QPropertyObserverCallback callback, QtPrivate::QPropertyBindingWrapper bindingWrapper)
    {
        Q_ASSERT(!(callback && bindingWrapper));
        if (callback) {
            hasStaticObserver = true;
            hasBindingWrapper = false;
            staticObserverCallback = callback;
        } else if (bindingWrapper) {
            hasStaticObserver = false;
            hasBindingWrapper = true;
            staticBindingWrapper = bindingWrapper;
        } else {
            hasStaticObserver = false;
            hasBindingWrapper = false;
            staticObserverCallback = nullptr;
        }
    }
    void prependObserver(QPropertyObserverPointer observer) {
        observer.ptr->prev = const_cast<QPropertyObserver **>(&firstObserver.ptr);
        firstObserver = observer;
    }

    QPropertyObserverPointer takeObservers()
    {
        auto observers = firstObserver;
        firstObserver.ptr = nullptr;
        return observers;
    }

    void clearDependencyObservers() {
        for (size_t i = 0; i < qMin(dependencyObserverCount, inlineDependencyObservers.size()); ++i) {
            QPropertyObserverPointer p{&inlineDependencyObservers[i]};
            p.unlink();
        }
        if (heapObservers)
            heapObservers->clear();
        dependencyObserverCount = 0;
    }
    QPropertyObserverPointer allocateDependencyObserver() {
        if (dependencyObserverCount < inlineDependencyObservers.size()) {
            ++dependencyObserverCount;
            return {&inlineDependencyObservers[dependencyObserverCount - 1]};
        }
        ++dependencyObserverCount;
        if (!heapObservers)
            heapObservers.reset(new std::vector<QPropertyObserver>());
        return {&heapObservers->emplace_back()};
    }

    QPropertyBindingSourceLocation sourceLocation() const { return location; }
    QPropertyBindingError bindingError() const { return error; }
    QMetaType valueMetaType() const { return metaType; }

    void unlinkAndDeref();

    void markDirtyAndNotifyObservers();
    bool evaluateIfDirtyAndReturnTrueIfValueChanged(const QUntypedPropertyData *data);

    static QPropertyBindingPrivate *get(const QUntypedPropertyBinding &binding)
    { return binding.d.data(); }

    void setError(QPropertyBindingError &&e)
    { error = std::move(e); }

    void detachFromProperty() {
        hasStaticObserver = false;
        hasBindingWrapper = false;
        propertyDataPtr = nullptr;
        clearDependencyObservers();
    }

    bool requiresEagerEvaluation() const { return hasBindingWrapper; }

    static QPropertyBindingPrivate *currentlyEvaluatingBinding();
};

inline void QPropertyBindingDataPointer::setFirstObserver(QPropertyObserver *observer)
{
    if (auto *binding = bindingPtr()) {
        binding->firstObserver.ptr = observer;
        return;
    }
    ptr->d_ptr = reinterpret_cast<quintptr>(observer) | (ptr->d_ptr & QtPrivate::QPropertyBindingData::FlagMask);
}

inline void QPropertyBindingDataPointer::fixupFirstObserverAfterMove() const
{
    // If QPropertyBindingData has been moved, and it has an observer
    // we have to adjust the firstObesrver's prev pointer to point to
    // the moved to QPropertyBindingData's d_ptr
    if (ptr->d_ptr & QtPrivate::QPropertyBindingData::BindingBit)
        return; // nothing to do if the observer is stored in the binding
    if (auto observer = firstObserver())
        observer.ptr->prev = reinterpret_cast<QPropertyObserver**>(&(ptr->d_ptr));
}

inline QPropertyObserverPointer QPropertyBindingDataPointer::firstObserver() const
{
    if (auto *binding = bindingPtr())
        return binding->firstObserver;
    return {reinterpret_cast<QPropertyObserver*>(ptr->d_ptr & ~QtPrivate::QPropertyBindingData::FlagMask)};
}


template<typename Class, typename T, auto Offset, auto Setter>
class QObjectCompatProperty : public QPropertyData<T>
{
    using ThisType = QObjectCompatProperty<Class, T, Offset, Setter>;
    Class *owner()
    {
        char *that = reinterpret_cast<char *>(this);
        return reinterpret_cast<Class *>(that - QtPrivate::detail::getOffset(Offset));
    }
    const Class *owner() const
    {
        char *that = const_cast<char *>(reinterpret_cast<const char *>(this));
        return reinterpret_cast<Class *>(that - QtPrivate::detail::getOffset(Offset));
    }
    static bool bindingWrapper(QMetaType type, QUntypedPropertyData *dataPtr, QtPrivate::QPropertyBindingFunction binding)
    {
        auto *thisData = static_cast<ThisType *>(dataPtr);
        QPropertyData<T> copy;
        binding(type, &copy);
        if constexpr (QTypeTraits::has_operator_equal_v<T>)
            if (copy.valueBypassingBindings() == thisData->valueBypassingBindings())
                return false;
        // ensure value and setValue know we're currently evaluating our binding
        QBindingStorage *storage = qGetBindingStorage(thisData->owner());
        QtPrivate::CurrentCompatProperty guardThis(storage->bindingStatus, thisData);
        (thisData->owner()->*Setter)(copy.valueBypassingBindings());
        return true;
    }
    inline bool inBindingWrapper(const QBindingStorage *storage) const
    {
        return storage->bindingStatus->currentCompatProperty &&
               storage->bindingStatus->currentCompatProperty->property == this;
    }

public:
    using value_type = typename QPropertyData<T>::value_type;
    using parameter_type = typename QPropertyData<T>::parameter_type;
    using arrow_operator_result = typename QPropertyData<T>::arrow_operator_result;

    QObjectCompatProperty() = default;
    explicit QObjectCompatProperty(const T &initialValue) : QPropertyData<T>(initialValue) {}
    explicit QObjectCompatProperty(T &&initialValue) : QPropertyData<T>(std::move(initialValue)) {}

    parameter_type value() const {
        const QBindingStorage *storage = qGetBindingStorage(owner());
        // make sure we don't register this binding as a dependency to itself
        if (!inBindingWrapper(storage))
            storage->maybeUpdateBindingAndRegister(this);
        return this->val;
    }

    arrow_operator_result operator->() const
    {
        if constexpr (QTypeTraits::is_dereferenceable_v<T>) {
            return value();
        } else if constexpr (std::is_pointer_v<T>) {
            value();
            return this->val;
        } else {
            return;
        }
    }

    parameter_type operator*() const
    {
        return value();
    }

    operator parameter_type() const
    {
        return value();
    }

    void setValue(parameter_type t) {
        QBindingStorage *storage = qGetBindingStorage(owner());
        auto *bd = storage->bindingData(this);
        // make sure we don't remove the binding if called from the bindingWrapper
        if (bd && !inBindingWrapper(storage))
            bd->removeBinding();
        if constexpr (QTypeTraits::has_operator_equal_v<T>)
            if (this->val == t)
                return;
        this->val = t;
        notify(bd);
    }

    QObjectCompatProperty &operator=(parameter_type newValue)
    {
        setValue(newValue);
        return *this;
    }

    QPropertyBinding<T> setBinding(const QPropertyBinding<T> &newBinding)
    {
        QtPrivate::QPropertyBindingData *bd = qGetBindingStorage(owner())->bindingData(this, true);
        QUntypedPropertyBinding oldBinding(bd->setBinding(newBinding, this, nullptr, bindingWrapper));
        // notification is already handled in QPropertyBindingData::setBinding
        return static_cast<QPropertyBinding<T> &>(oldBinding);
    }

    bool setBinding(const QUntypedPropertyBinding &newBinding)
    {
        if (!newBinding.isNull() && newBinding.valueMetaType().id() != qMetaTypeId<T>())
            return false;
        setBinding(static_cast<const QPropertyBinding<T> &>(newBinding));
        return true;
    }

#ifndef Q_CLANG_QDOC
    template <typename Functor>
    QPropertyBinding<T> setBinding(Functor &&f,
                                   const QPropertyBindingSourceLocation &location = QT_PROPERTY_DEFAULT_BINDING_LOCATION,
                                   std::enable_if_t<std::is_invocable_v<Functor>> * = nullptr)
    {
        return setBinding(Qt::makePropertyBinding(std::forward<Functor>(f), location));
    }
#else
    template <typename Functor>
    QPropertyBinding<T> setBinding(Functor f);
#endif

    bool hasBinding() const {
        auto *bd = qGetBindingStorage(owner())->bindingData(this);
        return bd && bd->binding() != nullptr;
    }

    QPropertyBinding<T> binding() const
    {
        auto *bd = qGetBindingStorage(owner())->bindingData(this);
        return static_cast<QPropertyBinding<T> &&>(QUntypedPropertyBinding(bd ? bd->binding() : nullptr));
    }

    QPropertyBinding<T> takeBinding()
    {
        return setBinding(QPropertyBinding<T>());
    }

    template<typename Functor>
    QPropertyChangeHandler<Functor> onValueChanged(Functor f)
    {
        static_assert(std::is_invocable_v<Functor>, "Functor callback must be callable without any parameters");
        return QPropertyChangeHandler<Functor>(*this, f);
    }

    template<typename Functor>
    QPropertyChangeHandler<Functor> subscribe(Functor f)
    {
        static_assert(std::is_invocable_v<Functor>, "Functor callback must be callable without any parameters");
        f();
        return onValueChanged(f);
    }

    QtPrivate::QPropertyBindingData &bindingData() const
    {
        auto *storage = const_cast<QBindingStorage *>(qGetBindingStorage(owner()));
        return *storage->bindingData(const_cast<QObjectCompatProperty *>(this), true);
    }
private:
    void notify(const QtPrivate::QPropertyBindingData *binding)
    {
        if (binding)
            binding->notifyObservers(this);
    }
};

#define Q_OBJECT_COMPAT_PROPERTY(Class, Type, name,  setter) \
    static constexpr size_t _qt_property_##name##_offset() { \
        QT_WARNING_PUSH QT_WARNING_DISABLE_INVALID_OFFSETOF \
        return offsetof(Class, name); \
        QT_WARNING_POP \
    } \
    QObjectCompatProperty<Class, Type, Class::_qt_property_##name##_offset, setter> name;


QT_END_NAMESPACE

#endif // QPROPERTY_P_H
