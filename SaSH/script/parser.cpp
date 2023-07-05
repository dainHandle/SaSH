﻿#include "stdafx.h"
#include "parser.h"

#include "signaldispatcher.h"
#include "injector.h"
#include "interpreter.h"

#ifdef _DEBUG
#define exprtk_enable_debugging
#endif
#include <exprtk/exprtk.hpp>

#include "spdloger.hpp"

extern QString g_logger_name;

Parser::Parser()
	: ThreadPlugin(nullptr)
{
	qDebug() << "Parser is created!!";
	SignalDispatcher& signalDispatcher = SignalDispatcher::getInstance();
	connect(&signalDispatcher, &SignalDispatcher::nodifyAllStop, this, &Parser::requestInterruption, Qt::UniqueConnection);


}

Parser::~Parser()
{
	qDebug() << "Parser is distory!!";
}

//根據token解釋腳本
void Parser::parse(qint64 line)
{

	lineNumber_ = line; //設置當前行號
	callStack_.clear(); //清空調用棧
	jmpStack_.clear();  //清空跳轉棧

	if (tokens_.isEmpty())
		return;

	if (variables_ == nullptr)
	{
		variables_ = new QVariantHash();
		if (variables_ == nullptr)
			return;
	}

	if (globalVarLock_ == nullptr)
	{
		globalVarLock_ = new QReadWriteLock();
		if (globalVarLock_ == nullptr)
			return;
	}

	processTokens();

	if (!isSubScript_)
	{
		if (variables_ != nullptr)
		{
			delete variables_;
			variables_ = nullptr;
		}

		if (globalVarLock_ != nullptr)
		{
			delete globalVarLock_;
			globalVarLock_ = nullptr;
		}
	}
}

//"調用" 傳參數最小佔位
constexpr qint64 kCallPlaceHoldSize = 2;
//"格式化" 最小佔位
constexpr qint64 kFormatPlaceHoldSize = 3;

//變量運算
template<typename T>
T calc(const QVariant& a, const QVariant& b, RESERVE operatorType)
{
	if (operatorType == TK_ADD)
	{
		return a.value<T>() + b.value<T>();
	}
	else if (operatorType == TK_SUB)
	{
		return a.value<T>() - b.value<T>();
	}
	else if (operatorType == TK_MUL)
	{
		return a.value<T>() * b.value<T>();
	}
	else if (operatorType == TK_DIV)
	{
		return a.value<T>() / b.value<T>();
	}
	else if (operatorType == TK_INC)
	{
		if constexpr (std::is_integral<T>::value)
			return a.value<T>() + 1;
		else
			return a.value<T>() + 1.0;
	}
	else if (operatorType == TK_DEC)
	{
		if constexpr (std::is_integral<T>::value)
			return a.value<T>() - 1;
		else
			return a.value<T>() - 1.0;
	}

	Q_UNREACHABLE();
}

//比較兩個QVariant以 a 的類型為主
bool Parser::compare(const QVariant& a, const QVariant& b, RESERVE type) const
{
	QVariant::Type aType = a.type();

	if (aType == QVariant::String)
	{
		QString strA = a.toString();
		QString strB = b.toString();

		// 根据 type 进行相应的比较操作
		switch (type)
		{
		case TK_EQ: // "=="
			return (strA == strB);

		case TK_NEQ: // "!="
			return (strA != strB);

		case TK_GT: // ">"
			return (strA.compare(strB) > 0);

		case TK_LT: // "<"
			return (strA.compare(strB) < 0);

		case TK_GEQ: // ">="
			return (strA.compare(strB) >= 0);

		case TK_LEQ: // "<="
			return (strA.compare(strB) <= 0);

		default:
			return false; // 不支持的比较类型，返回 false
		}
	}
	else
	{
		qint64 numA = a.toLongLong();
		qint64 numB = b.toLongLong();

		// 根据 type 进行相应的比较操作
		switch (type)
		{
		case TK_EQ: // "=="
			return (numA == numB);

		case TK_NEQ: // "!="
			return (numA != numB);

		case TK_GT: // ">"
			return (numA > numB);

		case TK_LT: // "<"
			return (numA < numB);

		case TK_GEQ: // ">="
			return (numA >= numB);

		case TK_LEQ: // "<="
			return (numA <= numB);

		default:
			return false; // 不支持的比较类型，返回 false
		}

	}

	return false; // 不支持的类型，返回 false
}

template <typename T>
bool Parser::exprTo(QString expr, T* ret)
{
	auto makeValue = [](const QString& expr, T* ret)->bool
	{
		using TKSymbolTable = exprtk::symbol_table<double>;
		using TKExpression = exprtk::expression<double>;
		using TKParser = exprtk::parser<double>;

		TKSymbolTable symbolTable;
		symbolTable.add_constants();

		TKParser parser;
		TKExpression expression;
		expression.register_symbol_table(symbolTable);

		const QByteArray exprStrUTF8 = expr.toUtf8();
		const std::string exprStr = std::string(exprStrUTF8.constData(), exprStrUTF8.size());
		if (!parser.compile(exprStr, expression))
			return false;

		if (ret != nullptr)
			*ret = static_cast<T>(expression.value());
		return true;
	};

	bool result = makeValue(expr, ret);
	if (result)
		return true;

	static const QStringList logicOp = { "==", "!=", "<=", ">=", "<", ">" };
	//檢查表達式是否包含邏輯運算符
	QString opStr;
	for (const auto& op : logicOp)
	{
		if (expr.contains(op))
		{
			opStr = op;
			break;
		}
	}

	static const QHash<QString, RESERVE> hash = {
		{ "==", TK_EQ }, // "=="
		{ "!=", TK_NEQ }, // "!="
		{ ">", TK_GT }, // ">"
		{ "<", TK_LT }, // "<"
		{ ">=", TK_GEQ }, // ">="
		{ "<=", TK_LEQ }, // "<="
	};

	//以邏輯符號為分界分割左右邊
	QStringList exprList;
	QVariant A;
	QVariant B;
	if (opStr.isEmpty())
		return false;

	exprList = expr.split(opStr, Qt::SkipEmptyParts);

	if (exprList.size() != 2)
		return false;

	A = exprList.at(0).trimmed();
	B = exprList.at(1).trimmed();

	RESERVE type = hash.value(opStr, TK_UNK);
	if (type == TK_UNK)
		return false;

	result = compare(A, B, type);

	if (ret != nullptr)
		*ret = static_cast<T>(result);

	return true;
}

template <typename T>
bool Parser::exprTo(T value, QString expr, T* ret)
{
	using TKSymbolTable = exprtk::symbol_table<double>;
	using TKExpression = exprtk::expression<double>;
	using TKParser = exprtk::parser<double>;

	constexpr auto varName = "A";

	TKSymbolTable symbolTable;
	symbolTable.add_constants();
	double dvalue = static_cast<double>(value);
	symbolTable.add_variable(varName, dvalue);


	TKParser parser;
	TKExpression expression;
	expression.register_symbol_table(symbolTable);

	QString newExpr = QString("%1 %2").arg(varName).arg(expr);

	const std::string exprStr = newExpr.toStdString();
	if (!parser.compile(exprStr, expression))
		return false;

	expression.value();
	dvalue = symbolTable.variable_ref(varName);

	if (ret != nullptr)
		*ret = static_cast<T>(dvalue);
	return true;
}

//嘗試取指定位置的token轉為字符串
bool Parser::checkString(const TokenMap& TK, qint64 idx, QString* ret)
{
	if (!TK.contains(idx))
		return false;
	if (ret == nullptr)
		return false;

	RESERVE type = TK.value(idx, Token{}).type;
	QVariant var = TK.value(idx, Token{}).data;

	if (!var.isValid())
		return false;

	if (type == TK_CSTRING)
	{
		*ret = var.toString();
	}
	else if (type == TK_STRING || type == TK_CMD || type == TK_NAME || type == TK_LABELVAR || type == TK_CAOS)
	{
		//檢查是否為區域變量
		VariantSafeHash args = getLocalVars();
		QString varName = var.toString();
		if (args.contains(varName))
		{
			QVariant::Type vtype = args.value(varName).type();
			if (vtype == QVariant::Int || vtype == QVariant::LongLong || vtype == QVariant::Double || vtype == QVariant::String)
				*ret = args.value(varName).toString();
			else
				return false;

			return true;
		}

		updateGlobalVariables();
		if (isGlobalVarContains(varName))
		{
			*ret = getGlobalVarValue(varName).toString();
		}
		else
			*ret = var.toString();
	}
	else
		return false;

	return true;
}

//嘗試取指定位置的token轉為整數
bool Parser::checkInteger(const TokenMap& TK, qint64 idx, qint64* ret)
{
	if (!TK.contains(idx))
		return false;
	if (ret == nullptr)
		return false;

	RESERVE type = TK.value(idx).type;
	QVariant var = TK.value(idx).data;

	if (!var.isValid())
		return false;
	if (type == TK_CSTRING)
		return false;

	if (type == TK_INT)
	{
		bool ok = false;
		qint64 value = var.toLongLong(&ok);
		if (!ok)
			return false;
		*ret = value;
	}
	else if (type == TK_STRING || type == TK_CMD || type == TK_NAME || type == TK_LABELVAR || type == TK_CAOS)
	{
		//檢查是否為區域變量
		VariantSafeHash args = getLocalVars();
		QString varName = var.toString();

		if (args.contains(varName)
			&& (args.value(varName).type() == QVariant::Int
				|| args.value(varName).type() == QVariant::LongLong
				|| args.value(varName).type() == QVariant::Double))
		{
			*ret = args.value(varName).toLongLong();
		}
		else if (args.contains(varName) && args.value(varName).type() == QVariant::String)
		{
			bool ok;
			qint64 value = args.value(varName).toLongLong(&ok);
			if (ok)
				*ret = value;
			else
				return false;

			return true;
		}

		updateGlobalVariables();
		if (isGlobalVarContains(varName))
		{
			QVariant gVar = getGlobalVarValue(varName);
			if (gVar.type() == QVariant::Int || gVar.type() == QVariant::LongLong || gVar.type() == QVariant::Double)
			{
				bool ok;
				qint64 value = gVar.toLongLong(&ok);
				if (ok)
					*ret = value;
				else
					return false;
			}
			else
				return false;
		}
		else
		{
			QString varValueStr = var.toString();
			replaceToVariable(varValueStr);

			qint64 value = 0;
			if (!exprTo(varValueStr, &value))
				return false;

			*ret = value;
		}
	}
	else
		return false;

	return true;
}

//嘗試取指定位置的token轉為QVariant
bool Parser::toVariant(const TokenMap& TK, qint64 idx, QVariant* ret)
{
	if (!TK.contains(idx))
		return false;
	if (ret == nullptr)
		return false;

	RESERVE type = TK.value(idx).type;
	QVariant var = TK.value(idx).data;
	if (!var.isValid())
		return false;

	if (type == TK_STRING || type == TK_CMD || type == TK_NAME || type == TK_LABELVAR || type == TK_CAOS)
	{
		VariantSafeHash args = getLocalVars();
		QString varName = var.toString();
		if (args.contains(varName))
		{
			QVariant::Type vtype = args.value(varName).type();
			if (vtype == QVariant::Int || vtype == QVariant::LongLong || vtype == QVariant::Double || vtype == QVariant::String)
				*ret = args.value(varName).toString();
			else
				return false;

			return true;
		}

		updateGlobalVariables();
		if (isGlobalVarContains(varName))
		{
			QVariant gVar = getGlobalVarValue(varName);
			bool ok;
			qint64 value = gVar.toLongLong(&ok);
			if (ok)
				*ret = value;
			else
			{
				*ret = gVar.toString();
			}
		}
		else
			*ret = var.toString();
	}
	else
	{
		*ret = var;
	}

	return true;
}

//嘗試取指定位置的token轉為按照double -> qint64 -> string順序檢查
QVariant Parser::checkValue(const TokenMap TK, qint64 idx, QVariant::Type type)
{
	QVariant varValue;
	qint64 ivalue;
	//double dvalue;
	QString text;
	//if (checkDouble(currentLineTokens_, idx, &dvalue))
	//{
		//varValue = dvalue;
	//}
	if (checkInteger(currentLineTokens_, idx, &ivalue))
	{
		varValue = ivalue;
	}
	else if (checkString(currentLineTokens_, idx, &text))
	{
		varValue = text;
	}
	else
	{
		if (type == QVariant::Int || type == QVariant::LongLong || type == QVariant::Double)
			varValue = 0ll;
		else if (type == QVariant::String)
			varValue = "";
	}

	return varValue;
}

//檢查跳轉是否滿足，和跳轉的方式
qint64 Parser::checkJump(const TokenMap& TK, qint64 idx, bool expr, JumpBehavior behavior)
{
	bool okJump = false;
	if (behavior == JumpBehavior::FailedJump)
	{
		okJump = !expr;
	}
	else
	{
		okJump = expr;
	}

	if (okJump)
	{
		QString label;
		qint64 line = 0;
		if (TK.contains(idx))
		{
			QVariant var = checkValue(TK, idx, QVariant::Double);//這裡故意用Double，這樣最後沒有結果時強制報參數錯誤
			QVariant::Type type = var.type();
			if (type == QVariant::String)
			{
				label = var.toString();
			}
			else if (type == QVariant::Int || type == QVariant::LongLong || type == QVariant::Double)
			{
				bool ok = false;
				qint64 value = 0;
				value = var.toLongLong(&ok);
				if (ok)
					line = value;
			}
			else
				return Parser::kArgError;

		}

		if (label.isEmpty() && line == 0)
			line = -1;

		if (!label.isEmpty())
		{
			jump(label, false);
		}
		else if (line != 0)
		{
			jump(line, false);
		}
		else
			return Parser::kArgError;

		return Parser::kHasJump;
	}

	return Parser::kNoChange;
}

void Parser::intsertLocalVar(const QString& name, const QVariant& value)
{
	QVariantHash& hash = getLocalVarsRef();
	if (value.isValid())
	{
		QVariant::Type type = value.type();
		if (type != QVariant::LongLong && type != QVariant::String)
		{
			bool ok = false;
			qint64 val = value.toLongLong(&ok);
			if (ok)
				hash.insert(name, val);
			else
				hash.insert(name, 0ll);
			return;
		}
		hash.insert(name, value);
	}

}

void Parser::insertGlobalVar(const QString& name, const QVariant& value)
{
	if (globalVarLock_ == nullptr)
		return;

	QWriteLocker locker(globalVarLock_);
	if (variables_ != nullptr)
	{
		if (value.isValid())
		{
			QVariant::Type type = value.type();
			if (type != QVariant::LongLong && type != QVariant::String)
			{
				bool ok = false;
				qint64 val = value.toLongLong(&ok);
				if (ok)
					variables_->insert(name, val);
				else
					variables_->insert(name, 0ll);
				return;
			}
			variables_->insert(name, value);
		}
	}
}

void Parser::insertGlobalVar(const QVariantHash& hash)
{
	if (globalVarLock_ == nullptr)
		return;

	SPD_LOG(g_logger_name, "inserting global system vars");

	QWriteLocker locker(globalVarLock_);

	if (variables_ != nullptr)
	{
		for (auto it = hash.cbegin(); it != hash.cend(); ++it)
		{
			QVariant::Type type = it.value().type();
			if (type != QVariant::LongLong && type != QVariant::String)
			{
				bool ok = false;
				qint64 val = it.value().toLongLong(&ok);
				if (ok)
					variables_->insert(it.key(), val);
				else
					variables_->insert(it.key(), 0ll);
				continue;
			}
			variables_->insert(it.key(), it.value());
		}
	}

	SPD_LOG(g_logger_name, "inserting global system vars done");
}

//將表達式中所有變量替換成實際數值
void Parser::replaceToVariable(QString& expr)
{
	QVector<QPair<QString, QVariant>> tmpvec;

	VariantSafeHash args = getLocalVars();
	for (auto it = args.cbegin(); it != args.cend(); ++it)
		tmpvec.append(qMakePair(it.key(), it.value()));

	//按照長度排序，這樣才能先替換長的變量
	std::sort(tmpvec.begin(), tmpvec.end(), [](const QPair<QString, QVariant>& a, const QPair<QString, QVariant>& b)
		{
			if (a.first.length() == b.first.length())
			{
				//長度依樣則按照字母順序排序
				static const QLocale locale;
				static const QCollator collator(locale);
				return collator.compare(a.first, b.first) < 0;
			}

			return a.first.length() > b.first.length();
		});


	for (const QPair<QString, QVariant> it : tmpvec)
	{
		QString name = it.first;
		QVariant value = it.second;
		if (value.type() == QVariant::String)
			expr.replace(name, QString(R"('%1')").arg(value.toString().trimmed()));
		else
			expr.replace(name, QString::number(value.toLongLong()));
	}

	tmpvec.clear();

	updateGlobalVariables();

	QVariantHash* pglobalhash = getGlobalVarPointer();
	for (auto it = pglobalhash->cbegin(); it != pglobalhash->cend(); ++it)
	{
		tmpvec.append(qMakePair(it.key(), it.value()));
	}

	//按照長度排序，這樣才能先替換長的變量
	std::sort(tmpvec.begin(), tmpvec.end(), [](const QPair<QString, QVariant>& a, const QPair<QString, QVariant>& b)
		{
			return a.first.length() > b.first.length();
		});

	for (const QPair<QString, QVariant> it : tmpvec)
	{
		QString name = it.first;
		QVariant value = it.second;
		if (value.type() == QVariant::String)
			expr.replace(name, QString(R"('%1')").arg(value.toString().trimmed()));
		else
			expr.replace(name, QString::number(value.toLongLong()));
	}

	expr.replace("^", " xor ");
	expr.replace("~", " nor ");
	expr.replace("&&", " and ");
	expr.replace("||", " or ");

	expr = expr.trimmed();
}

//行跳轉
void Parser::jump(qint64 line, bool noStack)
{
	if (!noStack)
		jmpStack_.push(lineNumber_ + 1);
	lineNumber_ += line;
}

//指定行跳轉
void Parser::jumpto(qint64 line, bool noStack)
{
	if (line - 1 < 0)
		line = 1;
	if (!noStack)
		jmpStack_.push(lineNumber_ + 1);
	lineNumber_ = line - 1;
}

//標記跳轉
bool Parser::jump(const QString& name, bool noStack)
{
	if (name.toLower() == "back")
	{
		if (!jmpStack_.isEmpty())
		{
			qint64 returnIndex = jmpStack_.pop();//jump行號出棧
			qint64 jumpLineCount = returnIndex - lineNumber_;
			jump(jumpLineCount, true);
			return true;
		}
		return false;
	}
	else if (name.toLower() == "return")
	{
		if (!callArgsStack_.isEmpty())
			callArgsStack_.pop();//call行號出棧
		if (!localVarStack_.isEmpty())
			localVarStack_.pop();//label局變量出棧
		if (!callStack_.isEmpty())
		{
			qint64 returnIndex = callStack_.pop();
			qint64 jumpLineCount = returnIndex - lineNumber_;
			jump(jumpLineCount, true);
			return true;
		}
		return false;
	}

	qint64 jumpLine = matchLineFromLabel(name);
	if (jumpLine == -1)
	{
		handleError(kLabelError);
		return false;
	}

	if (!noStack)
		jmpStack_.push(lineNumber_ + 1);

	qint64 jumpLineCount = jumpLine - lineNumber_;
	jump(jumpLineCount, true);
	return true;
}

//檢查是否需要跳過整個function代碼塊
bool Parser::checkCallStack()
{
	const QString cmd = currentLineTokens_.value(0).data.toString();
	if (cmd != "function")
		return false;

	const QString funname = currentLineTokens_.value(1).data.toString();
	do
	{
		//棧為空時直接跳過整個代碼塊
		if (callStack_.isEmpty())
			break;

		qint64 lastRow = callStack_.top() - 1;

		//匹配棧頂紀錄的function名稱與當前執行名稱是否一致
		QString oldname = tokens_.value(lastRow).value(1).data.toString();
		if (oldname != funname)
			break;

		return false;
	} while (false);

	if (!functionChunks_.contains(funname))
		return false;

	FunctionChunk chunk = functionChunks_.value(funname);
	jump(chunk.end - chunk.begin + 1, true);
	return true;
}

//處理"標記"，檢查是否有聲明局變量
void Parser::processLabel()
{
	QString token = currentLineTokens_.value(0).data.toString();
	if (token == "label")
		return;

	QVariantList args = getArgsRef();
	QVariantHash labelVars;
	for (qint64 i = kCallPlaceHoldSize; i < currentLineTokens_.size(); ++i)
	{
		Token token = currentLineTokens_.value(i);
		if (token.type == TK_LABELVAR)
		{
			QString labelName = token.data.toString();
			if (labelName.isEmpty())
				continue;

			if (!args.isEmpty() && (args.size() > (i - kCallPlaceHoldSize)) && (args.at(i - kCallPlaceHoldSize).isValid()))
				labelVars.insert(labelName, args.at(i - kCallPlaceHoldSize));
		}
	}

	localVarStack_.push(labelVars);

}

//處理"結束"
void Parser::processClean()
{
	callStack_.clear();
	jmpStack_.clear();
	callArgsStack_.clear();
	localVarStack_.clear();
}

//處理所有核心命令之外的所有命令
qint64 Parser::processCommand()
{
	TokenMap tokens = getCurrentTokens();
	Token commandToken = tokens.value(0);
	QString commandName = commandToken.data.toString();
	qint64 status = kNoChange;
	if (commandRegistry_.contains(commandName))
	{
		CommandRegistry function = commandRegistry_.value(commandName, nullptr);
		if (function == nullptr)
		{
			qDebug() << "Command not registered:" << commandName;
			return status;
		}

		status = function(lineNumber_, tokens);
	}
	else
	{
		qDebug() << "Command not found:" << commandName;

	}
	return status;
}

void Parser::processLocalVariable()
{
	QString varNameStr = getToken<QString>(0);
	QStringList varNames = varNameStr.split(util::rexComma, Qt::SkipEmptyParts);
	qint64 varCount = varNames.count();

	//取區域變量表
	QVariantHash& args = getLocalVarsRef();

	for (qint64 i = 0; i < varCount; ++i)
	{
		QString varName = varNames.at(i);
		if (varName.isEmpty())
		{
			args.insert(varName, 0);
			continue;
		}

		if (i + 1 >= currentLineTokens_.size())
		{
			args.insert(varName, 0);
			continue;
		}

		QVariant varValue = checkValue(currentLineTokens_, i + 1, QVariant::LongLong);

		args.insert(varName, varValue);
	}
}

bool Parser::checkFuzzyValue(const QString& varName, QVariant varValue, QVariant* pvalue)
{
	QString opStr = varValue.toString().simplified();
	if (!opStr.startsWith(kFuzzyPrefix))
		return false;

	SignalDispatcher& signalDispatcher = SignalDispatcher::getInstance();
	QString valueStr = varValue.toString().simplified();

	//檢查是否使用?讓使用者輸入
	QString msg;
	checkString(currentLineTokens_, 3, &msg);

	if (valueStr.startsWith(kFuzzyPrefix + QString("2")))//整數輸入框
	{
		varValue.clear();
		emit signalDispatcher.inputBoxShow(msg, QInputDialog::IntInput, &varValue);
		if (!varValue.isValid())
			return false;
		varValue = varValue.toLongLong();
	}
	else if ((valueStr.startsWith(kFuzzyPrefix) && valueStr.endsWith(kFuzzyPrefix)) || valueStr.startsWith(kFuzzyPrefix + QString("1")))// 字串輸入框
	{
		varValue.clear();
		emit signalDispatcher.inputBoxShow(msg, QInputDialog::TextInput, &varValue);
		if (!varValue.isValid())
			return false;
		varValue = varValue.toString();
	}

	if (!varValue.isValid())
		return false;

	if (pvalue == nullptr)
		return false;

	*pvalue = varValue;
	return true;
}

//處理"變量"
void Parser::processVariable(RESERVE type)
{

	switch (type)
	{
	case TK_VARDECL:
	{
		//取第一個參數
		QString varName = getToken<QString>(1);
		if (varName.isEmpty())
			break;

		//取第二個參數
		QVariant varValue = getToken<QVariant>(2);
		if (!varValue.isValid())
			break;

		//檢查第二參數是否為邏輯運算符
		RESERVE op = getTokenType(2);
		if (!operatorTypes.contains(op))
		{
			QString text;
			qint64 ivalue = 0;
			if (checkString(currentLineTokens_, 2, &text))
			{
				varValue = text;
			}
			else if (checkInteger(currentLineTokens_, 2, &ivalue))
			{
				varValue = ivalue;
			}

			if (checkFuzzyValue(varName, varValue, &varValue))
			{
				insertGlobalVar(varName, varValue);
				break;
			}

			QString varStr = varValue.toString();
			if (processGetSystemVarValue(varName, varStr, varValue))
			{
				insertVar(varName, varValue);
				break;
			}

			//插入全局變量表
			insertGlobalVar(varName, varValue);
			break;
		}

		updateGlobalVariables();

		//下面是變量比較數值，先檢查變量是否存在
		if (!isGlobalVarContains(varName))
		{
			if (op != TK_DEC && op != TK_INC)
				break;
			else
				insertGlobalVar(varName, 0);
		}

		if (op == TK_UNK)
			break;

		//取第三個參數
		varValue = getToken<QVariant>(3);
		//檢查第三個是否合法 ，如果不合法 且第二參數不是++或--，則跳出
		if (!varValue.isValid() && op != TK_DEC && op != TK_INC)
			break;

		//將第一參數，要被運算的變量原數值取出
		QVariant gVar = getGlobalVarValue(varName);
		//開始計算新值
		variableCalculate(op, &gVar, varValue);
		if (isGlobalVarContains(varName))
			insertGlobalVar(varName, gVar);
		break;
	}
	case TK_VARFREE:
	{
		QString varName = getToken<QString>(1);
		if (varName.isEmpty())
			break;

		if (isGlobalVarContains(varName))
			removeGlobalVar(varName);
		break;
	}
	case TK_VARCLR:
	{
		clearGlobalVar();
		break;
	}
	default:
		break;
	}
}

//處理多變量聲明
void Parser::processMultiVariable()
{
	QString varNameStr = getToken<QString>(0);
	QStringList varNames = varNameStr.split(util::rexComma, Qt::SkipEmptyParts);
	qint64 varCount = varNames.count();

	//取區域變量表
	VariantSafeHash args = getLocalVars();


	for (qint64 i = 0; i < varCount; ++i)
	{
		QString varName = varNames.at(i);
		if (varName.isEmpty())
		{
			insertGlobalVar(varName, 0);
			continue;
		}

		if (i + 1 >= currentLineTokens_.size())
		{
			insertGlobalVar(varName, 0);
			continue;
		}

		QVariant varValue = checkValue(currentLineTokens_, i + 1, QVariant::LongLong);

		insertGlobalVar(varName, varValue);
	}
}

//處理變量自增自減
void Parser::processVariableIncDec()
{
	QString varName = getToken<QString>(0);
	if (varName.isEmpty())
		return;

	RESERVE op = getTokenType(1);


	QVariantHash args = getLocalVars();

	qint64 value = 0;
	if (args.contains(varName))
		value = args.value(varName, 0).toLongLong();
	else
		value = getVar<qint64>(varName);

	if (op == TK_DEC)
		--value;
	else if (op == TK_INC)
		++value;

	insertVar(varName, value);
}

//處理變量計算
void Parser::processVariableCAOs()
{
	QString varName = getToken<QString>(0);
	if (varName.isEmpty())
		return;

	RESERVE op = getTokenType(1);

	QVariant var = checkValue(currentLineTokens_, 0, QVariant::LongLong);
	QVariant::Type type = var.type();
	QString opStr = getToken<QString>(1);

	QVariant varValue = checkValue(currentLineTokens_, 2, QVariant::LongLong);

	QString varValueStr = varValue.toString();
	replaceToVariable(varValueStr);

	if (type == QVariant::String)
	{
		if (varValue.type() == QVariant::String && op == TK_ADD)
		{
			QString str = var.toString() + varValue.toString();
			insertVar(varName, str);
		}
		return;
	}
	else
	{
		if (type == QVariant::Int || type == QVariant::LongLong || type == QVariant::Double)
		{
			qint64 value = 0;
			if (!exprTo(var.toLongLong(), QString("%1 %2").arg(opStr).arg(QString::number(varValue.toDouble(), 'f', 16)), &value))
				return;
			var = value;
		}
		else
			return;
	}

	insertVar(varName, var);
}

//處理變量表達式
void Parser::processVariableExpr()
{
	QString varName = getToken<QString>(0);
	if (varName.isEmpty())
		return;

	QVariant varValue = checkValue(currentLineTokens_, 0, QVariant::LongLong);

	QString expr;
	if (!checkString(currentLineTokens_, 1, &expr))
		return;

	replaceToVariable(expr);

	QVariant::Type type = varValue.type();

	QVariant result;
	if (type == QVariant::Int || type == QVariant::LongLong || type == QVariant::Double)
	{
		qint64 value = 0;
		if (!exprTo(expr, &value))
			return;
		result = value;
	}
	else
		return;

	insertVar(varName, result);
}

//根據關鍵字取值保存到變量
bool Parser::processGetSystemVarValue(const QString& varName, QString& valueStr, QVariant& varValue)
{
	Injector& injector = Injector::getInstance();
	if (injector.server.isNull())
		return false;

	QString trimmedStr = valueStr.simplified().toLower();

	enum SystemVarName
	{
		kPlayerInfo,
		kMagicInfo,
		kSkillInfo,
		kPetInfo,
		kPetSkillInfo,
		kMapInfo,
		kItemInfo,
		kEquipInfo,
		kPetEquipInfo,
		kPartyInfo,
		kChatInfo,
		kDialogInfo,
		kPointInfo,
		kBattleInfo,
	};

	const QHash<QString, SystemVarName> systemVarNameHash = {
		{ "player", kPlayerInfo },
		{ "magic", kMagicInfo },
		{ "skill", kSkillInfo },
		{ "pet", kPetInfo },
		{ "petskill", kPetSkillInfo },
		{ "map", kMapInfo },
		{ "item", kItemInfo },
		{ "equip", kEquipInfo },
		{ "petequip", kPetEquipInfo },
		{ "party", kPartyInfo },
		{ "chat", kChatInfo },
		{ "dialog", kDialogInfo },
		{ "point", kPointInfo },
		{ "battle", kBattleInfo },

	};

	if (!systemVarNameHash.contains(trimmedStr))
		return false;

	SystemVarName index = systemVarNameHash.value(trimmedStr);
	bool bret = false;
	switch (index)
	{
	case kPlayerInfo:
	{
		QString typeStr;
		checkString(currentLineTokens_, 3, &typeStr);
		typeStr = typeStr.simplified().toLower();
		if (typeStr.isEmpty())
			break;

		VariantSafeHash& hashpc = injector.server->hashpc;
		if (!hashpc.contains(typeStr))
			break;

		varValue = hashpc.value(typeStr);
		bret = varValue.isValid();
		break;
	}
	case kMagicInfo:
	{
		qint64 magicIndex = -1;
		if (!checkInteger(currentLineTokens_, 3, &magicIndex))
			break;

		QString typeStr;
		checkString(currentLineTokens_, 4, &typeStr);
		typeStr = typeStr.simplified().toLower();
		if (typeStr.isEmpty())
			break;

		util::SafeHash<int, QVariantHash> hashmagic = injector.server->hashmagic;
		if (!hashmagic.contains(magicIndex))
			break;

		QVariantHash hash = hashmagic.value(magicIndex);
		if (!hash.contains(typeStr))
			break;

		varValue = hash.value(typeStr);
		bret = varValue.isValid();
		break;
	}
	case kSkillInfo:
	{
		qint64 skillIndex = -1;
		if (!checkInteger(currentLineTokens_, 3, &skillIndex))
			break;

		QString typeStr;
		checkString(currentLineTokens_, 4, &typeStr);
		typeStr = typeStr.simplified().toLower();
		if (typeStr.isEmpty())
			break;

		util::SafeHash<int, QVariantHash> hashskill = injector.server->hashskill;
		if (!hashskill.contains(skillIndex))
			break;

		QVariantHash hash = hashskill.value(skillIndex);
		if (!hash.contains(typeStr))
			break;

		varValue = hash.value(typeStr);
		bret = varValue.isValid();
		break;
	}
	case kPetInfo:
	{
		qint64 petIndex = -1;
		if (!checkInteger(currentLineTokens_, 3, &petIndex))
		{
			QString typeStr;
			if (!checkString(currentLineTokens_, 3, &typeStr))
				break;

			if (typeStr == "count")
			{
				varValue = injector.server->hashpet.size();
				bret = true;
			}


			break;
		}

		QString typeStr;
		checkString(currentLineTokens_, 4, &typeStr);
		typeStr = typeStr.simplified().toLower();
		if (typeStr.isEmpty())
			break;

		util::SafeHash<int, QVariantHash> hashpet = injector.server->hashpet;
		if (!hashpet.contains(petIndex))
			break;

		QVariantHash hash = hashpet.value(petIndex);
		if (!hash.contains(typeStr))
			break;

		varValue = hash.value(typeStr);
		bret = varValue.isValid();
		break;
	}
	case kPetSkillInfo:
	{
		qint64 petIndex = -1;
		if (!checkInteger(currentLineTokens_, 3, &petIndex))
			break;

		qint64 skillIndex = -1;
		if (!checkInteger(currentLineTokens_, 4, &skillIndex))
			break;

		QString typeStr;
		checkString(currentLineTokens_, 5, &typeStr);
		typeStr = typeStr.simplified().toLower();
		if (typeStr.isEmpty())
			break;

		util::SafeHash<int, QHash<int, QVariantHash>>& hashpetskill = injector.server->hashpetskill;
		if (!hashpetskill.contains(petIndex))
			break;

		util::SafeHash<int, QVariantHash> hash = hashpetskill.value(petIndex);
		if (!hash.contains(skillIndex))
			break;

		QVariantHash hash2 = hash.value(skillIndex);
		if (!hash2.contains(typeStr))
			break;

		varValue = hash2.value(typeStr);
		bret = varValue.isValid();
		break;
	}
	case kMapInfo:
	{
		QString typeStr;
		checkString(currentLineTokens_, 3, &typeStr);
		typeStr = typeStr.simplified().toLower();
		if (typeStr.isEmpty())
			break;

		VariantSafeHash& hashmap = injector.server->hashmap;
		if (!hashmap.contains(typeStr))
			break;

		varValue = hashmap.value(typeStr);
		bret = varValue.isValid();
		break;
	}
	case kItemInfo:
	{
		qint64 itemIndex = -1;
		if (!checkInteger(currentLineTokens_, 3, &itemIndex))
		{
			QString typeStr;
			if (checkString(currentLineTokens_, 3, &typeStr))
			{
				typeStr = typeStr.simplified().toLower();
				if (typeStr == "space")
				{
					QVector<int> itemIndexs;
					qint64 size = 0;
					if (injector.server->getItemEmptySpotIndexs(&itemIndexs))
						size = itemIndexs.size();

					varValue = size;
					bret = varValue.isValid();
					break;
				}
				else if (typeStr == "count")
				{
					QString itemName;
					if (!checkString(currentLineTokens_, 4, &itemName))
						break;
					QVector<int> v;
					qint64 count = 0;
					if (injector.server->getItemIndexsByName(itemName, "", &v))
					{
						for (const int it : v)
							count += injector.server->getPC().item[it].pile;
					}

					varValue = count;
					bret = varValue.isValid();
				}
				else
				{
					QString cmpStr = typeStr;
					if (cmpStr.isEmpty())
						break;

					QString memo;
					checkString(currentLineTokens_, 4, &memo);

					QVector<int> itemIndexs;
					if (injector.server->getItemIndexsByName(cmpStr, memo, &itemIndexs))
					{
						int index = itemIndexs.front();
						if (itemIndexs.front() >= CHAR_EQUIPPLACENUM)
							varValue = index + 1 - CHAR_EQUIPPLACENUM;
						else
							varValue = index + 1 + 100;
						bret = varValue.isValid();
					}
				}
			}
		}

		QString typeStr;
		checkString(currentLineTokens_, 4, &typeStr);
		typeStr = typeStr.simplified().toLower();
		if (typeStr.isEmpty())
			break;

		util::SafeHash<int, QVariantHash> hashitem = injector.server->hashitem;
		if (!hashitem.contains(itemIndex))
			break;

		QVariantHash hash = hashitem.value(itemIndex);
		if (!hash.contains(typeStr))
			break;

		varValue = hash.value(typeStr);
		bret = varValue.isValid();
		break;
	}
	case kEquipInfo:
	{
		qint64 itemIndex = -1;
		if (!checkInteger(currentLineTokens_, 3, &itemIndex))
			break;

		QString typeStr;
		checkString(currentLineTokens_, 4, &typeStr);
		typeStr = typeStr.simplified().toLower();
		if (typeStr.isEmpty())
			break;

		util::SafeHash<int, QVariantHash> hashitem = injector.server->hashequip;
		if (!hashitem.contains(itemIndex))
			break;

		QVariantHash hash = hashitem.value(itemIndex);
		if (!hash.contains(typeStr))
			break;

		varValue = hash.value(typeStr);
		bret = varValue.isValid();
		break;
	}
	case kPetEquipInfo:
	{
		qint64 petIndex = -1;
		if (!checkInteger(currentLineTokens_, 3, &petIndex))
			break;

		qint64 itemIndex = -1;
		if (!checkInteger(currentLineTokens_, 4, &itemIndex))
			break;

		QString typeStr;
		checkString(currentLineTokens_, 5, &typeStr);
		typeStr = typeStr.simplified().toLower();
		if (typeStr.isEmpty())
			break;

		util::SafeHash<int, QHash<int, QVariantHash>>& hashpetequip = injector.server->hashpetequip;
		if (!hashpetequip.contains(petIndex))
			break;

		QHash<int, QVariantHash> hash = hashpetequip.value(petIndex);
		if (!hash.contains(itemIndex))
			break;

		QVariantHash hash2 = hash.value(itemIndex);
		if (!hash2.contains(typeStr))
			break;

		varValue = hash2.value(typeStr);
		bret = varValue.isValid();
		break;
	}
	case kPartyInfo:
	{
		qint64 partyIndex = -1;
		if (!checkInteger(currentLineTokens_, 3, &partyIndex))
			break;

		QString typeStr;
		checkString(currentLineTokens_, 4, &typeStr);
		typeStr = typeStr.simplified().toLower();
		if (typeStr.isEmpty())
			break;

		util::SafeHash<int, QVariantHash> hashparty = injector.server->hashparty;
		if (!hashparty.contains(partyIndex))
			break;

		QVariantHash hash = hashparty.value(partyIndex);
		if (!hash.contains(typeStr))
			break;

		varValue = hash.value(typeStr);
		bret = varValue.isValid();
		break;
	}
	case kChatInfo:
	{
		qint64 chatIndex = -1;
		if (!checkInteger(currentLineTokens_, 3, &chatIndex))
			break;

		util::SafeHash<int, QVariant> hashchat = injector.server->hashchat;
		if (!hashchat.contains(chatIndex))
			break;

		varValue = hashchat.value(chatIndex);
		bret = varValue.isValid();
		break;
	}
	case kDialogInfo:
	{
		qint64 dialogIndex = -1;
		if (!checkInteger(currentLineTokens_, 3, &dialogIndex))
			break;

		util::SafeHash<int, QVariant> hashdialog = injector.server->hashdialog;

		if (dialogIndex == -1)
		{
			QStringList texts;
			for (qint64 i = 0; i < MAX_DIALOG_LINE; ++i)
			{
				if (!hashdialog.contains(i))
					break;

				texts << hashdialog.value(i).toString().simplified();
			}

			varValue = texts.join("\n");
		}
		else
		{
			if (!hashdialog.contains(dialogIndex))
				break;

			varValue = hashdialog.value(dialogIndex);
		}


		bret = varValue.isValid();
		break;
	}
	case kPointInfo:
	{
		QString typeStr;
		checkString(currentLineTokens_, 3, &typeStr);
		typeStr = typeStr.simplified().toLower();
		if (typeStr.isEmpty())
			break;


		injector.server->IS_WAITFOR_EXTRA_DIALOG_INFO_FLAG = true;
		injector.server->shopOk(2);

		QElapsedTimer timer; timer.start();
		for (;;)
		{
			if (isInterruptionRequested())
				return false;

			if (timer.hasExpired(5000))
				break;

			if (!injector.server->IS_WAITFOR_EXTRA_DIALOG_INFO_FLAG)
				break;

			QThread::msleep(100);
		}
		//qint64 rep = 0;   // 聲望
		//qint64 ene = 0;   // 氣勢
		//qint64 shl = 0;   // 貝殼
		//qint64 vit = 0;   // 活力
		//qint64 pts = 0;   // 積分
		//qint64 vip = 0;   // 會員點
		currencydata_t currency = injector.server->currencyData;
		if (typeStr == "exp")
		{
			varValue = currency.expbufftime;
			bret = varValue.isValid();
		}
		else if (typeStr == "rep")
		{
			varValue = currency.prestige;
			bret = varValue.isValid();
		}
		else if (typeStr == "ene")
		{
			varValue = currency.energy;
			bret = varValue.isValid();
		}
		else if (typeStr == "shl")
		{
			varValue = currency.shell;
			bret = varValue.isValid();
		}
		else if (typeStr == "vit")
		{
			varValue = currency.vitality;
			bret = varValue.isValid();
		}
		else if (typeStr == "pts")
		{
			varValue = currency.points;
			bret = varValue.isValid();
		}
		else if (typeStr == "vip")
		{
			varValue = currency.VIPPoints;
			bret = varValue.isValid();
		}

		if (bret)
			injector.server->press(BUTTON_CANCEL);

		break;
	}
	case kBattleInfo:
	{
		qint64 index = -1;
		if (!checkInteger(currentLineTokens_, 3, &index))
		{
			QString typeStr;
			checkString(currentLineTokens_, 3, &typeStr);
			if (typeStr.isEmpty())
				break;

			if (typeStr == "round")
			{
				varValue = injector.server->BattleCliTurnNo;
				bret = varValue.isValid();
			}
			else if (typeStr == "field")
			{
				varValue = injector.server->hashbattlefield.get();
				bret = varValue.isValid();
			}
			break;
		}

		QString typeStr;
		checkString(currentLineTokens_, 4, &typeStr);

		util::SafeHash<int, QVariantHash> hashbattle = injector.server->hashbattle;
		if (!hashbattle.contains(index))
			break;

		QVariantHash hash = hashbattle.value(index);
		if (!hash.contains(typeStr))
			break;

		varValue = hash.value(typeStr);
		bret = varValue.isValid();
		break;
	}
	default:
		break;
	}

	return bret;
}

//處理if
bool Parser::processIfCompare()
{
	QString expr = getToken<QString>(1);

	replaceToVariable(expr);

	if (expr.contains("\""))
	{
		expr = expr.replace("\"", "\'");
	}

	double dvalue = 0.0;
	bool result = false;
	if (exprTo(expr, &dvalue))
	{
		result = dvalue > 0.0;
	}

	//RESERVE op;
	//if (!checkRelationalOperator(TK, 2, &op))
	//	return Parser::kArgError;

	//if (!toVariant(TK, 3, &b))
	//	return Parser::kArgError;

	//return checkJump(TK, 4, compare(a, b, op), SuccessJump);

	return checkJump(currentLineTokens_, 2, result, FailedJump) == kHasJump;
}

//處理隨機數
void Parser::processRandom()
{
	do
	{
		QString varName = getToken<QString>(1);
		if (varName.isEmpty())
			break;

		qint64 min = 0;
		checkInteger(currentLineTokens_, 2, &min);
		qint64 max = 0;
		checkInteger(currentLineTokens_, 3, &max);

		if (min == 0 && max == 0)
			break;

		if (min > 0 && max == 0)
		{
			std::random_device rd;
			std::mt19937_64 gen(rd());
			std::uniform_int_distribution<qint64> distribution(0, min);
			insertVar(varName, distribution(gen));
			break;
		}
		else if (min > max)
		{
			insertVar(varName, 0);
			break;
		}

		std::random_device rd;
		std::mt19937_64 gen(rd());
		std::uniform_int_distribution<qint64> distribution(min, max);
		insertVar(varName, distribution(gen));
	} while (false);
}

//處理"格式化"
void Parser::processFormation()
{

	auto formatTime = [](qint64 seconds)->QString
	{
		qint64 hours = seconds / 3600ll;
		qint64 minutes = (seconds % 3600ll) / 60ll;
		qint64 remainingSeconds = seconds % 60ll;

		return QString(QObject::tr("%1 hour %2 min %3 sec")).arg(hours).arg(minutes).arg(remainingSeconds);
	};

	do
	{
		QString varName = getToken<QString>(1);
		if (varName.isEmpty())
			break;

		QString formatStr;
		checkString(currentLineTokens_, 2, &formatStr);
		if (formatStr.isEmpty())
			break;

		QVariantHash& args = getLocalVarsRef();

		QString key;
		QString keyWithTime;
		constexpr qint64 MAX_NESTING_DEPTH = 10;//最大嵌套深度
		for (qint64 i = 0; i < MAX_NESTING_DEPTH; ++i)
		{
			for (auto it = args.cbegin(); it != args.cend(); ++it)
			{
				key = QString("{:%1}").arg(it.key());

				if (formatStr.contains(key))
				{
					formatStr.replace(key, it.value().toString());
					continue;
				}

				keyWithTime = QString("{T:%1}").arg(it.key());
				if (formatStr.contains(keyWithTime))
				{
					qint64 seconds = it.value().toLongLong();
					formatStr.replace(keyWithTime, formatTime(seconds));
				}
			}

			//查找字符串中包含 {:變數名} 全部替換成變數數值

			for (auto it = variables_->cbegin(); it != variables_->cend(); ++it)
			{
				key = QString("{:%1}").arg(it.key());

				if (formatStr.contains(key))
				{
					formatStr.replace(key, it.value().toString());
					continue;
				}

				keyWithTime = QString("{T:%1}").arg(it.key());
				if (formatStr.contains(keyWithTime))
				{
					qint64 seconds = it.value().toLongLong();
					formatStr.replace(keyWithTime, formatTime(seconds));
				}
			}
		}

		QVariant varValue;
		qint64 argsize = tokens_.size();
		for (qint64 i = kFormatPlaceHoldSize; i < argsize; ++i)
		{
			varValue = checkValue(currentLineTokens_, i + 1, QVariant::String);

			key = QString("{:%1}").arg(i - kFormatPlaceHoldSize + 1);

			if (formatStr.contains(key))
			{
				formatStr.replace(key, varValue.toString());
				continue;
			}

			keyWithTime = QString("{T:%1}").arg(i - kFormatPlaceHoldSize + 1);
			if (formatStr.contains(keyWithTime))
				formatStr.replace(key, formatTime(varValue.toLongLong()));
		}

		static const QRegularExpression rexOut(R"(\[(\d+)\])");
		if ((varName.startsWith("out", Qt::CaseInsensitive) && varName.contains(rexOut)) || varName.toLower() == "out")
		{
			QRegularExpressionMatch match = rexOut.match(varName);
			qint64 color = QRandomGenerator::global()->bounded(0, 10);
			if (match.hasMatch())
			{
				QString str = match.captured(1);
				qint64 nColor = str.toLongLong();
				if (nColor >= 0 && nColor <= 10)
					color = nColor;
			}

			const QDateTime time(QDateTime::currentDateTime());
			const QString timeStr(time.toString("hh:mm:ss:zzz"));
			QString src = "\0";

			QString msg = (QString("[%1 | @%2]: %3\0") \
				.arg(timeStr)
				.arg(lineNumber_ + 1, 3, 10, QLatin1Char(' ')).arg(formatStr));



			Injector& injector = Injector::getInstance();
			if (!injector.server.isNull())
				injector.server->announce(formatStr, color);
			SignalDispatcher& signalDispatcher = SignalDispatcher::getInstance();
			emit signalDispatcher.appendScriptLog(msg, color);
		}
		else if ((varName.startsWith("say", Qt::CaseInsensitive) && varName.contains(rexOut)) || varName.toLower() == "say")
		{
			QRegularExpressionMatch match = rexOut.match(varName);
			qint64 color = QRandomGenerator::global()->bounded(0, 10);
			if (match.hasMatch())
			{
				QString str = match.captured(1);
				qint64 nColor = str.toInt();
				if (nColor >= 0 && nColor <= 10)
					color = nColor;
			}

			Injector& injector = Injector::getInstance();
			if (!injector.server.isNull())
				injector.server->talk(formatStr, color);
		}
		else
		{
			insertVar(varName, QVariant::fromValue(formatStr));
		}
	} while (false);
}

//檢查"調用"是否傳入參數
void Parser::checkArgs()
{
	//check rest of the tokens is exist push to stack 	QStack<QVariantList> callArgs_
	QVariantList list;
	for (qint64 i = kCallPlaceHoldSize; i < tokens_.value(lineNumber_).size(); ++i)
	{
		TokenMap map = tokens_.value(lineNumber_);
		Token token = map.value(i);
		QVariant var = checkValue(map, i, QVariant::String);

		if (token.type != TK_FUZZY)
			list.append(var);
		else
			list.append(0);
	}

	//無論如何都要在調用call之後壓入新的參數堆棧
	callArgsStack_.push(list);
}

//處理"調用"
bool Parser::processCall()
{
	RESERVE type = getTokenType(1);
	do
	{
		if (type != TK_NAME)
			break;

		QString functionName;
		checkString(currentLineTokens_, 1, &functionName);
		if (functionName.isEmpty())
			break;

		qint64 jumpLine = matchLineFromLabel(functionName);
		if (jumpLine == -1)
			break;

		qint64 currentLine = lineNumber_;
		checkArgs();
		if (!jump(functionName, true))
		{
			break;
		}
		callStack_.push(currentLine + 1); // Push the next line index to the call stack

		return true;

	} while (false);
	return false;
}

//處理"跳轉"
bool Parser::processGoto()
{
	RESERVE type = getTokenType(1);
	do
	{
		if (type == TK_NAME)
		{
			QString labelName = getToken<QString>(1);
			if (labelName.isEmpty())
				break;

			jump(labelName, false);
		}
		else
		{
			QVariant var = checkValue(currentLineTokens_, 1, QVariant::LongLong);
			QVariant::Type type = var.type();
			if (type == QVariant::Int || type == QVariant::LongLong || type == QVariant::Double)
			{
				qint64 jumpLineCount = var.toLongLong();
				if (jumpLineCount == 0)
					break;

				jump(jumpLineCount, false);
				return true;
			}
			else if (type == QVariant::String)
			{
				QString labelName = var.toString();
				if (labelName.isEmpty())
					break;

				jump(labelName, false);
				return true;
			}
		}

	} while (false);
	return false;
}

//處理"指定行跳轉"
bool Parser::processJump()
{
	//RESERVE type = getTokenType(1);
	do
	{
		QVariant var = checkValue(currentLineTokens_, 1, QVariant::LongLong);

		qint64 line = var.toLongLong();
		if (line <= 0)
			break;

		jumpto(line, false);
		return true;

	} while (false);

	return false;
}

//處理"返回"
void Parser::processReturn()
{
	if (!callArgsStack_.isEmpty())
		callArgsStack_.pop();//call行號出棧
	if (!localVarStack_.isEmpty())
		localVarStack_.pop();//label局變量出棧
	if (!callStack_.isEmpty())
	{
		qint64 returnIndex = callStack_.pop();
		qint64 jumpLineCount = returnIndex - lineNumber_;
		jump(jumpLineCount, true);
		return;
	}
	lineNumber_ = 0;
}

//處理"返回跳轉"
void Parser::processBack()
{
	if (!jmpStack_.isEmpty())
	{
		qint64 returnIndex = jmpStack_.pop();//jump行號出棧
		qint64 jumpLineCount = returnIndex - lineNumber_;
		jump(jumpLineCount, true);
		return;
	}
	lineNumber_ = 0;
}

//處理"變量"運算
void Parser::variableCalculate(RESERVE op, QVariant* pvar, const QVariant& varValue)
{
	if (nullptr == pvar)
		return;

	QVariant::Type type = pvar->type();

	switch (op)
	{
	case TK_ADD:
		*pvar = calc<qint64>(*pvar, varValue, op);
		break;
	case TK_SUB:
		*pvar = calc<qint64>(*pvar, varValue, op);
		break;
	case TK_MUL:
		*pvar = calc<qint64>(*pvar, varValue, op);
		break;
	case TK_DIV:
		*pvar = calc<qint64>(*pvar, varValue, op);
		break;
	case TK_INC:
		*pvar = calc<qint64>(*pvar, varValue, op);
		break;
	case TK_DEC:
		*pvar = calc<qint64>(*pvar, varValue, op);
		break;
	case TK_MOD:
		*pvar = pvar->toLongLong() % varValue.toLongLong();
		break;
	case TK_AND:
		*pvar = pvar->toLongLong() & varValue.toLongLong();
		break;
	case TK_OR:
		*pvar = pvar->toLongLong() | varValue.toLongLong();
		break;
	case TK_XOR:
		*pvar = pvar->toLongLong() ^ varValue.toLongLong();
		break;
	case TK_SHL:
		*pvar = pvar->toLongLong() << varValue.toLongLong();
		break;
	case TK_SHR:
		*pvar = pvar->toLongLong() >> varValue.toLongLong();
		break;
	default:
		//Q_UNREACHABLE();//基於lexer事前處理過不可能會執行到這裡
		break;
	}
}

//處理錯誤
void Parser::handleError(qint64 err)
{
	if (err == kNoChange)
		return;

	SignalDispatcher& signalDispatcher = SignalDispatcher::getInstance();
	if (err == kError)
		emit signalDispatcher.addErrorMarker(lineNumber_, err);

	QString msg;
	switch (err)
	{
	case kError:
		msg = QObject::tr("unknown error");
		break;
	case kArgError:
		msg = QObject::tr("argument error");
		break;
	case kLabelError:
		msg = QObject::tr("label incorrect or not exist");
		break;
	case kUnknownCommand:
	{
		QString cmd = currentLineTokens_.value(0).data.toString();
		msg = QObject::tr("unknown command: %1").arg(cmd);
		break;
	}
	case kNoChange:
		return;
	default:
		break;
	}

	emit signalDispatcher.appendScriptLog(QObject::tr("error occured at line %1. detail:%2").arg(lineNumber_ + 1).arg(msg), 6);
}

//解析跳轉棧或調用棧相關訊息並發送信號
void Parser::generateStackInfo(qint64 type)
{
	if (mode_ != kSync)
		return;

	const QStack<qint64> stack = (type == 0) ? callStack_ : jmpStack_;
	QList<qint64> list = stack.toList();
	QHash<int, QString> hash;
	if (!list.isEmpty())
	{
		for (const qint64 it : list)
		{
			if (!tokens_.contains(it - 1))
				continue;

			TokenMap tokens = tokens_.value(it - 1);
			if (tokens.isEmpty())
				continue;

			QStringList l;
			QString cmd = tokens.value(0).raw.trimmed() + " ";

			int size = tokens.size();
			for (qint64 i = 1; i < size; ++i)
				l.append(tokens.value(i).raw.trimmed());

			hash.insert(it, cmd + l.join(", "));
		}
	}

	SignalDispatcher& signalDispatcher = SignalDispatcher::getInstance();
	if (type == 0)
		emit signalDispatcher.callStackInfoChanged(hash);
	else
		emit signalDispatcher.jumpStackInfoChanged(hash);
}

//更新系統預定變量的值
void Parser::updateGlobalVariables()
{
	Injector& injector = Injector::getInstance();

	SPD_LOG(g_logger_name, "start update system global variables");

	QVariantHash hash;

	qint64 tick = QDateTime::currentMSecsSinceEpoch();
	hash.insert("tick", tick);
	hash.insert("stick", tick / 1000);

	SPD_LOG(g_logger_name, "insert tick and stick");

	QDateTime dt = QDateTime::currentDateTime();
	QString date = dt.toString("yyyy-MM-dd");
	QString time = dt.toString("hh:mm:ss:zzz");
	hash.insert("date", date);
	hash.insert("time", time);

	SPD_LOG(g_logger_name, "insert date and time");

	if (!injector.server.isNull() && injector.globalMutex.tryLock())
	{
		bool isBattle = injector.server->getBattleFlag();
		hash.insert("bt", isBattle ? 1ll : 0ll);

		SPD_LOG(g_logger_name, "insert bt");

		dialog_t dialog = injector.server->currentDialog;
		hash.insert("dlgid", dialog.seqno);
		SPD_LOG(g_logger_name, "insert dlgid");

		QPoint pos = injector.server->getPoint();
		hash.insert("px", pos.x());
		hash.insert("py", pos.y());

		SPD_LOG(g_logger_name, "insert px and py");

		PC pc = injector.server->getPC();
		hash.insert("chname", pc.name);
		hash.insert("chfname", pc.freeName);
		hash.insert("chlv", pc.level);
		hash.insert("chhp", pc.hp);
		hash.insert("chmp", pc.mp);
		hash.insert("chdp", pc.dp);
		hash.insert("stone", pc.gold);

		SPD_LOG(g_logger_name, "insert chname, chfname, chlv, chhp, chmp, chdp, stone");

		hash.insert("floor", injector.server->nowFloor);
		hash.insert("frname", injector.server->nowFloorName);

		SPD_LOG(g_logger_name, "insert floor and frname");

		qint64 goldearn = injector.server->recorder[0].goldearn;
		hash.insert("earnstone", goldearn);

		SPD_LOG(g_logger_name, "insert earnstone");

		injector.globalMutex.unlock();
	}

	insertGlobalVar(hash);

	SPD_LOG(g_logger_name, "update system global variables done");
}

//處理所有的token
void Parser::processTokens()
{
	SignalDispatcher& signalDispatcher = SignalDispatcher::getInstance();
	Injector& injector = Injector::getInstance();

	//同步模式時清空所有marker並重置UI顯示的堆棧訊息
	if (mode_ == kSync)
	{
		emit signalDispatcher.addErrorMarker(-1, false);
		emit signalDispatcher.addForwardMarker(-1, false);
		emit signalDispatcher.addStepMarker(-1, false);
		generateStackInfo(0);
		generateStackInfo(1);
	}

	//這裡是防止人為設置過長的延時導致腳本無法停止
	auto delay = [this, &injector]()
	{
		qint64 extraDelay = injector.getValueHash(util::kScriptSpeedValue);
		if (extraDelay > 1000)
		{
			//將超過1秒的延時分段
			qint64 i = 0;
			qint64 size = extraDelay / 1000;
			for (i = 0; i < size; ++i)
			{
				if (isInterruptionRequested())
					return;
				QThread::msleep(1000);
			}
			if (extraDelay % 1000 > 0)
				QThread::msleep(extraDelay % 1000);
		}
		else if (extraDelay > 0)
		{
			QThread::msleep(extraDelay);
		}
		QThread::msleep(1);
	};

	QHash<qint64, FunctionChunk> chunkHash;
	QMap<qint64, TokenMap> map;
	for (auto it = tokens_.cbegin(); it != tokens_.cend(); ++it)
		map.insert(it.key(), it.value());

	//這裡是為了避免沒有透過call調用函數的情況
	qint64 indentLevel = 0;
	for (auto it = map.cbegin(); it != map.cend(); ++it)
	{
		qint64 row = it.key();
		TokenMap tokens = it.value();
		QString cmd = tokens.value(0).data.toString();
		FunctionChunk chunk = chunkHash.value(indentLevel, FunctionChunk{});
		if (!chunk.name.isEmpty() && chunk.begin >= 0 && chunk.end > 0)
		{
			chunkHash.remove(indentLevel);
			functionChunks_.insert(chunk.name, chunk);
		}

		//紀錄function結束行
		if (cmd == "end")
		{
			--indentLevel;
			chunk = chunkHash.value(indentLevel, FunctionChunk{});
			chunk.end = row;
			if (!chunk.name.isEmpty() && chunk.begin >= 0 && chunk.end > 0)
			{
				chunkHash.remove(indentLevel);
				functionChunks_.insert(chunk.name, chunk);
				continue;
			}

			chunkHash.insert(indentLevel, chunk);
		}
		//紀錄function起始行
		if (cmd == "function")
		{
			QString name = tokens.value(1).data.toString();
			chunk.name = name;
			chunk.begin = row;
			chunkHash.insert(indentLevel, chunk);
			++indentLevel;
		}
		}

#ifdef _DEBUG
	for (auto it = functionChunks_.cbegin(); it != functionChunks_.cend(); ++it)
		qDebug() << it.key() << it.value().name << it.value().begin << it.value().end;
#endif

	qint64 oldlineNumber = lineNumber_;
	bool skip = false;
	RESERVE currentType = TK_UNK;
	QString name;

	for (;;)
	{
		if (empty())
		{
			SPD_LOG(g_logger_name, "met script EOF");

			//檢查是否存在腳本析構函數
			QString name;
			if (!getDtorCallBackLabelName(&name))
				break;
			else
			{
				callStack_.push(static_cast<qint64>(tokens_.size() - 1));
				jump(name, true);
			}
		}

		if (!ctorCallBackFlag_)
		{
			//檢查是否存在腳本建構函數
			util::SafeHash<QString, qint64> hash = getLabels();
			constexpr const char* CTOR = "ctor";
			if (hash.contains(CTOR))
			{
				ctorCallBackFlag_ = true;
				callStack_.push(0);
				jump(CTOR, true);
			}
		}

		currentLineTokens_ = tokens_.value(lineNumber_);

		if (!g_logger_name.isEmpty())
		{
			QStringList rawList;
			for (auto it = currentLineTokens_.cbegin(); it != currentLineTokens_.cend(); ++it)
				rawList.append(it.value().raw);

			QString logStr = QString("Line %1 with Content: %2").arg(lineNumber_ + 1).arg(rawList.join(", "));

			SPD_LOG(g_logger_name, logStr);
		}


		currentType = getCurrentFirstTokenType();
		skip = currentType == RESERVE::TK_WHITESPACE || currentType == RESERVE::TK_SPACE || currentType == RESERVE::TK_COMMENT || currentType == RESERVE::TK_UNK;

		oldlineNumber = lineNumber_;

		if (currentType == TK_LABEL)
		{
			SPD_LOG(g_logger_name, "checking call stack");
			if (checkCallStack())
			{
				SPD_LOG(g_logger_name, "call stack is empty, skip function chunk");
				continue;
			}
			SPD_LOG(g_logger_name, "call stack is not empty, procssing function now");
		}

		if (!skip)
		{
			delay();
			if (callBack_ != nullptr)
			{
				SPD_LOG(g_logger_name, "callBack started");
				qint64 status = callBack_(lineNumber_, currentLineTokens_);
				if (status == kStop)
				{
					SPD_LOG(g_logger_name, "callBack request script stop");
					break;
				}
				SPD_LOG(g_logger_name, "callBack ended");
			}
		}

		//qDebug() << "line:" << lineNumber_ << "tokens:" << currentLineTokens_.value(0).raw;

		switch (currentType)
		{
		case TK_COMMENT:
		case TK_WHITESPACE:
		case TK_SPACE:
			break;
		case TK_END:
		{
			SPD_LOG(g_logger_name, "END command");
			lastCriticalError_ = kNoError;
			processClean();
			name.clear();
			if (!getDtorCallBackLabelName(&name))
				return;
			else
			{
				callStack_.push(static_cast<qint64>(tokens_.size() - 1));
				jump(name, true);
				continue;
			}
			break;
		}
		case TK_CMP:
		{
			SPD_LOG(g_logger_name, "IF command started");
			if (processIfCompare())
			{
				SPD_LOG(g_logger_name, "IF command has jumped");
				continue;
			}
			SPD_LOG(g_logger_name, "IF command has not jumped");
			break;
		}
		case TK_CMD:
		{
			SPD_LOG(g_logger_name, "Command: " + currentLineTokens_.value(0).raw + " started");
			qint64 ret = processCommand();
			switch (ret)
			{
			case kHasJump:
			{
				SPD_LOG(g_logger_name, "Command has jumped");
				generateStackInfo(1);
				continue;
			}
			case kError:
			case kArgError:
			case kUnknownCommand:
			{
				handleError(ret);
				name.clear();
				if (getErrorCallBackLabelName(&name))
				{
					jump(name, true);
					continue;
				}
				break;
			}
			default:
				break;
			}
			SPD_LOG(g_logger_name, "Command has finished and no jump");
			break;
		}
		case TK_LOCAL:
		{
			SPD_LOG(g_logger_name, "Processing local variable");
			processLocalVariable();
			SPD_LOG(g_logger_name, "Local variable has finished");
			break;
		}
		case TK_VARDECL:
		case TK_VARFREE:
		case TK_VARCLR:
		{
			SPD_LOG(g_logger_name, "Processing global variable");
			processVariable(currentType);
			SPD_LOG(g_logger_name, "Global variable has finished");
			break;
		}
		case TK_MULTIVAR:
		{
			SPD_LOG(g_logger_name, "Processing multi global variable");
			processMultiVariable();
			SPD_LOG(g_logger_name, "Multi global variable has finished");
			break;
		}
		case TK_INCDEC:
		{
			SPD_LOG(g_logger_name, "Processing inc/dec variable");
			processVariableIncDec();
			SPD_LOG(g_logger_name, "Inc/dec variable has finished");
			break;
		}
		case TK_CAOS:
		{
			SPD_LOG(g_logger_name, "Processing CAOS variable");
			processVariableCAOs();
			SPD_LOG(g_logger_name, "CAOS variable has finished");
			break;
		}
		case TK_EXPR:
		{
			SPD_LOG(g_logger_name, "Processing expression");
			processVariableExpr();
			SPD_LOG(g_logger_name, "Expression has finished");
			break;
		}
		case TK_FORMAT:
		{
			SPD_LOG(g_logger_name, "Processing formation");
			processFormation();
			SPD_LOG(g_logger_name, "Formation has finished");
			break;
		}
		case TK_RND:
		{
			SPD_LOG(g_logger_name, "Processing random");
			processRandom();
			SPD_LOG(g_logger_name, "Random has finished");
			break;
		}
		case TK_CALL:
		{
			SPD_LOG(g_logger_name, "Processing call");
			if (processCall())
			{
				SPD_LOG(g_logger_name, "Call has jumped");
				generateStackInfo(0);
				continue;
			}
			SPD_LOG(g_logger_name, "Call has not jumped");
			break;
		}
		case TK_GOTO:
		{
			SPD_LOG(g_logger_name, "Processing goto");
			if (processGoto())
			{
				SPD_LOG(g_logger_name, "Goto has jumped");
				generateStackInfo(1);
				continue;
			}
			SPD_LOG(g_logger_name, "Goto has not jumped");
			break;
		}
		case TK_JMP:
		{
			SPD_LOG(g_logger_name, "Processing jump");
			if (processJump())
			{
				SPD_LOG(g_logger_name, "Jump has jumped");
				generateStackInfo(1);
				continue;
			}
			SPD_LOG(g_logger_name, "Jump has not jumped");
			break;
		}
		case TK_RETURN:
		{
			SPD_LOG(g_logger_name, "Processing return");
			processReturn();
			generateStackInfo(0);
			SPD_LOG(g_logger_name, "Return has finished");
			continue;
		}
		case TK_BAK:
		{
			SPD_LOG(g_logger_name, "Processing back");
			processBack();
			generateStackInfo(1);
			SPD_LOG(g_logger_name, "Back has finished");
			continue;
		}
		case TK_LABEL:
		{
			SPD_LOG(g_logger_name, "Processing label/function");
			processLabel();
			SPD_LOG(g_logger_name, "Label/function has finished");
			break;
		}
		default:
			qDebug() << "Unexpected token type:" << currentType;
			break;
		}

		if (isInterruptionRequested())
		{
			name.clear();
			if (!getDtorCallBackLabelName(&name))
				break;
			else
			{
				callStack_.push(static_cast<qint64>(tokens_.size() - 1));
				jump(name, true);
				continue;
			}
		}

		//導出變量訊息
		if (mode_ == kSync)
		{
			if (!skip)
			{
				SPD_LOG(g_logger_name, "Emitting var info");
				QVariantHash varhash;
				QVariantHash* pglobalHash = getGlobalVarPointer();
				QVariantHash localHash;
				if (!localVarStack_.isEmpty())
					localHash = localVarStack_.top();

				QString key;
				for (auto it = pglobalHash->cbegin(); it != pglobalHash->cend(); ++it)
				{
					key = QString("global|%1").arg(it.key());
					varhash.insert(key, it.value());
				}

				for (auto it = localHash.cbegin(); it != localHash.cend(); ++it)
				{
					key = QString("local|%1").arg(it.key());
					varhash.insert(key, it.value());
				}

				emit signalDispatcher.varInfoImported(varhash);
				SPD_LOG(g_logger_name, "Var info emitted");
			}
		}

		//移動至下一行
		SPD_LOG(g_logger_name, "Moving to next line");
		next();
	}

	SPD_LOG(g_logger_name, "Processing finished");
	processClean();

	}
