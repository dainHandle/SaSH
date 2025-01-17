﻿/*
				GNU GENERAL PUBLIC LICENSE
				   Version 2, June 1991
COPYRIGHT (C) Bestkakkoii 2023 All Rights Reserved.
This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.
You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.

*/

#include "stdafx.h"
#include "selectobjectform.h"
#include <util.h>

SelectObjectForm::SelectObjectForm(TitleType type, QWidget* parent)
	: QDialog(parent), type_(type)
{
	ui.setupUi(this);
	setAttribute(Qt::WA_DeleteOnClose);
	setWindowFlags(Qt::Tool | Qt::Dialog | Qt::WindowCloseButtonHint);
	setModal(true);
	ui.listWidget->setUniformItemSizes(false);
	ui.comboBox->setEditable(true);

	connect(ui.buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(ui.buttonBox, &QDialogButtonBox::accepted, this, &SelectObjectForm::onAccept);
	connect(ui.buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

	ui.buttonBox->button(QDialogButtonBox::Ok)->setText(tr("ok"));
	ui.buttonBox->button(QDialogButtonBox::Cancel)->setText(tr("cancel"));

	QList<QPushButton*> buttonList = util::findWidgets<QPushButton>(this);

	for (auto& button : buttonList)
	{
		connect(button, &QPushButton::clicked, this, &SelectObjectForm::onButtonClicked, Qt::UniqueConnection);
	}

	static const QHash<TitleType, QString> title_hash = {
		{ kAutoDropItem, tr("auto drop item") },//自動丟棄
		{ kLockAttack, tr("lock attack") },//鎖定攻擊
		{ kLockEscape, tr("lock escape") },//鎖定逃跑
		{ kAutoCatch, tr("auto catch") },//自動捉寵
		{ kAutoDropPet, tr("auto drop pet") },//自動丟寵
		{ kAutoLogOut, tr("auto logout") },//自動登出名單
		{ kWhiteList, tr("white list") },//允許加入名單
		{ kBlackList, tr("black list") },//自動踢除名單
	};
	setWindowTitle(title_hash.value(type_, tr("unknown type")));
}

SelectObjectForm::~SelectObjectForm()
{
}

void SelectObjectForm::showEvent(QShowEvent* e)
{
	setAttribute(Qt::WA_Mapped);
	QDialog::showEvent(e);
}

void SelectObjectForm::setList(const QStringList& objectList)
{
	ui.listWidget->clear();
	ui.listWidget->addItems(objectList);

}

void SelectObjectForm::setSelectList(const QStringList& objectList)
{
	ui.comboBox->clear();
	ui.comboBox->addItems(objectList);
}

void SelectObjectForm::onButtonClicked()
{
	QPushButton* pButton = qobject_cast<QPushButton*>(sender());
	if (!pButton)
		return;

	QString name = pButton->objectName();
	if (name.isEmpty())
		return;

	do
	{
		if (name == "pushButton_down")
		{
			appendItem();
			break;
		}

		if (name == "pushButton_up")
		{
			deleteItem();
			break;
		}
	} while (false);
}


void SelectObjectForm::onAccept()
{
	if (pRecviveList_ != nullptr)
	{
		pRecviveList_->clear();
		for (int i = 0; i < ui.listWidget->count(); ++i)
		{
			pRecviveList_->append(ui.listWidget->item(i)->text());
		}
	}
}

void SelectObjectForm::deleteItem()
{
	QList<QListWidgetItem*> selectedItems = ui.listWidget->selectedItems();
	for (QListWidgetItem* item : selectedItems)
	{
		ui.listWidget->takeItem(ui.listWidget->row(item));
	}
}

void SelectObjectForm::appendItem()
{
	QString currentText = ui.comboBox->currentText().simplified();
	if (currentText.isEmpty())
	{
		return;
	}

	QScopedPointer<QListWidgetItem> newItem(new QListWidgetItem(currentText));
	if (newItem.isNull())
	{
		return;
	}

	QList<QListWidgetItem*> existingItems = ui.listWidget->findItems(currentText, Qt::MatchExactly);

	if (existingItems.isEmpty())
	{
		ui.listWidget->addItem(newItem.take());
		ui.listWidget->scrollToBottom();
	}
}

void SelectObjectForm::setRecviveList(QStringList* pList)
{
	if (pList == nullptr)
	{
		return;
	}

	pRecviveList_ = pList;
}