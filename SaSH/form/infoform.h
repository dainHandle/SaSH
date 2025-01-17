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

#pragma once

#include <QWidget>
#include "ui_infoform.h"

class BattleInfoForm;
class PlayerInfoForm;
class ItemInfoForm;
class ChatInfoForm;
class MailInfoForm;
class PetInfoForm;
class AfkInfoForm;

class InfoForm : public QWidget
{
	Q_OBJECT

public:
	InfoForm(int defaultPage = -1, QWidget* parent = nullptr);
	virtual ~InfoForm();

signals:
	void resetControlTextLanguage();

public slots:
	void onResetControlTextLanguage();
	void onApplyHashSettingsToUI();

protected:
	void showEvent(QShowEvent* e);
	void closeEvent(QCloseEvent* e);

private:
	Ui::InfoFormClass ui;

	BattleInfoForm* pBattleInfoForm_ = nullptr;
	PlayerInfoForm* pPlayerInfoForm_ = nullptr;
	ItemInfoForm* pItemInfoForm_ = nullptr;
	ChatInfoForm* pChatInfoForm_ = nullptr;
	MailInfoForm* pMailInfoForm_ = nullptr;
	PetInfoForm* pPetInfoForm_ = nullptr;
	AfkInfoForm* pAfkInfoForm_ = nullptr;
};
