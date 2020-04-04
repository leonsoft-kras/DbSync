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
			qWarning () << "Warning: Unsupported column type ("<< type << "). Check program execution";
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
			qWarning () << "Warning: Unsupported column type ("<< type << "). Check program execution";
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
	if (m_tabcol->trigg.size() == 0)
		return 0;

	for (int i = 0; i < m_tabcol->trigg.size(); i++) {
		QString nametrg = m_tabcol->trigg.at(i);
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
			qCritical() << "Error executing SQL-request: " << x1;
			qCritical() << "                           : " << x2;
			AddLog(m_tabcol, x1);
			AddLog(m_tabcol, x2);
			AddLog(m_tabcol, sql); 
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
		qCritical() << "Error executing SQL-request: " << x1;
		qCritical() << "                           : " << x2;
		AddLog(m_tabcol, x1);
		AddLog(m_tabcol, x2);
		AddLog(m_tabcol, sql); 
		pdb->rollback();	
	}
	return err;
}

//-------------------------------------------------------------------------------------------------
int TableComparison(QSqlDatabase* pdb, QString SqlDrv, tabcol* m_tabcol, linetab* m_linesS, linetab* m_linesD)
{
	qInfo () << "\n---------------------------------";
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

	if (m_tabcol->bIgnTRG == false && 
		TriggersOn (pdb, SqlDrv, m_tabcol, false) != 0) { 
		QString txt1  = "Trigger is not disabled. Data change disabled";
		qWarning () << txt1;
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
			AddLog(m_tabcol, GetRows (&vlist, &m_linesS->m_typeCol));
			qInfo() << "Diff.Rows: " << strKeyS;

			if (m_tabcol->bIgnAll == false && m_tabcol->bIgnUpd == false)
				err += SychroDatab(pdb, SqlDrv, 2, m_tabcol, m_linesS, m_linesD, i, j); // update

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
		AddLog(m_tabcol, GetRows (&vlist, &m_linesD->m_typeCol));
		QString strKeyD = GetKeyStr(m_linesD->m_typeCol, m_linesD->m_nameCol, m_linesD->m_tabdata.at(j), m_tabcol->poskey);  // destination
		qInfo() << "Unwd.Rows: " << strKeyD;

		if (m_tabcol->bIgnAll == false && m_tabcol->bIgnDel == false)
			err += SychroDatab(pdb, SqlDrv, 0, m_tabcol, m_linesS, m_linesD, 0, j);	// delete

		UnwantedRows++;
	}

	AddLog(m_tabcol, QString("-----------------------------------------"));
	AddLog(m_tabcol, QString("- Missing Rows (source DB):"));

	// add missing rows
	int MissingRows = 0;
	for (int i = 0; i < m_linesS->m_crcline.size(); i++)	{

		QVariantList vlist = m_linesS->m_tabdata.at(i);
		AddLog(m_tabcol, GetRows (&vlist, &m_linesS->m_typeCol));
		QString strKeyS= GetKeyStr(	m_linesS->m_typeCol, m_linesS->m_nameCol, m_linesS->m_tabdata.at(i), m_tabcol->poskey);  // source
		qInfo() << "Miss.Rows: " << strKeyS;

		if (m_tabcol->bIgnAll == false && m_tabcol->bIgnIns == false)
			err += SychroDatab(pdb, SqlDrv, 1, m_tabcol, m_linesS, m_linesD, i, 0);	// insert

		MissingRows++;
	}

	if (m_tabcol->bIgnTRG == false && 
		TriggersOn (pdb, SqlDrv, m_tabcol, true) != 0) {
		QString txt1  = "Trigger is not enabled. Check database!";
		qWarning () << txt1;
		AddLog(m_tabcol, txt1);
		m_tabcol->bIgnAll = true;
	}

	AddLog(m_tabcol, QString("-----------------------------------------"));
	if (DifferentRows != 0 || UnwantedRows != 0 || MissingRows != 0)
		qInfo () << "\n---------------------------------";

	AddLog(m_tabcol, QString("Total:"));
	AddLog(m_tabcol, QString("   Identical rows: %1").arg(DuplicateRows));	qInfo () << "Identical rows: " << DuplicateRows;
	AddLog(m_tabcol, QString("   Different rows: %1").arg(DifferentRows));	qInfo () << "Different rows: " << DifferentRows;
	AddLog(m_tabcol, QString("   Unwanted rows : %1").arg(UnwantedRows ));	qInfo () << "Unwanted rows : " << UnwantedRows;
	AddLog(m_tabcol, QString("   Missing rows  : %1").arg(MissingRows  ));	qInfo () << "Missing rows  : " << MissingRows ;

	if (err != 0) {
		QString tt = "Warning: there were errors when changing the data in the table.";
		qWarning() << "\n" << tt;
		AddLog(m_tabcol, tt);
	}
	return err;
}


//-------------------------------------------------------------------------------------------------
int GetDataTable(QSqlDatabase* pdb, QString SqlDrv, tabcol*	m_tabcol, linetab* m_lines)
{
	qInfo() << "Read data :  start";

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
		qCritical() << "Error in determining the SQL-driver:" << SqlDrv;
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
		qCritical() << "Error executing SQL-request: " << x1;

		AddLog(m_tabcol, x1);
		AddLog(m_tabcol, sql);
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
//		int			plen	= oField.length ();
//		int			type1   = oField.typeID();
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
	qInfo() << "Read data :  finish";
	return 0;
}



//-------------------------------------------------------------------------------------------------
QSqlDatabase* GetDb(QString db, QString drv)
{
	qInfo() << "Connect Db: " << db;
	QSqlDatabase  xx = QSqlDatabase::addDatabase(drv, QString("DbSync.%1").arg(QTime::currentTime().msecsSinceStartOfDay()));
	QStringList list = db.split(QRegExp("[/@:]"), QString::SkipEmptyParts);
	if (list.size() < 3) { 
		qCritical()<< "Error in database connection settings: " << db;	
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
		qCritical() << "Unsupported SQL-driver type. Program revision required";
		return nullptr;
	}

	if (!xx.open()) {
		qCritical()<< "Error connecting to database: " << xx.lastError().text().replace("\n","; ");	
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
	QCoreApplication::setApplicationVersion("1.0");

	QCommandLineParser parser;
	parser.setApplicationDescription("Data synchronization in tables of two databases");
	parser.addHelpOption();
	parser.addVersionOption();

	parser.addPositionalArgument			("TableFile",		"Path to table data file.");
	parser.addPositionalArgument			("SqlDriver",		"Driver name for connecting to the database");
	parser.addPositionalArgument			("Source",			"Source Db: user/password@alias[:host]");
	parser.addPositionalArgument			("Destination",		"Destination Db: user/password@alias[:host]");

	QCommandLineOption showIgnoreAllOption	("x",				"Show differences in tables only.");
	QCommandLineOption showIgnoreUpdOption	("u",				"Ignore rows update.");
	QCommandLineOption showIgnoreInsOption	("i",				"ignore rows adding.");
	QCommandLineOption showIgnoreDelOption	("d",				"Ignore rows deletion.");
	QCommandLineOption showLogOption        ("l",				"Write to the log file.");
	QCommandLineOption showTriggOption      ("t",				"Disable trigger processing (off/on).");

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
		qInfo () << "Error in 'TableFile': \nLine (#):";
		qInfo () << "1: table columns";
		qInfo () << "2: key table columns";
		qInfo () << "3: name table";
		qInfo () << "4: condition (where)";
		qInfo () << "5: triggers";
		return -2;
	}

	m_tabcol.bLog   = parser.isSet(showLogOption);
	m_tabcol.bIgnAll= parser.isSet(showIgnoreAllOption);
	m_tabcol.bIgnUpd= parser.isSet(showIgnoreUpdOption);
	m_tabcol.bIgnIns= parser.isSet(showIgnoreInsOption);
	m_tabcol.bIgnDel= parser.isSet(showIgnoreDelOption);
	m_tabcol.bIgnTRG= parser.isSet(showTriggOption);

	m_tabcol.col	= filetab.at(0).split(",");
	m_tabcol.colkey = filetab.at(1).split(",");
	m_tabcol.tab	= filetab.at(2);
	m_tabcol.where  = (filetab.size() > 3) ? filetab.at(3) : "";
	m_tabcol.trigg  = (filetab.size() > 4) ? filetab.at(4).split(",") : QStringList();
	
	// get key column indexes
	for (int t = 0; t < m_tabcol.colkey.size(); t++) {
		for (int m = 0; m < m_tabcol.col.size(); m++) {
			if (m_tabcol.colkey.at(t).compare(m_tabcol.col.at(m), Qt::CaseInsensitive) == 0)
				m_tabcol.poskey.append(m);	// key column position
		}
	}

	// print
	qInfo () << "SQL Driver: " << dbDrv;
	qInfo () << "Source Db : " << args.at(2);
	qInfo () << "Destin Db : " << args.at(3);
	qInfo () << "TableFile : " << targetFile;
	qInfo () << "  column  : " << filetab.at(0);
	qInfo () << "  key col.: " << filetab.at(1);
	qInfo () << "  table   : " << filetab.at(2);
	if (filetab.size() > 3)
	qInfo () << "  where   : " << filetab.at(3);
	if (filetab.size() > 4)
	qInfo () << "  triggers: " << filetab.at(4);
	qInfo () << "  ";

	QSqlDatabase *p1 = nullptr, *p2 = nullptr;
	err = 0; linetab linesSrc, linesDst;
	for (;;)
	{
		// get Source -------
		p1 = GetDb(args.at(2), dbDrv);
		if (p1 == nullptr) { err = -10; break; }

		if (GetDataTable(p1, dbDrv, &m_tabcol, &linesSrc) != 0) { err = -11; break; }
		p1->close();

		// get Destin -------
		p2 = GetDb(args.at(3), dbDrv);
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
