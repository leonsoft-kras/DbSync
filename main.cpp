/*
 * Db Sync
 * Copyright (C) 2020-2022 by A.Petrov (Leonsoft)
 *
 * This application is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This application is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 * Please contact gordos.kund@gmail.com with any questions on this license.
 */

#ifdef CRASH_ON
#include "crashrpt.h"
#include <windows.h>
#pragma comment( lib, "crashrpt.lib" )

	// Зададим Callback-функцию, которая будет вызвана при сбое
BOOL WINAPI CrashCallback(LPVOID /*lpvState*/)
{  	
	crAddScreenshot(CR_AS_VIRTUAL_SCREEN);
	return TRUE;
}
#endif


#include <QtCore/QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QCommandLineParser>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QVariant>
#include <QSqlField>
#include <QTime>
#include <QDir>
#include <QThread>
#include <QCryptographicHash>
#include <QSettings>
#include <QTextCodec>
#include <locale.h>
#include <QTextStream>

#include "lin.h"
#include "vers.h"


typedef struct _tabcol
{
	QStringList	col;			// columns name  
	QStringList	colkey;			// columns name (primary key)
	QList<int>	poskey;			// columns index (primary key)
	QString		tab		= "";	// table name
	QString		where	= "";	// where
	QStringList	trigg;			// triggers off

	bool		bIgnAll	= false;
	bool		bIgnIns	= false;
	bool		bIgnDel	= false;
	bool		bIgnUpd	= false;
	bool		bIgnTRG	= false;
	bool		bAAC	= false;
	bool		bDebug  = false;

	bool		bLog	= false;	
	QString		logData	= "";
	int			maxQSymb= -1;

} tabcol;


typedef QList<QVariantList> tabdata;
typedef struct _linetab
{
	QList<int>	m_typeCol;			// columns type
	QStringList	m_nameCol;			// columns name
	QStringList	m_crcline;			// CRC per row
	tabdata		m_tabdata;			// all rows of the table in columns

	bool		b_Unsuppt = false;	// unsupported columns
	int			ColUnsupp = -1;		// index of unsupp.col

} linetab;


//-------------------------------------------------------------------------------------------------
void AddLog (tabcol* ptabcol, QString txt, bool CheckMax=true)	
{
	if (ptabcol->maxQSymb > 0 && CheckMax == true)
		txt = txt.mid (0, ptabcol->maxQSymb);

	ptabcol->logData += txt;
	ptabcol->logData += "\n";
}

//-------------------------------------------------------------------------------------------------
QString SetVariantStr(int type, const QVariant& var, int pos)	// column data to form a query
{
	QString r = "";
	if (var.isNull()) {
		r = "null";
	}
	else {
		switch (type)
		{
		case QVariant::DateTime: {
			QDateTime md = var.toDateTime();
			QDate d = md.date();
			QTime t = md.time();
			r = QString("to_timestamp('%1.%2.%3 %4:%5:%6.%7', 'dd.mm.yyyy hh24:mi:ss.ff3')").	// bag fix. set ff3
				arg(d.day( ), 2, 10, QChar('0')).arg(d.month( ), 2, 10, QChar('0')).arg(d.year(  ), 4, 10, QChar('0')).
				arg(t.hour(), 2, 10, QChar('0')).arg(t.minute(), 2, 10, QChar('0')).arg(t.second(), 2, 10, QChar('0')).
				arg(t.msec(), 3, 10, QChar('0'));
			break;
		}
		case QVariant::Double:
			r = var.toString(); //-V1037
			break;

		case QVariant::Date: {
			QDate d = var.toDate();
			r = QString("to_timestamp('%1.%2.%3', 'dd.mm.yyyy')").
				arg(d.day(), 2, 10, QChar('0')).arg(d.month(), 2, 10, QChar('0')).arg(d.year(), 4, 10, QChar('0'));
			break;
		}
		case QVariant::Time: {
			QTime t = var.toTime();
			r = QString("to_timestamp('%1:%2:%3.%4', 'hh24:mi:ss.ff3')"). // bag fix
				arg(t.hour(), 2, 10, QChar('0')).arg(t.minute(), 2, 10, QChar('0')).arg(t.second(), 2, 10, QChar('0')).
				arg(t.msec(), 3, 10, QChar('0'));
			break;
		}
		case QVariant::LongLong:
		case QVariant::ULongLong:
			r = var.toString();
			break;
		case QVariant::UInt:
		case QVariant::Int:
			r = var.toString();
			break;

		case QVariant::String:
			r = "'" + var.toString().replace("'","''") + "'";	// 2022 - fix bag if appostrof
			break;
		case QVariant::ByteArray: {
			r = QString(":id%1").arg(pos);
			break;
		}
		default: // error !!!
			r = var.toString();
			printf ("Warning: Unsupported column type. See program log.\n");
			break;
		}
	}
	return r;
}

//-------------------------------------------------------------------------------------------------
inline QString GetVariantStr(int type, const QVariant& var, bool& bUnsupport)	// column data from the database
{
	QString r = "";
	if (var.isNull()) {
		r += "null";
	}
	else {
		switch (type)
		{
		case QVariant::DateTime: {	
			QDateTime md = var.toDateTime();
			QDate d = md.date();
			QTime t = md.time();
			r = QString("%1.%2.%3 %4:%5:%6.%7").
				arg(d.day( ), 2, 10, QChar('0')).arg(d.month( ), 2, 10, QChar('0')).arg(d.year(  ), 4, 10, QChar('0')).
				arg(t.hour(), 2, 10, QChar('0')).arg(t.minute(), 2, 10, QChar('0')).arg(t.second(), 2, 10, QChar('0')).
				arg(t.msec(), 3, 10, QChar('0'));
			break;  
		}
		case QVariant::Double:
 			r = var.toString(); //-V1037
 			break;

		case QVariant::Date: {
			QDate d = var.toDate();
			r = QString("%1.%2.%3").arg(d.day(), 2, 10, QChar('0')).arg(d.month(), 2, 10, QChar('0')).arg(d.year(), 4, 10, QChar('0'));
			break;
		}
		case QVariant::Time: {
			QTime t = var.toTime();
			r = QString("%1:%2:%3.%4").arg(t.hour(), 2, 10, QChar('0')).arg(t.minute(), 2, 10, QChar('0')).arg(t.second(), 2, 10, QChar('0')).
																		arg(t.msec(),   3, 10, QChar('0'));
			break;
		}
		case QVariant::LongLong:
		case QVariant::ULongLong:
			r = var.toString();
		break;
		case QVariant::UInt:
		case QVariant::Int:
 			r = var.toString();
 			break;
		case QVariant::String:
			r = var.toString();
			break;
		case QVariant::ByteArray: {
			QByteArray ba = var.toByteArray().toHex(':');
			r = QString::fromLocal8Bit(ba.data()); // -char-
			break;
		}
		default:
			bUnsupport = true;
			r = var.toString();
			// printf ("Warning: Unsupported column type (%d). See program log.\n", type);
			break;
		}
	}
	return r;
}

//-------------------------------------------------------------------------------------------------
inline QString GetKeyStr(	QList<int>  m_type, 
					QStringList	m_name,
					QVariantList vl, QList<int> keys) // forming a row of key columns
{
	bool bUnsupport = false;
	QString r = "";
	for (int n = 0; n < keys.size(); n++) {
		int indx		= keys.at(n);
		r += GetVariantStr(m_type.at(indx), vl.at(indx), bUnsupport) + "; ";
	}
	return r;
}

//-------------------------------------------------------------------------------------------------
inline QString GetRows (QVariantList* vlist, QList<int>* m_typeCol, bool& bUnsupport, int& ColUnsupp)
{
	QString xx = "";
	for (int i = 0; i < vlist->size() - 1; i++) {	// exclude rowid (last rows!)
		xx += GetVariantStr(m_typeCol->at(i), vlist->at(i), bUnsupport) + "; ";
		if (bUnsupport == true && ColUnsupp == -1)
			ColUnsupp = i+1;
	}
	return xx;
}

//-------------------------------------------------------------------------------------------------
inline QString GetCrc(QVariantList* vlist, QList<int>*	m_typeCol, bool& bUnsupport, int& ColUnsupp)	// checksum calculation for all table columns
{
	QString rowdata= GetRows (vlist, m_typeCol, bUnsupport, ColUnsupp);
	QByteArray bb  = rowdata.toUtf8();
	QByteArray sha = QCryptographicHash::hash(bb, QCryptographicHash::Sha3_512);	// fix - best hash func!
	return QString::fromLocal8Bit(sha.toHex ());	// -char-
}

//-------------------------------------------------------------------------------------------------
int TriggersOn (QSqlDatabase* pdb, QString SqlDrv, tabcol* m_tabcol, bool bOn)
{
	int err = 0;
	if (m_tabcol->trigg.size() < 1)	
		return 0;

	for (int i = 0; i < m_tabcol->trigg.size(); i++) {
		QString nametrg = m_tabcol->trigg.at(i).trimmed();
		if (nametrg.isEmpty())	
			continue;

		QString sql = "";
		if (SqlDrv == "QOCI") {
			sql += "alter trigger " + nametrg;			
			if (bOn == true)	sql += " enable ";
			else				sql += " disable ";
		}
		else 
		if (SqlDrv == "QPSQL") {
			sql += "ALTER TABLE " + m_tabcol->tab.toLower();
			if (bOn == true)	sql += " ENABLE ";
			else				sql += " DISABLE ";
			sql += "TRIGGER " + nametrg.toLower();
		} 
		else {
			return -29;
		}

		QSqlQuery query(*pdb);
		for (;;) {
			if (pdb->transaction() == false) { err = -22; break; }

			if (SqlDrv == "QPSQL") {
				// prepare(sql) - "syntax error at ..."
				if (query.exec(sql)    == false) { err = -24; break; }	// execute SQL (tab modify)
			}
			else {
				if (query.prepare(sql) == false) { err = -23; break; }
				if (query.exec()       == false) { err = -24; break; }	// execute SQL (tab modify)
			}

			if (pdb->commit()      == false) { err = -25; break; }
			err = 0;
			break;
		}

		if (err != 0) {

			QString x0 = query.executedQuery();

			QString x1 = query.lastError().text().replace("\n", "; ");
			QString x2 = pdb-> lastError().text().replace("\n", "; ");
			printf("SQL execution error: %s\n", qPrintable(x1));
			printf("                   : %s\n", qPrintable(x2));
			AddLog(m_tabcol, "Err: " + x1, false);
			AddLog(m_tabcol, "Err: " + x2, false);
			AddLog(m_tabcol, "SQL: " + sql,false); 
			AddLog(m_tabcol, "     ");
			pdb->rollback();
		}
	}
	return err;
}																				

//-------------------------------------------------------------------------------------------------
int SychroDatab(QSqlDatabase* pdb, QString SqlDrv, int mode, tabcol* m_tabcol, linetab* m_linesS, linetab* m_linesD, int posS, int posD)	// synchro row
{
	QSqlQuery query(*pdb);
	QString sql = "";
	int     err = 0;

	QVariantList varlS;
	QVariantList varlD;
	int sizelist = 0;

	switch (mode)
	{
	case 0:	// delete -----------
		sql = "delete from ";
		sql += m_tabcol->tab;
		break;
	case 1:	// insert ------------
		varlS	= m_linesS->m_tabdata.at(posS);
		sizelist= varlS.size();

		sql = "insert into ";
		sql += m_tabcol->tab + " (";
		for (int n = 0; n < sizelist - 1; n++) {
			if (n > 0)	sql += ",";
			sql += m_tabcol->col.at(n);
		}		
		sql += ") values (";
		for (int n = 0; n < sizelist - 1; n++) {
			if (n > 0)	sql += ",";
			sql += SetVariantStr(m_linesS->m_typeCol.at(n), varlS.at(n), n);
		}
		sql += ") ";
		break;
	case 2:	// update ------------
		varlS    = m_linesS->m_tabdata.at(posS);
		sizelist = varlS.size();

		sql = "update ";
		sql += m_tabcol->tab + " set ";
		for (int n = 0; n < sizelist - 1; n++) {
			if (n > 0)	sql += ",";
			sql += m_tabcol->col.at(n);
			sql += "=";
			sql += SetVariantStr(m_linesS->m_typeCol.at(n), varlS.at(n), n);
		}
		break;
	}

	if (mode != 1) { // for update or delete
		varlD	= m_linesD->m_tabdata.at(posD);
		sizelist= varlD.size();
		sql	   += " where ";
		if (SqlDrv == "QOCI") {
			sql += "rowid=chartorowid('" + varlD.at(sizelist - 1).toString() + "')";
		}
		else
		if (SqlDrv == "QPSQL") {
			sql += "ctid='" + varlD.at(sizelist - 1).toString() + "'";
		}
		else  {
			return -21;
		}
	}
	   
	for (;;) {
		if (pdb->transaction() == false)					{ err = -22; break; } 
		if (query.prepare(sql) == false)					{ err = -23; break; }

		// prepare blob 
		for (int n = 0; n < sizelist - 1; n++)	{
			if (m_linesS->m_typeCol.at(n) != QVariant::ByteArray) 
				continue;
			
			QString		 xx   = QString(":id%1").arg(n);
			QVariantList varl = m_linesS->m_tabdata.at(posS);

			if (varl.at(n).isNull()) {
				QVariant varnull;
				query.bindValue(xx, varnull);
			}
			else {	
				QVariant binvar(varl.at(n).toByteArray());
				query.bindValue(xx, binvar);
			}
		}

		if (query.exec() == false)							{ err = -24; break; }	// execute SQL (tab modify)
		if (pdb->commit() == false)							{ err = -25; break; }

		err = 0;
		break;
	}

	if (err != 0) {
		QString x1 = query.lastError().text().replace("\n", "; ");
		QString x2 = pdb-> lastError().text().replace("\n" , "; ");
		printf("SQL execution error: %s\n", qPrintable(x1));
		printf("                   : %s\n", qPrintable(x2));
		AddLog(m_tabcol, "Err: " + x1, false);
		AddLog(m_tabcol, "Err: " + x2, false);
		AddLog(m_tabcol, "SQL: " + sql,false);
		AddLog(m_tabcol, "     ");
		pdb->rollback();	
	}
	return err;
}

//-------------------------------------------------------------------------------------------------
inline bool Confirm (bool bAAC, const char* Msg)
{
	bool bres = true;
	if (bAAC == false) {
		printf ("%s", Msg);
		int ass = getch ();
		if (!(ass == 'Y' || ass == 'y'))   bres = false;
		printf ("\r                                \r");
	}
	return bres;
}

//-------------------------------------------------------------------------------------------------
inline QString LimitScreen (QString& txt, int limit=70)
{
	return (txt.size() < limit+3) ? txt : txt.mid(0, limit) + "...";
}

//-------------------------------------------------------------------------------------------------
int TableComparison(QSqlDatabase* pdb, QString SqlDrv, tabcol* m_tabcol, linetab* m_linesS, linetab* m_linesD)
{
	printf ("\n------------------------------------------------------------------\n");
	int err = 0;

	// for debug
	if (m_tabcol->bDebug == true)  AddLog(m_tabcol, "\n", false);

	AddLog(m_tabcol, "Table: " + m_tabcol->tab, false);
	QString txt = "Columns: "; for (int b=0; b < m_tabcol->col.size(); b++)	txt += m_tabcol->col.at(b) + ";"; AddLog(m_tabcol, txt, false);

	// search & ignore duplicate rows
	int DuplicateRows = 0;
	for (int i = 0; i < m_linesS->m_crcline.size(); i++) {
		QString crcS =  m_linesS->m_crcline.at(i);

		for (int j = 0; j < m_linesD->m_crcline.size(); j++) {
			QString crcD =  m_linesD->m_crcline.at(j);

			if (crcD != crcS) continue; // it isn't same row
			// --> crcD == crcS, but rowline ?

			m_linesS->m_crcline.removeAt(i);	m_linesS->m_tabdata.removeAt(i); 
			m_linesD->m_crcline.removeAt(j);	m_linesD->m_tabdata.removeAt(j); 
			i--; j--; DuplicateRows++;
			break;
		}
	}

	// fix error, check flags
	bool bChngeTrg = true;
	if ((m_tabcol->bIgnAll == true) ||	// all ignore
		(m_tabcol->bIgnIns == true && m_tabcol->bIgnDel == true && m_tabcol->bIgnUpd == true)) {
		bChngeTrg = false;
	}

	if (m_tabcol->trigg.size() >  0 &&		// trigger exists
		m_tabcol->bIgnTRG      != true  &&	// trigger isn't ignore
		bChngeTrg			   == true)		// 2022 only if recovery db
	{
		if (TriggersOn (pdb, SqlDrv, m_tabcol, false) != 0)
		{
			QString txt1  = "Trigger isn't disabled. Data will not be changed.";
			printf("%s\n", qPrintable(txt1));
			AddLog(m_tabcol, txt1);
			m_tabcol->bIgnAll = true;
		}
	}

	AddLog(m_tabcol, QString("-----------------------------------------"));
	AddLog(m_tabcol, QString("- Different Rows (source DB):"));
	bool bUnsupp = false;
	int  indUnsC = -1;


	// search & update different rows
	int DifferentRows = 0;
	for (int i = 0; i < m_linesS->m_crcline.size() && m_linesD->m_crcline.size() > 0; i++)	{
		QString strKeyS= GetKeyStr(	m_linesS->m_typeCol, m_linesS->m_nameCol, m_linesS->m_tabdata.at(i), m_tabcol->poskey);  // source

		for (int j = 0; j < m_linesD->m_crcline.size(); j++)	{
			QString strKeyD = GetKeyStr(m_linesD->m_typeCol, m_linesD->m_nameCol, m_linesD->m_tabdata.at(j), m_tabcol->poskey);  // destination

			if (strKeyS != strKeyD)	// !same row, but... (2022 this is Unnecessary or Missing - key1 != key2)
				continue;
		
			// this is diff row (key1==key2)
			QVariantList vlist = m_linesS->m_tabdata.at(i);
			QString ScreenStr  = GetRows (&vlist, &m_linesS->m_typeCol, bUnsupp, indUnsC).replace("\n"," | ").replace("\r"," ");
			AddLog(m_tabcol, ScreenStr);

			// for debug
			if (m_tabcol->bDebug == true) {
				bool bx0 = false; int bx1 = 0;
				QVariantList vlisS = m_linesS->m_tabdata.at(i);
				QString   DebugSrc = "  *S:  " + GetRows (&vlisS, &m_linesS->m_typeCol, bx0, bx1);

				QVariantList vlisD = m_linesD->m_tabdata.at(j);
				QString   DebugDst = "  *D:  " + GetRows (&vlisD, &m_linesD->m_typeCol, bx0, bx1);

				AddLog(m_tabcol, DebugSrc, false);
				AddLog(m_tabcol, DebugDst, false);
			}

			if (bUnsupp == true) {
				QString terr = QString ("Column type unsupported (%1)\n").arg(indUnsC);
				printf ("%s", qPrintable(terr));
				AddLog(m_tabcol, terr);
			}

			int nChangeRow = 1;
			if (m_tabcol->bIgnAll == false && 
				m_tabcol->bIgnUpd == false) 
			{
				printf ("Different  : %s\n", qPrintable(LimitScreen(ScreenStr)));
				bool bY = Confirm (m_tabcol->bAAC, "Replace data (y/n) ?");
				if (bY == true) {
					int err0 = SychroDatab(pdb, SqlDrv, 2, m_tabcol, m_linesS, m_linesD, i, j); // update
					if (err0 == 0) { DuplicateRows++;  nChangeRow = 0; } else err  += err0;
				}
			}
			else {
				printf ("Different  : %s\n", qPrintable(LimitScreen(strKeyS)));
			}

			m_linesS->m_crcline.removeAt(i);	m_linesS->m_tabdata.removeAt(i);
			m_linesD->m_crcline.removeAt(j);	m_linesD->m_tabdata.removeAt(j);
			i--; j--; DifferentRows += nChangeRow;
			break;
		}
	}

	AddLog(m_tabcol, QString("-----------------------------------------"));
	AddLog(m_tabcol, QString("- Unnecessary Rows (destination DB):"));
	bUnsupp = false;
	indUnsC = -1;

	// delete unwanted rows
	int UnnecessRows = 0;
	for (int j = 0; j < m_linesD->m_crcline.size(); j++)	{

		QVariantList vlist = m_linesD->m_tabdata.at(j);
		QString ScreenStr = GetRows (&vlist, &m_linesD->m_typeCol, bUnsupp, indUnsC).replace("\n"," | ").replace("\r"," ");
		AddLog(m_tabcol, ScreenStr);
		if (bUnsupp == true) {
			QString terr = QString ("Column type unsupported (%1)\n").arg(indUnsC);
			printf ("%s", qPrintable(terr));
			AddLog(m_tabcol, terr);
		}

		int nChangeRow = 1;
		if (m_tabcol->bIgnAll == false && 
			m_tabcol->bIgnDel == false) {
			printf ("Unnecessary: %s\n", qPrintable(LimitScreen(ScreenStr)));
			bool bY = Confirm (m_tabcol->bAAC, "Delete data (y/n) ?");
			if (bY == true) {
				int err0 = SychroDatab(pdb, SqlDrv, 0, m_tabcol, m_linesS, m_linesD, 0, j);	// delete;				
				if (err0 == 0) { DuplicateRows++;  nChangeRow = 0; } else err  += err0;
			}
		}
		else {
			QString strKeyD = GetKeyStr(m_linesD->m_typeCol, m_linesD->m_nameCol, m_linesD->m_tabdata.at(j), m_tabcol->poskey);  // destination
			printf ("Unnecessary: %s\n", qPrintable(LimitScreen(strKeyD)));
		}

		UnnecessRows += nChangeRow;	// 2022 - 
	}

	AddLog(m_tabcol, QString("-----------------------------------------"));
	AddLog(m_tabcol, QString("- Missing Rows (source DB):"));
	bUnsupp = false;
	indUnsC = -1;

	// add missing rows
	int MissingRows = 0;
	for (int i = 0; i < m_linesS->m_crcline.size(); i++)	{

		QVariantList vlist = m_linesS->m_tabdata.at(i);
		QString ScreenStr  = GetRows (&vlist, &m_linesS->m_typeCol, bUnsupp, indUnsC).replace("\n"," | ").replace("\r"," ");
		AddLog(m_tabcol, ScreenStr);
		if (bUnsupp == true) {
			QString terr = QString ("Column type unsupported (%1)\n").arg(indUnsC);
			printf ("%s", qPrintable(terr));
			AddLog(m_tabcol, terr);
		}

		int nChangeRow = 1;
		if (m_tabcol->bIgnAll == false && 
			m_tabcol->bIgnIns == false) {
			printf ("Missing    : %s\n", qPrintable(LimitScreen(ScreenStr)));
			bool bY = Confirm (m_tabcol->bAAC, "Insert data (y/n) ?");
			if (bY == true) {
				int err0 = SychroDatab(pdb, SqlDrv, 1, m_tabcol, m_linesS, m_linesD, i, 0);	// insert
				if (err0 == 0) { DuplicateRows++;  nChangeRow = 0; } else err  += err0;
			}
		}
		else {
			QString strKeyS	= GetKeyStr(m_linesS->m_typeCol, m_linesS->m_nameCol, m_linesS->m_tabdata.at(i), m_tabcol->poskey);  // source
			printf ("Missing    : %s\n", qPrintable(LimitScreen(strKeyS)));
		}

		MissingRows += nChangeRow;
	}

	if (bChngeTrg == true && // 2022 only if recovery db
		m_tabcol->bIgnTRG == false &&
		m_tabcol->trigg.size() > 0) 
	{
		if (TriggersOn (pdb, SqlDrv, m_tabcol, true) != 0)
		{
			QString txt1  = "Trigger isn't enabled. Check database!";
			printf ("%s\n", qPrintable(txt1));
			AddLog(m_tabcol, txt1);
			m_tabcol->bIgnAll = true;
		}
	}

	AddLog(m_tabcol, QString("-----------------------------------------"));
	printf ("\n------------------------------------------------------------------\n");	// 2022 - always print

	if (bChngeTrg == true) {
		AddLog(m_tabcol, QString("Total differences after synchronization:"));
		printf ("Total differences after synchronization:\n"); 
	}
	else {
		AddLog(m_tabcol, QString("Total:"));
		printf ("Total:\n"); 
	}

	AddLog(m_tabcol, QString("   Identical   rows (Src==Dst): %1").arg(DuplicateRows));	qInfo () << "- Identical   rows (Src==Dst): " << DuplicateRows;
	AddLog(m_tabcol, QString("   Different   rows (Src<>Dst): %1").arg(DifferentRows));	qInfo () << "- Different   rows (Src<>Dst): " << DifferentRows;
	AddLog(m_tabcol, QString("   Unnecessary rows (Only Dst): %1").arg(UnnecessRows ));	qInfo () << "- Unnecessary rows (Only Dst): " << UnnecessRows;
	AddLog(m_tabcol, QString("   Missing     rows (Only Src): %1").arg(MissingRows  ));	qInfo () << "- Missing     rows (Only Src): " << MissingRows ;

	if (err != 0) {
		QString tt = "Warning: there were errors when changing the data in the table.";
		printf ("\n%s\n",qPrintable(tt));	// 2022 fix )
		AddLog(m_tabcol, tt);
	}
	return err;
}

//-------------------------------------------------------------------------------------------------
int SaveErr (QSqlQuery* p, QString sql, tabcol*	m_tabcol, int err)
{
	QString x1 = p->lastError().text().replace("\n", "; ");
	printf ("SQL execution error: %s\n", qPrintable(x1));
	AddLog(m_tabcol, "Err: " + x1 ,false);
	AddLog(m_tabcol, "SQL: " + sql,false);
	AddLog(m_tabcol, " ");
	return err;
}

//-------------------------------------------------------------------------------
//
class CMyDbThread : public QThread
{
public:
	CMyDbThread (QObject *parent = NULL) : QThread(parent) { //-V730
		pDb = nullptr;
	}
	void run() override
	{
#ifdef CRASH_ON
		crInstallToCurrentThread2(0);
#endif
		QSqlQuery query(*pDb);
		bool b = query.exec(tsql);
		if (b == false) {
			// return SaveErr (&query, cnttest, m_tabcol, -31);
			CodeErr = 1;
			DbErr	= query.lastError().text().replace("\n", "; ");
#ifdef CRASH_ON
			crUninstallFromCurrentThread();
#endif
			return;
		}

		QSqlRecord rec = query.record();
		int  cntField  = rec.count();
		for (int n = 0; n < cntField; n++)	{
			QSqlField  oField	= rec.field(n);
			QString    nname	= oField.name();
			QVariant   vartype	= oField.type();
			int			precis	= oField.precision();
			int			typeCol =(int)vartype.type();
			if (typeCol == (int)QVariant::Double && precis <= 0)
					typeCol = QVariant::Int;

			m_lines.m_nameCol.append(nname);
			m_lines.m_typeCol.append(typeCol);
		}

		m_lines.m_tabdata.reserve(maxRow);
		m_lines.m_crcline.reserve(maxRow);
		
		bool bUnsupport = false;
		int  indxUnsupp = -1;

		// read records
		while (query.next())	{
			QVariantList mvlist;
			for (int n = 0; n < cntField; n++)
				mvlist.append(query.value(n));
		
			QString rd = "";
			m_lines.m_tabdata.append( mvlist );
			m_lines.m_crcline.append( GetCrc(&mvlist, &m_lines.m_typeCol, bUnsupport, indxUnsupp) );
		} 

		m_lines.b_Unsuppt	= bUnsupport;
		m_lines.ColUnsupp	= indxUnsupp;
		CodeErr				= 0;
#ifdef CRASH_ON
		crUninstallFromCurrentThread();
#endif
	}

	QString		 tsql = "";
	QSqlDatabase* pDb = nullptr;
	int		Instance  = -1;
	QString DbErr	  = "";
	int		CodeErr	  = -101;
	int		maxRow	  = 1;
	linetab	m_lines;
};


//-------------------------------------------------------------------------------------------------
// reading data in multiple threads.
int GetDataTable(QList<QSqlDatabase*> m_db, QString SqlDrv, tabcol*	m_tabcol, linetab* m_lines)
{
	int    maxConnect = m_db.size();
	QSqlDatabase* pdb = m_db.at(0);				// for simple queries
	QString nameRID	  = "DbSyncExtentKeyId";	// Aliases for special database fields. 
	QString nameRNM	  = "DbSyncExtentRowNum";	// Must be unique, do not match table fields

	// get rows count
	QString cnttest = "select count(*) from " + m_tabcol->tab;
	m_tabcol->where = m_tabcol->where.trimmed();
	if (m_tabcol->where.isEmpty() == false) {
		cnttest += " where ";
		cnttest += m_tabcol->where;
	}	

	// for debug
	if (m_tabcol->bDebug == true) AddLog(m_tabcol, "  " + cnttest, false);

	QSqlQuery query0(*pdb);						// execute sql
	bool b = query0.exec(cnttest);
	if (b == false) 
		return SaveErr (&query0, cnttest, m_tabcol, -33);
		
	int cnt_rows = 0;
	if (query0.next())							// read records
		cnt_rows = query0.value(0).toInt();

	printf ("Rows count: %d\n", cnt_rows);
	printf ("Read  data: start\n");

	//-----------------------------
	QString sql0;
	int n_beg = 0, n_end = 0, n_step = 1;
	if (maxConnect == 1) {
		// old postgresql
		sql0= "select ";
		for (int i = 0; i < m_tabcol->col.size(); i++) {
			sql0 +=        m_tabcol->col.at(i).trimmed() + ",";
		}

		if (SqlDrv == "QOCI") {
			sql0 += "t.rowid " + nameRID ;	// to access the row to update
		} else
		if (SqlDrv == "QPSQL") {
			sql0 += "ctid as " + nameRID;
		} else {
			printf ("SQL-driver determination error: %s\n", qPrintable (SqlDrv));
			return -30;	// w/o rowid !!!
		}
		sql0 += " from " + m_tabcol->tab;

		if (m_tabcol->where.isEmpty() == false) {
			sql0 += " where ";
			sql0 += m_tabcol->where;
		}

		n_step= cnt_rows;
	}
	else {
		// 
		// multi processing - prepare requests for several threads
		QString sql1= "";
		QString sql2= "";
		QString sqlw= "";
		for (int i = 0; i < m_tabcol->col.size(); i++) {
			sql1 +=        m_tabcol->col.at(i).trimmed() + ",";
			sql2 += "t." + m_tabcol->col.at(i).trimmed() + ",";
		}
		sql1 += nameRID;

		if (SqlDrv == "QOCI") {
			sql2 += "t.rowid " + nameRID + ", ";	// to access the row to update
			sql2 += "rownum " + nameRNM + " ";		// to fetch a specific piece of data (part of select)
			sqlw  = "t.rowid";						// data acquisition identity
		} else
		if (SqlDrv == "QPSQL") {
			sql2 += "ctid as " + nameRID + ", ";
			sql2 += "row_number() over() as " + nameRNM + " ";
			sqlw  = "ctid";
		} else {
			printf ("SQL-driver determination error: %s\n", qPrintable (SqlDrv));
			return -30;	// w/o rowid !!!
		}

		sql0  = "select ";
		sql0 += sql1;
		sql0 += " from (select ";
		sql0 += sql2;
		sql0 += " from " + m_tabcol->tab;
		sql0 += " t ";
		if (m_tabcol->where.isEmpty() == false) {
			sql0 += " where ";
			sql0 += m_tabcol->where;
		}
		sql0 += " order by ";
		sql0 += sqlw;
		sql0 += ") ";

		if (SqlDrv == "QPSQL") {	// fix Postgresql - alias for:  FROM (SELECT ...) [AS] foo
			sql0 += "as DbSyncCursorTable ";
		}

		n_beg = 0; n_end = 0; n_step = 10 + cnt_rows / maxConnect;
		n_step = (n_step / 10) * 10;

		if (cnt_rows / maxConnect <= 10) {				// little data = 1 thread
			maxConnect = 1;
			n_step     = 10 + cnt_rows;
		}
	}

	CMyDbThread pThr[22];							// max 10 thread (protected)
	for (int n = 0; n < maxConnect; n++) {			// threads get data in parts from the all rows: 1..10, 11..20, 21..30 and etc.

		QString ssql= sql0;
		if (maxConnect != 1) {
			n_end		= n_beg + n_step;			// so the database client loads the entire network channel			
			if (n < maxConnect - 1)	ssql += QString ("where %1>=%2 and %3<%4").arg(nameRNM).arg (n_beg).arg(nameRNM).arg (n_end);
			else					ssql += QString ("where %1>=%2").arg(nameRNM).arg (n_beg);
		}
		else {
		}

		n_beg			= n_end;
		pThr[n].tsql	= ssql;
		pThr[n].pDb		= m_db.at(n);
		pThr[n].Instance= n;
		pThr[n].maxRow	= 1.5 * n_step;
	}

	char txtTime[99];
	QDateTime timeSql = QDateTime::currentDateTime();

	for (int nn=0; nn < maxConnect; nn++) 		pThr[nn].start();	
	for (int nn=0; nn < maxConnect; nn++)		pThr[nn].wait();	// waiting for all threads

	int ThrWorks = timeSql.msecsTo(QDateTime::currentDateTime());
	if (ThrWorks < 1000)	sprintf(txtTime, "%d msec", ThrWorks);
	else					sprintf(txtTime, "%.2f sec", ThrWorks / 1000.);

	int unsupp	= -1;
	int CodErr  = 0;
	QString Err = "", ErrSql = "";
	for (int nn = 0; nn < maxConnect; nn++) {	
		// if one of the threads did not fulfill the database request - stop!
		if (pThr[nn].CodeErr != 0) {
			CodErr  = pThr[nn].CodeErr;
			Err		= pThr[nn].DbErr;
			ErrSql	= pThr[nn].tsql;
			break;
		}

		if (pThr[0].m_lines.b_Unsuppt == true && nn == 0) 
			unsupp = pThr[0].m_lines.ColUnsupp;
			
		// save data from all threads to the output-list
		m_lines->m_crcline.append(pThr[nn].m_lines.m_crcline);
		m_lines->m_tabdata.append(pThr[nn].m_lines.m_tabdata);
		if (m_lines->m_typeCol.size() == 0) {
			m_lines->m_typeCol.append(pThr[nn].m_lines.m_typeCol);
			m_lines->m_nameCol.append(pThr[nn].m_lines.m_nameCol);
		}
		// free
		pThr[nn].m_lines.m_crcline.clear();
		pThr[nn].m_lines.m_tabdata.clear();
		pThr[nn].m_lines.m_typeCol.clear();
		pThr[nn].m_lines.m_nameCol.clear();
	}

	if (CodErr != 0) {
		printf ("SQL execution error: %s\n", qPrintable(Err));
		AddLog(m_tabcol, "Err: " + Err, false);
		AddLog(m_tabcol, "SQL: " + ErrSql, false);
		AddLog(m_tabcol, " ");
		return -1;
	}

 	printf ("Read  data: finish (%s)\n\n", txtTime);
	
	if (m_lines->m_tabdata.size() != cnt_rows)
		printf ("  Warning ! Received rows %d of %d\n", m_lines->m_tabdata.size(), cnt_rows);
	if (unsupp > 0) {
		QString errCol = QString ("  Warning ! Column type unsupported (%1)\n").arg(unsupp);
		printf ("%s", qPrintable(errCol));
		AddLog(m_tabcol, errCol, false);
	}
	printf ("\n");
	return 0;
}

//-------------------------------------------------------------------------------------------------
QList <QSqlDatabase*> GetDb(QString db, QString drv, QStringList& list, const char* name, int m)
{
	QList <QSqlDatabase*> listDb;

	QSqlDatabase  xx = QSqlDatabase::addDatabase(drv, QString("DbSync.%1").arg(QTime::currentTime().msecsSinceStartOfDay()));
	if (list.size() < 3) { 
		printf ("Error in %s Db connection settings\n", name);	
		return listDb;	
	}

	// user / password @ db : addr * port
	xx.setUserName		(list.at(0));
	xx.setPassword		(list.at(1));
	xx.setDatabaseName	(list.at(2));			// database or alias
	if (list.size() > 3)
		xx.setHostName	(list.at(3));			// host
	if (list.size() > 4)
		xx.setPort      (list.at(4).toInt());	// port

	if (drv == "QOCI")	
		xx.setConnectOptions("OCI_ATTR_PREFETCH_ROWS=4000;OCI_ATTR_PREFETCH_MEMORY=524288;");
	else
	if (drv == "QPSQL") {
		// nothing
	}
	else {
		printf ("Unsupported SQL-driver type. Program revision is required\n");
		return listDb;
	}

	printf ("Connect Db: %s\n", name);
	if (!xx.open()) {
		printf ("Error during connecting to database: %s\n", qPrintable( xx.lastError().text().replace("\n","; ") ));	
		return listDb;	
	}

	QSqlDatabase* p = new QSqlDatabase(xx);
	listDb.append(p);

	// multiple connections
	for (int n = 1; n < m; n++) {
		QSqlDatabase test = QSqlDatabase::cloneDatabase(xx, QString("DbSync.%1.%2.%d").arg(n).arg(QTime::currentTime().msecsSinceStartOfDay()));
		QSqlDatabase* p2  = new QSqlDatabase(test);
		if (p2->open() == true)
			listDb.append(p2);
	}
	printf ("Connect Db: Ok\n");
	return listDb;
}


//-------------------------------------------------------------------------------------------------
int main(int argc, char *argv[])
{
	QCoreApplication a(argc, argv);

#ifdef _WIN32
	// system("@chcp 65001");
	setlocale(LC_ALL, ".65001");
	QTextCodec::setCodecForLocale  (QTextCodec::codecForName("UTF-8"));
#endif

	QCoreApplication::setApplicationName("Db Synchro");
	QCoreApplication::setApplicationVersion(QString("%1.%2").arg(FVERSION_MAJOR).arg(FVERSION_MINOR)); // 2022-11-23
	QCommandLineParser parser;
	parser.setApplicationDescription("Data synchronization utility in tables of two databases.");
	parser.addHelpOption();
	parser.addVersionOption();

	parser.addPositionalArgument			("TableFile",		"Path to the table data file (file in UTF8).");
	parser.addPositionalArgument			("DrvSrc",			"SQL Driver name for connecting: QOCI,QPSQL,...");
	parser.addPositionalArgument			("Source",			"Db: user/password@alias or user/password@db[:addr*port]");
	parser.addPositionalArgument			("DrvDst",			"SQL Driver name for connecting: QOCI,QPSQL,...");
	parser.addPositionalArgument			("Destination",		"Db: user/password@alias or user/password@db[:addr*port]");

	QCommandLineOption showAutoActionConf	("y",				"Confirm the automatic actions.");
	QCommandLineOption showIgnoreAllOption	("x",				"Show differences in tables only.");
	QCommandLineOption showIgnoreUpdOption	("u",				"Ignore rows updating.");
	QCommandLineOption showIgnoreInsOption	("i",				"Ignore rows adding.");
	QCommandLineOption showIgnoreDelOption	("d",				"Ignore rows deletion.");
	QCommandLineOption showLogOption        ("l",				"Write down to the log file.");
	QCommandLineOption showTriggOption      ("t",				"Disable trigger execution (off/on).");
	QCommandLineOption showMaxConnect       ("m",				"The number of concurrent database connections (1-20).", "connections");
	QCommandLineOption showMaxLineWdt       ("n",				"Symbols quantity limit in the log file line (50-999).", "limit");
	QCommandLineOption saveDebugInfo        ("b",				"Writing additional debugging information.");

	parser.addOption  (showAutoActionConf);
	parser.addOption  (showLogOption);
	parser.addOption  (showIgnoreAllOption);
	parser.addOption  (showIgnoreUpdOption);
	parser.addOption  (showIgnoreInsOption);
	parser.addOption  (showIgnoreDelOption);
	parser.addOption  (showTriggOption);
	parser.addOption  (showMaxConnect);
	parser.addOption  (showMaxLineWdt);
	parser.addOption  (saveDebugInfo);

	printf ("LeonSoft - %s %s\n\n", qPrintable(QCoreApplication::applicationName()), qPrintable(QCoreApplication::applicationVersion()));

	// тут может вылететь, например на ключе -v
	parser.process(a);// Process the actual command line arguments given by the user

	const QStringList args	= parser.positionalArguments();	// db connection
	if (args.size() != 5) {
		parser.showHelp();
		return -1;
	}

	// argum
	QString targetFile	= args.at(0);
	QString dbDrvSrc	= args.at(1);
	QString dbDrvDst	= args.at(3);
	// user / password @ db : addr * port
	QString sdb_alias	= "unknown";

#if (QT_VERSION >= QT_VERSION_CHECK(5, 15, 0))
	QStringList slist	= args.at(2).split(QRegExp("[/@:*]"), Qt::SkipEmptyParts);
#else
	QStringList slist	= args.at(2).split(QRegExp("[/@:*]"), QString::SkipEmptyParts);
#endif

	if (slist.size() >= 3) {
		sdb_alias = slist.at(0) + "/***@" + slist.at(2);
		if (slist.size() >= 4) sdb_alias += ":" + slist.at(3);
		if (slist.size() >= 5) sdb_alias += "*" + slist.at(4);
	}
	QString ddb_alias	= "unknown";

#if (QT_VERSION >= QT_VERSION_CHECK(5, 15, 0))
	QStringList dlist	= args.at(4).split(QRegExp("[/@:*]"), Qt::SkipEmptyParts);
#else
	QStringList dlist	= args.at(4).split(QRegExp("[/@:*]"), QString::SkipEmptyParts);
#endif
	if (dlist.size() >= 3) {
		ddb_alias = dlist.at(0) + "/***@" + dlist.at(2);
		if (dlist.size() >= 4) ddb_alias += ":" + dlist.at(3);		
		if (dlist.size() >= 5) ddb_alias += "*" + dlist.at(4);
	}

	// for log-file (path)
	QDir dir;
	QString		fp  = dir.absoluteFilePath(targetFile).replace("\\", "/");
	int     indxlog = fp.lastIndexOf(".");
	QString pathlog = (indxlog < 0) ? fp + ".log" : fp.mid(0, indxlog) + ".log";

	// file-tab
	tabcol	m_tabcol; QStringList filetab;	int err = -2;
	QFile file(fp);
	if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
		err = 0; int linen = 0;
		while (!file.atEnd()) {

			QString str = QString::fromUtf8 (file.readLine());	// 2022 uft8 file
			str			= str.trimmed();

			filetab.append(str);
			if (linen++ <= 2 && str.isEmpty() == true)	
				err = -3;
		}
		file.close();	// fix
	}
	if (err != 0 || filetab.size() < 3) {
		printf ("Error in 'TableFile' (#line. description): \n");
		printf ("1. table columns\n");
		printf ("2. key table columns\n");
		printf ("3. table name\n");
		printf ("4. [condition (where)]\n");
		printf ("5. [trigger(s)]\n");
		
		printf ("\nfile: %s (%d lines)\n", qPrintable(fp), filetab.size());
		for (int a=0; a<filetab.size();a++)
			printf ("%d. %s\n", a, qPrintable(filetab.at(a)));
		return -2;
	}

#ifdef WIN32
	{
		QString  syspath1 = QCoreApplication::applicationDirPath();
		QDir dr (syspath1);
		dr.cdUp();
		QString  syspath2 = dr.path();
		dr.cdUp();
		QString  syspath3 = dr.path();

		QString t	= qEnvironmentVariable("PATH");
		QString ora	= syspath1 + "/DbInstance/oracle;" + syspath2 + "/DbInstance/oracle;" + syspath3 + "/DbInstance/oracle;";
		QString psq	= syspath1 + "/DbInstance/psql;" + syspath2 + "/DbInstance/psql;" + syspath3 + "/DbInstance/psql;";
		QString tot	= ora + psq + t;
		tot = tot.replace ("/", "\\");
		bool bSetPath = qputenv ("PATH", tot.toLocal8Bit());
		t	          = qEnvironmentVariable("PATH");
	}
#endif

	//
#ifdef WIN32
	{
		void*     p   = nullptr;
		QString s86   = (sizeof(p) == 4) ? "/dbinstance/32" : "/dbinstance/64";
		QString pOra  = s86 + "/oracle";
		QString pPsq  = s86 + "/postgresql";
		bool bOraPath = false;
		bool bPsqPath = false;
		QSettings settings2 ("HKEY_CURRENT_USER\\Software\\Leonsoft", QSettings::NativeFormat);
		QVariant  varPathO = settings2.value(pOra);
		if (varPathO.isValid() == false || varPathO.toString().isEmpty() == true) {
			settings2.setValue(pOra, " ");
		} else {
			QDir dirO (varPathO.toString());
			bOraPath = dirO.exists();
		}
		QVariant  varPathP = settings2.value(pPsq);
		if (varPathP.isValid() == false || varPathP.toString().isEmpty() == true) {
			settings2.setValue(pPsq, " ");
		} else {
			QDir dirP (varPathP.toString());
			bPsqPath = dirP.exists();
		}

		QString t	= qEnvironmentVariable("PATH");
		if (bOraPath == true) { t = varPathO.toString() + ";" + t; }
		if (bPsqPath == true) { t = varPathP.toString() + ";" + t; }
		t = t.replace ("/", "\\");
		qputenv ("PATH", t.toLocal8Bit());
	}
#endif



#ifdef CRASH_ON
	CR_INSTALL_INFOA info; memset(&info, 0, sizeof(CR_INSTALL_INFOA));  

	int xpos;
	QString logsPath = ((xpos=pathlog.lastIndexOf('/')) > 0) ? pathlog.mid(0, xpos) : QCoreApplication::applicationDirPath();
	QString langPath = QCoreApplication::applicationDirPath() + "/crashrpt.ini";
	QDir dirr; dirr.mkpath (logsPath);

	char XX3[250];  strcpy (XX3, "DbSync");
	char XX2[250];  sprintf(XX2, "%d.%d", FVERSION_MAJOR, FVERSION_MINOR);
	char XX6[450];  strcpy (XX6, qPrintable(langPath));
	char XX7[450];  strcpy (XX7, qPrintable(logsPath));

	info.cb						  = sizeof(CR_INSTALL_INFO);    
	info.pszAppName				  = XX3;  
	info.pszAppVersion			  = XX2;  
	info.pszLangFilePath		  = XX6;
	info.pszErrorReportSaveDir	  = XX7;
	info.pfnCrashCallback		  = CrashCallback;   
	info.uPriorities[CR_HTTP]	  = CR_NEGATIVE_PRIORITY;  
	info.uPriorities[CR_SMTP]	  = CR_NEGATIVE_PRIORITY;	
	info.uPriorities[CR_SMAPI]	  = CR_NEGATIVE_PRIORITY;	
	info.dwFlags				 |= CR_INST_ALL_POSSIBLE_HANDLERS;
	info.dwFlags				 |= CR_INST_HTTP_BINARY_ENCODING; 
	info.dwFlags				 |= CR_INST_SEND_QUEUED_REPORTS; 
	info.dwFlags				 |= CR_INST_DONT_SEND_REPORT;
	info.dwFlags				 |= CR_INST_NO_GUI;
	int nResult = crInstallA(&info);	
	if (nResult != 0)	{ 
		char szErrorMsg[512]; szErrorMsg[0] = 0;
		crGetLastErrorMsgA	 (szErrorMsg, 511);    
		printf("%s\n",		  szErrorMsg);    
	} 
#endif

	// table data (processing)
#if (QT_VERSION >= QT_VERSION_CHECK(5, 15, 0))
	m_tabcol.col	= filetab.at(0).split(",", Qt::SkipEmptyParts);
	m_tabcol.colkey = filetab.at(1).split(",", Qt::SkipEmptyParts);
	m_tabcol.tab	= filetab.at(2);
	m_tabcol.where  = (filetab.size() > 3) ? filetab.at(3) : "";
	m_tabcol.trigg  = (filetab.size() > 4) ? filetab.at(4).split(",", Qt::SkipEmptyParts) : QStringList();
#else
	m_tabcol.col	= filetab.at(0).split(",", QString::SkipEmptyParts);
	m_tabcol.colkey = filetab.at(1).split(",", QString::SkipEmptyParts);
	m_tabcol.tab	= filetab.at(2);
	m_tabcol.where  = (filetab.size() > 3) ? filetab.at(3) : "";
	m_tabcol.trigg  = (filetab.size() > 4) ? filetab.at(4).split(",", QString::SkipEmptyParts) : QStringList();
#endif

	// get key column indexes
	for (int t = 0; t < m_tabcol.colkey.size(); t++) {
		for (int m = 0; m < m_tabcol.col.size(); m++) {
			if (m_tabcol.colkey.at(t).compare(m_tabcol.col.at(m), Qt::CaseInsensitive) == 0)
				m_tabcol.poskey.append(m);	// key column position
		}
	}

	m_tabcol.bLog   = parser.isSet(showLogOption);
	m_tabcol.bIgnAll= parser.isSet(showIgnoreAllOption);
	m_tabcol.bIgnUpd= parser.isSet(showIgnoreUpdOption);
	m_tabcol.bIgnIns= parser.isSet(showIgnoreInsOption);
	m_tabcol.bIgnDel= parser.isSet(showIgnoreDelOption);
	m_tabcol.bIgnTRG= parser.isSet(showTriggOption);
	m_tabcol.bAAC	= parser.isSet(showAutoActionConf);
	m_tabcol.bDebug = parser.isSet(saveDebugInfo); // debug
	m_tabcol.maxQSymb=-1;

	if (m_tabcol.bIgnAll == true) {	// 2022 fix
		m_tabcol.bIgnUpd = m_tabcol.bIgnIns = m_tabcol.bIgnDel = true;
	}

	int multDB		= 5;	// default connections DB
	if (parser.isSet(showMaxConnect))
		multDB = parser.value(showMaxConnect).toInt();
	if (multDB > 20)	multDB = 20;	// protection
	if (multDB < 1)		multDB = 1;

	if (parser.isSet(showMaxLineWdt)) {
		m_tabcol.maxQSymb = parser.value(showMaxLineWdt).toInt();
		if (m_tabcol.maxQSymb < 50)		m_tabcol.maxQSymb = 50;
		if (m_tabcol.maxQSymb > 999)	m_tabcol.maxQSymb = 999;
	}

	// show : 
	printf ("==================================================================\n");
	printf ("Source Db : %s  %s\n", qPrintable(dbDrvSrc), qPrintable(sdb_alias));
	printf ("Destin Db : %s  %s\n", qPrintable(dbDrvDst), qPrintable(ddb_alias));
	printf ("TableFile : %s\n", qPrintable(targetFile));
	printf ("  column  : %s\n", qPrintable(filetab.at(0)));
	printf ("  key col.: %s\n", qPrintable(filetab.at(1)));
	printf ("  table   : %s\n", qPrintable(filetab.at(2)));
	
	if (m_tabcol.where.size() > 0)
	printf ("  where   : %s\n", qPrintable(filetab.at(3)));
	else
	printf ("  where   : *NONE*\n");

	if (m_tabcol.trigg.size() > 0)
	printf ("  triggers: %s\n", qPrintable(filetab.at(4)));
	else
	printf ("  triggers: *NONE*\n");

	printf ("  \n");

	// warning!	rights
	bool bChngeTrg = true;
	if ((m_tabcol.bIgnAll == true) ||	// all ignore
		(m_tabcol.bIgnIns == true && m_tabcol.bIgnDel == true && m_tabcol.bIgnUpd == true)) bChngeTrg = false;

	if (m_tabcol.trigg.size() > 0      &&
		m_tabcol.bIgnTRG      != true  &&
		bChngeTrg		      == true)	
	{
		printf ("Attention! You must have rights to disable trigger(s).\n\n");
	}

	if (bChngeTrg == false) {
		printf ("\n*** CHECK ONLY ***\n\n");
	}

	// for debug
	if (m_tabcol.bDebug == true)  AddLog(&m_tabcol, ">> SQL\n", false);

	// works!
	QList<QSqlDatabase*> listSrcDb;
	QList<QSqlDatabase*> listDstDb;
	err = 0; linetab linesSrc, linesDst;
	for (;;)
	{
		// get Source -------
		listSrcDb = GetDb(args.at(2), dbDrvSrc, slist, "source", multDB);
		if (listSrcDb.size() == 0) { err = -10; break; }
		if (GetDataTable(listSrcDb, dbDrvSrc, &m_tabcol, &linesSrc) != 0) { err = -11; break; }

		// get Destin -------
		listDstDb = GetDb(args.at(4), dbDrvDst, dlist, "destination", multDB);
		if (listDstDb.size() == 0) { err = -12; break; }
		if (GetDataTable(listDstDb, dbDrvDst, &m_tabcol, &linesDst) != 0) { err = -13; break;	}

		// synchro
		err = TableComparison(listDstDb.at(0), dbDrvDst, &m_tabcol, &linesSrc, &linesDst);
		break;
	}
	// finish!
	printf ("\n\n");

	// clear
	for (int n1=0; n1 < listSrcDb.size(); n1++) {
		QSqlDatabase* p1 = listSrcDb.at(n1);
		p1->close();
		delete p1;
	}
	for (int n2=0; n2 < listDstDb.size(); n2++) {
		QSqlDatabase* p2 = listDstDb.at(n2);
		p2->close();
		delete p2;
	}

	// write log
	if (m_tabcol.bLog == true) {
		QFile ff (pathlog);
		if (ff.open(QFile::WriteOnly) == true) {
			ff.write (m_tabcol.logData.toUtf8());
			ff.close ();
		}
	}
#ifdef CRASH_ON
	crUninstall();	
#endif
	return err;
}
