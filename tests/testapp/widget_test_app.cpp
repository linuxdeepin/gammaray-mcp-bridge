/*
  widget_test_app.cpp — Minimal QWidget test application for GammaRay MCP widget tools.

  SPDX-FileCopyrightText: 2026 The GammaRay MCP Bridge Authors
  SPDX-License-Identifier: GPL-2.0-or-later
*/

#include <QApplication>
#include <QPushButton>
#include <QVBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QCheckBox>
#include <QGroupBox>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("GammaRay MCP Widget Test App"));

    auto *mainWindow = new QWidget();
    mainWindow->setWindowTitle(QStringLiteral("GammaRay Widget Test"));
    mainWindow->resize(400, 300);

    auto *layout = new QVBoxLayout(mainWindow);

    auto *label = new QLabel(QStringLiteral("Hello from GammaRay MCP"));
    layout->addWidget(label);

    auto *lineEdit = new QLineEdit();
    lineEdit->setPlaceholderText(QStringLiteral("Type something..."));
    layout->addWidget(lineEdit);

    auto *checkBox = new QCheckBox(QStringLiteral("Enable feature"));
    checkBox->setChecked(true);
    layout->addWidget(checkBox);

    auto *groupBox = new QGroupBox(QStringLiteral("Options"));
    auto *groupLayout = new QVBoxLayout(groupBox);
    auto *innerCheck = new QCheckBox(QStringLiteral("Sub-option A"));
    groupLayout->addWidget(innerCheck);
    groupLayout->addWidget(new QCheckBox(QStringLiteral("Sub-option B")));
    layout->addWidget(groupBox);

    auto *button = new QPushButton(QStringLiteral("Click Me"));
    layout->addWidget(button);

    mainWindow->show();
    return app.exec();
}