/*
 * Db Sync
 * Copyright (C) 2020 by A.Petrov (Leonsoft)
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

#include <conio.h>
#include <QTextStream>

typedef struct 
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

	bool		bLog	= false;	
	QString		logData	= "";

} tabcol;


typedef QList<QVariantList> tabdata;
typedef struct
{
	QList<int>	m_typeCol;	// columns type
	QStringList	m_nameCol;	// columns name
	QList<int>	m_crcline;	// CRC per row
	tabdata		m_tabdata;	// all rows of the table in columns

} linetab;


//-------------------------------------------------------------------------------------------------
void AddLog (tabcol* ptabcol, QString txt)	
{
	ptabcol->logData += txt;
	ptabcol->logData += "\n";
}

//-------------------------------------------------------------------------------------------------
QString SetVariantStr(int type, QVariant var, int pos)	// column data to form a query
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
			r = QString("to_timestamp('%1.%2.%3 %4:%5:%6.%7', 'dd.mm.yyyy hh24:mi:ss.ff')").
				arg(d.day(), 2, 10, QChar('0')).arg(d.month(), 2, 10, QChar('0')).arg(d.year(), 4, 10, QChar('0')).
				arg(t.hour(), 2, 10, QChar('0')).arg(t.minute(), 2, 10, QChar('0')).arg(t.second(), 2, 10, QChar('0')).arg(t.msec(), 3, 10, QChar('0'));
			break;
		}
		case QVariant::Int:
			r = var.toString();
			break;
		case QVariant::String:
			r = "'" + var.toString() + "'";
			break;
		case QVariant::ByteArray: {
			r = QString(":id%1").arg(pos);
			break;
		}
		default:
			// error !!!
			r = var.toString();
			printf ("Warning: Unsupported column type (%d). Check program execution\n", type);
			break;
		}
	}
	return r;
}

//-------------------------------------------------------------------------------------------------
QString GetVariantStr(int type, QVariant var)	// column data from the database
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
				arg(d.day(), 2, 10, QChar('0')).arg(d.month(), 2, 10, QChar('0')).arg(d.year(), 4, 10, QChar('0')).
				arg(t.hour(), 2, 10, QChar('0')).arg(t.minute(), 2, 10, QChar('0')).arg(t.second(), 2, 10, QChar('0')).arg(t.msec(), 3, 10, QChar('0'));
			break;  
		}
		case QVariant::ByteArray: {	
			QByteArray ba = var.toByteArray().toHex(':');
			r = QString(ba.data());
			break;	
		}
		case QVariant::Double:
 			r = var.toString();
 			break;
		case QVariant::Int:
 			r = var.toString();
 			break;
		case QVariant::String:
			r = var.toString();
			break;
		default:					
			r = var.toString();
			printf ("Warning: Unsupported column type (%d). Check program execution\n", type);
			break;
		}
	}
	return r;
}

//-------------------------------------------------------------------------------------------------
QString GetKeyStr(	QList<int>  m_type, 
					QStringList	m_name,
					QVariantList vl, QList<int> keys) // forming a row of key columns
{
	QString r = "";
	for (int n = 0; n < keys.size(); n++) {
		int indx		= keys.at(n);
//		QString test	= m_name.at(indx);
		r += GetVariantStr(m_type.at(indx), vl.at(indx)) + "; ";
	}
	return r;
}

//-------------------------------------------------------------------------------------------------
QString GetRows (QVariantList* vlist, QList<int>* m_typeCol)
{
	QString xx = "";
	for (int i = 0; i < vlist->size() - 1; i++)	// exclude rowid (last rows!)
		xx += GetVariantStr(m_typeCol->at(i), vlist->at(i)) + "; ";
	return xx;
}

//-------------------------------------------------------------------------------------------------
int GetCrc(QVariantList* vlist, QList<int>*	m_typeCol)	// checksum calculation for all table columns
{
	QByteArray bb = GetRows (vlist, m_typeCol).toUtf8();
	return qChecksum(bb.data(), bb.size());
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
			sql += "alter table " + m_tabcol->tab;
			if (bOn == true)	sql += " enable ";
			else				sql += " disable ";
			sql += "trigger \"" + nametrg + "\"";
		} 
		else {
			return -29;
		}

		QSqlQuery query(*pdb);
		for (;;) {
			if (pdb->transaction() == false) { err = -22; break; }
			if (query.prepare(sql) == false) { err = -23; break; }
			if (query.exec()       == false) { err = -24; break; }	// execute SQL (tab modify)
			if (pdb->commit()      == false) { err = -25; break; }
			err = 0;
			break;
		}

		if (err != 0) {
			QString x1 = query.lastError().text().replace("\n", "; ");
			QString x2 = pdb-> lastError().text().replace("\n", "; ");
			printf("Error executing SQL-request: %s\n", qPrintable(x1));
			printf("                           : %s\n", qPrintable(x2));
			AddLog(m_tabcol, "Err: " + x1);
			AddLog(m_tabcol, "Err: " + x2);
			AddLog(m_tabcol, "SQL: " + sql); 
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
		printf("Error executing SQL-request: %s\n", qPrintable(x1));
		printf("                           : %s\n", qPrintable(x2));
		AddLog(m_tabcol, "Err: " + x1);
		AddLog(m_tabcol, "Err: " + x2);
		AddLog(m_tabcol, "SQL: " + sql); 
		pdb->rollback();	
	}
	return err;
}

//-------------------------------------------------------------------------------------------------
int TableComparison(QSqlDatabase* pdb, QString SqlDrv, tabcol* m_tabcol, linetab* m_linesS, linetab* m_linesD)
{
	printf ("\n---------------------------------\n");
	int err = 0;

	QString txt = "Columns: ";
	for (int b=0; b < m_tabcol->col.size() - 1; b++) txt += m_tabcol->col.at(b) + "; ";
	AddLog(m_tabcol, txt);

	// search & ignore duplicate rows
	int DuplicateRows = 0;
	for (int i = 0; i < m_linesS->m_crcline.size(); i++) {
		int crcS = m_linesS->m_crcline.at(i);

		for (int j = 0; j < m_linesD->m_crcline.size(); j++) {
			int crcD = m_linesD->m_crcline.at(j);

			if (crcD != crcS) // !same row
				continue;
			
			m_linesS->m_crcline.removeAt(i);	m_linesS->m_tabdata.removeAt(i);
			m_linesD->m_crcline.removeAt(j);	m_linesD->m_tabdata.removeAt(j);
			i--; j--; DuplicateRows++;
			break;
		}
	}

	if ((m_tabcol->bIgnTRG == false && m_tabcol->trigg.size() > 0) && TriggersOn (pdb, SqlDrv, m_tabcol, false) != 0) { 
		QString txt1  = "Trigger is not disabled. Data change disabled";
		printf("%s\n", qPrintable(txt1));
		AddLog(m_tabcol, txt1);
		m_tabcol->bIgnAll = true;
	}

	AddLog(m_tabcol, QString("-----------------------------------------"));
	AddLog(m_tabcol, QString("- Different Rows (source DB):"));

	// search & update different rows
	int DifferentRows = 0;
	for (int i = 0; i < m_linesS->m_crcline.size(); i++)	{
		QString strKeyS= GetKeyStr(	m_linesS->m_typeCol, m_linesS->m_nameCol, m_linesS->m_tabdata.at(i), m_tabcol->poskey);  // source

		for (int j = 0; j < m_linesD->m_crcline.size(); j++)	{
			QString strKeyD = GetKeyStr(m_linesD->m_typeCol, m_linesD->m_nameCol, m_linesD->m_tabdata.at(j), m_tabcol->poskey);  // destination

			if (strKeyS != strKeyD)	// !same row, but...
				continue;
		
			QVariantList vlist = m_linesS->m_tabdata.at(i);
			QString ScreenStr  = GetRows (&vlist, &m_linesS->m_typeCol);
			AddLog(m_tabcol, ScreenStr);

			if (m_tabcol->bIgnAll == false && m_tabcol->bIgnUpd == false) {
				
				printf ("Diff.Rows: %s\n", qPrintable(ScreenStr));
				bool bY = true;
				if (m_tabcol->bAAC == false) {
					printf ("Replace data (y/n) ?"); 
					int ass = getch ();
					if (!(ass == 'Y' || ass == 'y'))	bY = false;
					printf ("\r                                \r");
				}

				if (bY == true)
					err += SychroDatab(pdb, SqlDrv, 2, m_tabcol, m_linesS, m_linesD, i, j); // update
			}
			else {
				printf ("Diff.Rows: %s\n", qPrintable(strKeyS));
			}


			m_linesS->m_crcline.removeAt(i);	m_linesS->m_tabdata.removeAt(i);
			m_linesD->m_crcline.removeAt(j);	m_linesD->m_tabdata.removeAt(j);
			i--; j--; DifferentRows++;
			break;
		}
	}

	AddLog(m_tabcol, QString("-----------------------------------------"));
	AddLog(m_tabcol, QString("- Unwanted Rows (destination DB):"));

	// delete unwanted rows
	int UnwantedRows = 0;
	for (int j = 0; j < m_linesD->m_crcline.size(); j++)	{

		QVariantList vlist = m_linesD->m_tabdata.at(j);
		QString ScreenStr = GetRows (&vlist, &m_linesD->m_typeCol);
		AddLog(m_tabcol, ScreenStr);

		if (m_tabcol->bIgnAll == false && m_tabcol->bIgnDel == false) {
			printf ("Unwd.Rows: %s\n", qPrintable(ScreenStr));
			bool bY = true;
			if (m_tabcol->bAAC == false) {
				printf ("Delete data (y/n) ?");
				int ass = getch ();
				if (!(ass == 'Y' || ass == 'y'))	bY = false;
				printf ("\r                                \r");
			}

			if (bY == true)
				err += SychroDatab(pdb, SqlDrv, 0, m_tabcol, m_linesS, m_linesD, 0, j);	// delete
		}
		else {
			QString strKeyD = GetKeyStr(m_linesD->m_typeCol, m_linesD->m_nameCol, m_linesD->m_tabdata.at(j), m_tabcol->poskey);  // destination
			printf ("Unwd.Rows: %s\n", qPrintable(strKeyD));
		}

		UnwantedRows++;
	}

	AddLog(m_tabcol, QString("-----------------------------------------"));
	AddLog(m_tabcol, QString("- Missing Rows (source DB):"));

	// add missing rows
	int MissingRows = 0;
	for (int i = 0; i < m_linesS->m_crcline.size(); i++)	{

		QVariantList vlist = m_linesS->m_tabdata.at(i);
		QString ScreenStr  = GetRows (&vlist, &m_linesS->m_typeCol);
		AddLog(m_tabcol, ScreenStr);

		if (m_tabcol->bIgnAll == false && m_tabcol->bIgnIns == false) {
			printf ("Miss.Rows: %s\n", qPrintable(ScreenStr));
			bool bY = true;
			if (m_tabcol->bAAC == false) {
				printf ("Insert data (y/n) ?");
				int ass = getch ();
				if (!(ass == 'Y' || ass == 'y'))	bY = false;
				printf ("\r                                \r");
			}

			if (bY == true)
				err += SychroDatab(pdb, SqlDrv, 1, m_tabcol, m_linesS, m_linesD, i, 0);	// insert
		}
		else {
			QString strKeyS= GetKeyStr(m_linesS->m_typeCol, m_linesS->m_nameCol, m_linesS->m_tabdata.at(i), m_tabcol->poskey);  // source
			printf ("Miss.Rows: %s\n", qPrintable(strKeyS));
		}

		MissingRows++;
	}


	if ((m_tabcol->bIgnTRG == false && m_tabcol->trigg.size() > 0) && TriggersOn (pdb, SqlDrv, m_tabcol, true) != 0) { // ????
		QString txt1  = "Trigger is not enabled. Check database!";
		printf ("%s\n", qPrintable(txt1));
		AddLog(m_tabcol, txt1);
		m_tabcol->bIgnAll = true;
	}

	AddLog(m_tabcol, QString("-----------------------------------------"));
	if (DifferentRows != 0 || UnwantedRows != 0 || MissingRows != 0)
		printf ("\n---------------------------------\n");

	AddLog(m_tabcol, QString("Total:"));
	AddLog(m_tabcol, QString("   Identical rows: %1").arg(DuplicateRows));	qInfo () << "Identical rows: " << DuplicateRows;
	AddLog(m_tabcol, QString("   Different rows: %1").arg(DifferentRows));	qInfo () << "Different rows: " << DifferentRows;
	AddLog(m_tabcol, QString("   Unwanted rows : %1").arg(UnwantedRows ));	qInfo () << "Unwanted rows : " << UnwantedRows;
	AddLog(m_tabcol, QString("   Missing rows  : %1").arg(MissingRows  ));	qInfo () << "Missing rows  : " << MissingRows ;

	if (err != 0) {
		QString tt = "Warning: there were errors when changing the data in the table.";
		printf ("\n%s\n)",qPrintable(tt));
		AddLog(m_tabcol, tt);
	}
	return err;
}


//-------------------------------------------------------------------------------------------------
int GetDataTable(QSqlDatabase* pdb, QString SqlDrv, tabcol*	m_tabcol, linetab* m_lines)
{
	printf ("Read data : start\n");

	QString sql = "select ";
	for (int i = 0; i < m_tabcol->col.size(); i++) {
		sql += "t.";
		sql += m_tabcol->col.at(i).trimmed();
		sql += ", ";
	}
	if (SqlDrv == "QOCI")	sql += "t.rowid";
 	else
 	if (SqlDrv == "QPSQL")	sql += "ctid";
	else {
		printf ("Error in determining the SQL-driver: %s\n", qPrintable (SqlDrv));
		return -30;	// w/o rowid !!!
	}

	sql += " from ";
	sql += m_tabcol->tab;
	sql += " t ";
	m_tabcol->where = m_tabcol->where.trimmed();
	if (m_tabcol->where.isEmpty() == false) {
		sql += " where ";
		sql += m_tabcol->where;
	}

	// execute sql
	QSqlQuery query(*pdb);
	bool b = query.exec(sql);
	if (b == false) {
		QString x1 = query.lastError().text().replace("\n", "; ");
		printf ("Error executing SQL-request: %s\n", qPrintable(x1));

		AddLog(m_tabcol, "Err: " + x1);
		AddLog(m_tabcol, "SQL: " + sql);
		return -31;
	}

	//-----------------------
	// record data (name, type)
	QSqlRecord rec = query.record();
	int  cntField  = rec.count();
	for (int n = 0; n < cntField; n++)	{
		QSqlField  oField	= rec.field(n);
		QString    nname	= oField.name();
		QVariant   vartype	= oField.type();
		int			precis	= oField.precision();
		int			typeCol =(int)vartype.type();

		if (typeCol == (int)QVariant::Double && precis == 0) {
			typeCol = QVariant::Int;
		}

		m_lines->m_nameCol.append(nname);
		m_lines->m_typeCol.append(typeCol);
	}

	// read records
	while (query.next())	{
		QVariantList mvlist;
		for (int n = 0; n < cntField; n++)
			mvlist.append(query.value(n));
		
		m_lines->m_tabdata.append( mvlist );
		m_lines->m_crcline.append( GetCrc(&mvlist, &m_lines->m_typeCol) );
	} 
	printf ("Read data : finish\n\n");
	return 0;
}



//-------------------------------------------------------------------------------------------------
QSqlDatabase* GetDb(QString db, QString drv, const char* name)
{
	QSqlDatabase  xx = QSqlDatabase::addDatabase(drv, QString("DbSync.%1").arg(QTime::currentTime().msecsSinceStartOfDay()));
	QStringList list = db.split(QRegExp("[/@:]"), QString::SkipEmptyParts);
	if (list.size() < 3) { 
		printf ("Error in %s Db connection settings\n", name);	
		return nullptr;	
	}

	xx.setUserName		(list.at(0));
	xx.setPassword		(list.at(1));
	xx.setDatabaseName	(list.at(2));
	if (list.size() > 3 && list.at(3).isEmpty() == false)
		xx.setHostName	(list.at(3));

	if (drv == "QOCI")	
		xx.setConnectOptions("OCI_ATTR_PREFETCH_ROWS=4000;OCI_ATTR_PREFETCH_MEMORY=524288;");
	else
	if (drv == "QPSQL") {
		// nothing
	}
	else {
		printf ("Unsupported SQL-driver type. Program revision required\n");
		return nullptr;
	}

	printf ("Connect Db: %s\n", name);
	if (!xx.open()) {
		printf ("Error connecting to database: %s\n", qPrintable( xx.lastError().text().replace("\n","; ") ));	
		return nullptr;	
	}

	QSqlDatabase* p = new QSqlDatabase(xx);
	return p;
}


//-------------------------------------------------------------------------------------------------
int main(int argc, char *argv[])
{
	QCoreApplication a(argc, argv);
	QCoreApplication::setApplicationName("Db Synchro");
	QCoreApplication::setApplicationVersion("1.2");

	QCommandLineParser parser;
	parser.setApplicationDescription("Data synchronization utility in tables of two databases.");
	parser.addHelpOption();
	parser.addVersionOption();

	parser.addPositionalArgument			("TableFile",		"Path to the table data file.");
	parser.addPositionalArgument			("SqlDriver",		"Driver name for connecting to the database");
	parser.addPositionalArgument			("Source",			"Source Db: user/password@alias[:host]");
	parser.addPositionalArgument			("Destination",		"Destination Db: user/password@alias[:host]");

	QCommandLineOption showAutoActionConf	("y",				"Automatic actions confirmation.");
	QCommandLineOption showIgnoreAllOption	("x",				"Show differences in tables only.");
	QCommandLineOption showIgnoreUpdOption	("u",				"Ignore rows updating.");
	QCommandLineOption showIgnoreInsOption	("i",				"ignore rows adding.");
	QCommandLineOption showIgnoreDelOption	("d",				"Ignore rows deletion.");
	QCommandLineOption showLogOption        ("l",				"Write to the log file.");
	QCommandLineOption showTriggOption      ("t",				"Disable trigger execution (off/on).");

	parser.addOption  (showAutoActionConf);
	parser.addOption  (showLogOption);
	parser.addOption  (showIgnoreAllOption);
	parser.addOption  (showIgnoreUpdOption);
	parser.addOption  (showIgnoreInsOption);
	parser.addOption  (showIgnoreDelOption);
	parser.addOption  (showTriggOption);
	parser.process(a);// Process the actual command line arguments given by the user

	const QStringList args	= parser.positionalArguments();	// db connection
	if (args.size() != 4) {
		parser.showHelp();
		return -1;
	}

	QString targetFile	= args.at(0);
	QString dbDrv		= args.at(1);

	// for log-file (path)
	QDir dir;
	QString		fp  = dir.absoluteFilePath(targetFile).replace("\\", "/");
	int     indxlog = fp.lastIndexOf(".");
	QString pathlog = (indxlog < 0) ? fp + ".log" : fp.mid(0, indxlog) + ".log";

	tabcol	m_tabcol; QStringList filetab;	int err = -2;
	QFile file(fp);
	if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
		err = 0;
		while (!file.atEnd())	filetab.append(file.readLine().trimmed());
	}
	if (err != 0 || filetab.size() <= 3) {
		printf ("Error in 'TableFile' (#line. description): \n");
		printf ("1: table columns\n");
		printf ("2: key table columns\n");
		printf ("3: name table\n");
		printf ("4: condition (where)\n");
		printf ("5: triggers\n");
		return -2;
	}

	m_tabcol.bLog   = parser.isSet(showLogOption);
	m_tabcol.bIgnAll= parser.isSet(showIgnoreAllOption);
	m_tabcol.bIgnUpd= parser.isSet(showIgnoreUpdOption);
	m_tabcol.bIgnIns= parser.isSet(showIgnoreInsOption);
	m_tabcol.bIgnDel= parser.isSet(showIgnoreDelOption);
	m_tabcol.bIgnTRG= parser.isSet(showTriggOption);
	m_tabcol.bAAC	= parser.isSet(showAutoActionConf);

	m_tabcol.col	= filetab.at(0).split(",", QString::SkipEmptyParts);
	m_tabcol.colkey = filetab.at(1).split(",", QString::SkipEmptyParts);
	m_tabcol.tab	= filetab.at(2);
	m_tabcol.where  = (filetab.size() > 3) ? filetab.at(3) : "";
	m_tabcol.trigg  = (filetab.size() > 4) ? filetab.at(4).split(",", QString::SkipEmptyParts) : QStringList();
	
	// get key column indexes
	for (int t = 0; t < m_tabcol.colkey.size(); t++) {
		for (int m = 0; m < m_tabcol.col.size(); m++) {
			if (m_tabcol.colkey.at(t).compare(m_tabcol.col.at(m), Qt::CaseInsensitive) == 0)
				m_tabcol.poskey.append(m);	// key column position
		}
	}

	// print
	QString sdb_alias= "unknown";
	QStringList slist= args.at(2).split(QRegExp("[/@:]"), QString::SkipEmptyParts);
	if (slist.size() >= 3) {
		sdb_alias = slist.at(0) + "/***@" + slist.at(2);
		if (slist.size() >= 4) sdb_alias += ":" + slist.at(3);		
	}
	QString ddb_alias= "unknown";
	QStringList dlist= args.at(3).split(QRegExp("[/@:]"), QString::SkipEmptyParts);
	if (dlist.size() >= 3) {
		ddb_alias = dlist.at(0) + "/***@" + dlist.at(2);
		if (dlist.size() >= 4) ddb_alias += ":" + dlist.at(3);		
	}

	printf ("SQL Driver: %s\n", qPrintable(dbDrv));
	printf ("Source Db : %s\n", qPrintable(sdb_alias));
	printf ("Destin Db : %s\n", qPrintable(ddb_alias));
	printf ("TableFile : %s\n", qPrintable(targetFile));
	printf ("  column  : %s\n", qPrintable(filetab.at(0)));
	printf ("  key col.: %s\n", qPrintable(filetab.at(1)));
	printf ("  table   : %s\n", qPrintable(filetab.at(2)));
	if (filetab.size() > 3)
		printf ("  where   : %s\n", qPrintable(filetab.at(3)));
	if (filetab.size() > 4)
		printf ("  triggers: %s\n", qPrintable(filetab.at(4)));
	printf ("  \n");

	QSqlDatabase *p1 = nullptr, *p2 = nullptr;
	err = 0; linetab linesSrc, linesDst;
	for (;;)
	{
		// get Source -------
		p1 = GetDb(args.at(2), dbDrv, "source");
		if (p1 == nullptr) { err = -10; break; }

		if (GetDataTable(p1, dbDrv, &m_tabcol, &linesSrc) != 0) { err = -11; break; }
		p1->close();

		// get Destin -------
		p2 = GetDb(args.at(3), dbDrv, "destination");
		if (p2 == nullptr) { err = -12; break; }

		if (GetDataTable(p2, dbDrv, &m_tabcol, &linesDst) != 0) { err = -13; break;	}

		// synchro
		err = TableComparison(p2, dbDrv, &m_tabcol, &linesSrc, &linesDst);
		p2->close();
		break;
	}

	delete p1; p1 = nullptr;
	delete p2; p2 = nullptr;

	// write log
	if (m_tabcol.bLog == true) {
		QFile ff (pathlog);
		if (ff.open(QFile::WriteOnly) == true) {
			ff.write (m_tabcol.logData.toUtf8());
			ff.close ();
		}
	}
	return err;
}
