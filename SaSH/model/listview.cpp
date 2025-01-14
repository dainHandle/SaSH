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
#include <QApplication>
#include <listview.h>
#include <qmath.h>

constexpr int MAX_LIST_COUNT = 2048;

ListView::ListView(QWidget* parent)
	: QListView(parent)
{
}

void ListView::dataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight, const QVector<int>& roles)
{
	QListView::dataChanged(topLeft, bottomRight, roles);
	//如果數據改變則滾動到底部
	//scrollToBottom();
}

void ListView::wheelEvent(QWheelEvent* event)
{
	if (event->modifiers() & Qt::ControlModifier)
	{
		// 获取当前字体
		QFont font = this->font();

		// 根据滚轮滚动的角度调整字体大小
		int delta = event->angleDelta().y();
		int fontSize = font.pointSize();
		int newFontSize = fontSize + delta / 120;  // 根据需要调整滚动速度

		// 限制字体大小的范围
		const int minFontSize = 8;
		const int maxFontSize = 48;
		newFontSize = qBound(minFontSize, newFontSize, maxFontSize);

		// 设置新的字体大小
		font.setPointSize(newFontSize);
		this->setFont(font);
		event->accept();
	}
	else
	{
		QListView::wheelEvent(event);
	}
}

void ListView::setModel(StringListModel* model)
{
	if (model)
	{
		StringListModel* old_mod = (StringListModel*)this->model();
		if (old_mod)
			disconnect(old_mod, &StringListModel::dataAppended, this, &QListView::scrollToBottom);
		QListView::setModel(model);
		connect(model, &StringListModel::dataAppended, this, &QListView::scrollToBottom, Qt::QueuedConnection);
	}
}

void ListView::append(const QString& str, int color)
{
	StringListModel* old_mod = (StringListModel*)this->model();
	if (old_mod)
		old_mod->append(str, color);
}

void ListView::remove(const QString& str)
{
	StringListModel* old_mod = (StringListModel*)this->model();
	if (old_mod)
		old_mod->remove(str);
}

void ListView::clear()
{
	StringListModel* old_mod = (StringListModel*)this->model();
	if (old_mod)
		old_mod->clear();
}

void ListView::swapRowUp(int source)
{
	StringListModel* old_mod = (StringListModel*)this->model();
	if (old_mod)
		old_mod->swapRowUp(source);
}

void ListView::swapRowDown(int source)
{
	StringListModel* old_mod = (StringListModel*)this->model();
	if (old_mod)
		old_mod->swapRowDown(source);
}

///////////////////////////////////////////////////////////////////////////////////////////////
StringListModel::StringListModel(QObject* parent)
	: QAbstractListModel(parent)
	, m_stringlistLocker(QReadWriteLock::Recursive)
{
}


void StringListModel::append(const QString& str, int color)
{
	QVector<QString> list;
	QVector<int> colorlist;
	getAllList(list, colorlist);
	int size = list.size();

	if (size >= MAX_LIST_COUNT)
	{
		// Remove first element
		beginRemoveRows(QModelIndex(), 0, 0);
		list.removeFirst();
		colorlist.removeFirst();
		endRemoveRows();
	}

	beginInsertRows(QModelIndex(), size, size);
	list.append(str);
	colorlist.append(color);
	setAllList(list, colorlist);
	endInsertRows();

}

QString StringListModel::takeFirst()
{
	QVector<QString> list;
	QVector<int> colorlist;
	getAllList(list, colorlist);
	int size = list.size();
	QString str = "";

	beginRemoveRows(QModelIndex(), 0, 0);
	if (size > 0)
	{
		str = list.takeFirst();
		setList(list);
	}
	endRemoveRows();

	return str;
}

void StringListModel::remove(const QString& str)
{
	QVector<QString> list;
	QVector<int> colorlist;
	getAllList(list, colorlist);
	int size = list.size();

	int index = list.indexOf(str);
	if (index >= 0 && index < size)
	{
		beginRemoveRows(QModelIndex(), index, index);
		list.removeAt(index);
		colorlist.removeAt(index);
		setAllList(list, colorlist);
		endRemoveRows();
	}
}

void StringListModel::clear()
{
	beginResetModel();

	setAllList(QVector<QString>(), QVector<int>());

	endResetModel();
}

// 用于排序的比较函数，按整数值进行比较
bool pairCompare(const QPair<int, QString>& pair1, const QPair<int, QString>& pair2)
{
	return pair1.first < pair2.first;
}

bool pairCompareGreaerString(const QPair<int, QString>& pair1, const QPair<int, QString>& pair2)
{
	return pair1.second > pair2.second;
}
void StringListModel::sort(int column, Qt::SortOrder order)
{
	Q_UNUSED(column);
	beginResetModel();

	if (order == Qt::AscendingOrder)
	{
		QVector<QString> list;
		QVector<int> colorlist;
		getAllList(list, colorlist);
		int size = list.size();
		QVector<QPair<int, QString>> pairVector;

		for (int i = 0; i < size; ++i)
		{
			pairVector.append(qMakePair(colorlist.at(i), list.at(i)));
		}

		std::sort(pairVector.begin(), pairVector.end(), pairCompare);

		list.clear();
		colorlist.clear();

		size = pairVector.size();
		for (int i = 0; i < size; ++i)
		{
			colorlist.append(pairVector.at(i).first);
			list.append(pairVector.at(i).second);
		}

		setAllList(list, colorlist);
	}
	else
	{
		QVector<QString> list;
		QVector<int> colorlist;
		getAllList(list, colorlist);
		int size = list.size();
		QVector<QPair<int, QString>> pairVector;

		for (int i = 0; i < size; ++i)
		{
			pairVector.append(qMakePair(colorlist.at(i), list.at(i)));
		}

		std::sort(pairVector.begin(), pairVector.end(), pairCompareGreaerString);

		list.clear();
		colorlist.clear();
		size = pairVector.size();
		for (int i = size - 1; i >= 0; --i)
		{
			colorlist.append(pairVector.at(i).first);
			list.append(pairVector.at(i).second);
		}

		setAllList(list, colorlist);
	}

	endResetModel();
}

bool StringListModel::insertRows(int row, int count, const QModelIndex& parent)
{
	Q_UNUSED(parent);
	beginInsertRows(QModelIndex(), row, row + count - 1);
	endInsertRows();
	return true;
}
bool StringListModel::removeRows(int row, int count, const QModelIndex& parent)
{
	Q_UNUSED(parent);
	beginRemoveRows(QModelIndex(), row, row + count - 1);
	endRemoveRows();
	return true;
}
bool StringListModel::moveRows(const QModelIndex& sourceParent, int sourceRow, int count, const QModelIndex& destinationParent, int destinationChild)
{
	Q_UNUSED(destinationParent);
	Q_UNUSED(sourceParent);
	beginMoveRows(QModelIndex(), sourceRow, sourceRow + count - 1, QModelIndex(), destinationChild);
	endMoveRows();
	return true;
}

QMap<int, QVariant> StringListModel::itemData(const QModelIndex& index) const
{
	QMap<int, QVariant> map;

	int i = index.row();

	QVector<QString> list = getList();
	int size = list.size();
	if (i >= 0 && i < size)
		map.insert(Qt::DisplayRole, list.at(i));

	return map;
}

QVariant StringListModel::data(const QModelIndex& index, int role) const
{
	if (!index.isValid())
		return QVariant();


	switch (role)
	{
	case Qt::DisplayRole:
	{
		QVector<QString> list = getList();
		int i = index.row();
		if (i < 0 || i >= list.size())
			return QVariant();
		return list.at(i);
	}
	case Qt::BackgroundRole:
	{
		static const QBrush brushBase = qApp->palette().base();
		static const QBrush brushAlternate = qApp->palette().alternateBase();
		return ((index.row() & 99) == 0) ? brushBase : brushAlternate;
	}
	case Qt::ForegroundRole:
	{

		static const QHash<int, QColor> hash = {
			{ 0, QColor(255,255,255) },
			{ 1, QColor(0,255,255) },
			{ 2, QColor(255,0,255) },
			{ 3, QColor(0,0,255) },
			{ 4, QColor(255,255,0) },
			{ 5, QColor(0,255,0) },
			{ 6, QColor(255,0,0) },
			{ 7, QColor(160,160,164) },
			{ 8, QColor(166,202,240) },
			{ 9, QColor(192,220,192) },
			{ 10, QColor(218,175,66) },
		};

		QVector<int> colorlist = getColorList();
		int i = index.row();
		int size = colorlist.size();
		if (i >= size || i < 0)
			return QColor(255, 255, 255);

		int color = colorlist.at(i);
		return hash.value(color, QColor(255, 255, 255));
	}
	default:
		break;
	}
	return QVariant();
}

bool StringListModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
	if (role == Qt::EditRole)
	{
		QVector<QString> list = getList();
		int i = index.row();
		int size = list.size();
		if (i >= 0 && i < size)
		{
			list.replace(index.row(), value.toString());
			setList(list);
			emit dataChanged(index, index, QVector<int>() << role);
			return true;
		}
	}
	return false;
}

void StringListModel::swapRowUp(int source)
{
	QVector<QString> list;
	QVector<int> colorlist;
	getAllList(list, colorlist);
	int size = list.size();

	if (source > 0 && source < size)
	{
		beginMoveRows(QModelIndex(), source, source, QModelIndex(), source - 1);

		list.swapItemsAt(source, source - 1);
		colorlist.swapItemsAt(source, source - 1);
		setAllList(list, colorlist);

		endMoveRows();
	}
}
void StringListModel::swapRowDown(int source)
{
	QVector<QString> list;
	QVector<int> colorlist;
	getAllList(list, colorlist);
	int size = list.size();

	if (source >= 0 && source + 1 < size)
	{
		beginMoveRows(QModelIndex(), source, source, QModelIndex(), source + 2);

		list.swapItemsAt(source, source + 1);
		colorlist.swapItemsAt(source, source + 1);
		setAllList(list, colorlist);

		endMoveRows();
	}
}