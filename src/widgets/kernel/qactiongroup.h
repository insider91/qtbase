/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the QtWidgets module of the Qt Toolkit.
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

#ifndef QACTIONGROUP_H
#define QACTIONGROUP_H

#include <QtWidgets/qtwidgetsglobal.h>
#include <QtGui/qguiactiongroup.h>
#include <QtWidgets/qaction.h>

QT_REQUIRE_CONFIG(action);

QT_BEGIN_NAMESPACE

class QActionGroupPrivate;

class Q_WIDGETS_EXPORT QActionGroup : public QGuiActionGroup
{
    Q_OBJECT
    Q_DECLARE_PRIVATE(QActionGroup)

public:
    explicit QActionGroup(QObject* parent);
    ~QActionGroup();

    QAction *checkedAction() const;

    QAction *addAction(QAction *a);
    QAction *addAction(const QString &text);
    QAction *addAction(const QIcon &icon, const QString &text);

    QList<QAction*> actions() const;

Q_SIGNALS:
    void triggered(QAction *);
    void hovered(QAction *);

private:
    Q_DISABLE_COPY(QActionGroup)
};

QT_END_NAMESPACE

#endif // QACTIONGROUP_H
