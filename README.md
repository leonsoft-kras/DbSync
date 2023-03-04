# DbSync

**Data synchronization utility in tables of two databases**

*This utility allows you to fix errors in the failure of data replication.*

**Usage**: dbsync [options] TableFile DrvSrc Source DrvDst Destination

**Options**:

    -?, -h, --help   Displays this help.
    -v, --version    Displays version information.
    -y               Confirm the automatic actions.    
    -l               Write down to the log file.
    -x               Show differences in tables only.
    -u               Ignore rows updating.
    -i               Ignore rows adding.
    -d               Ignore rows deletion.
    -t               Disable trigger execution (off/on).
    -m <connections> The number of concurrent database connections (1-20).
    -n <limit>       Symbols quantity limit in the log file line (50-999).
    -b               Writing additional debugging information.

**Arguments**:

      TableFile       Path to the table data file.
      DrvSrc,DrvDst   Driver name for connecting to the (source,destination) database: QOCI QPSQL
      Source          Db: user/password@alias or user/password@db[:addr*port]
      Destination     Db: user/password@alias or user/password@db[:addr*port]

**TableFile**:

- table columns
- key table columns
- name table
- [condition (where)]
- [triggers]

note:
- key table columns - columns to define unique rows;
- where - process only part of the data in the table;
- triggers - disabling (replication) triggers during data synchronization.
  
**How to Use:**

DbSync is a command line application. You can use it like the following:

1. Detect the differences in the tables: "dbsync -l -x ..."
2. Check the log file
3. Synchronize data in tables: "dbsync -l ..."
4. Check the log file and the Db

The Examples folder contains examples of tables and the program invocation.

**Project building:**

1. qmake -makefile
2. make (gcc/mingw) or nmake (msvs)

You need:
- Qt 5 (v.5.11 and higher)
- compiler: MinGW, GCC, Microsoft Visual C++
- libraries for the database handling: oracle, postgresql

**Folder "DBINSTANCE" (for Windows):**

SubFolder "DbInstance\Psql":
- iconv.dll
- libeay32.dll
- libiconv.dll
- libintl.dll
- libpq.dll
- libxml2.dll
- libxslt.dll
- ssleay32.dll

SubFolder "DbInstance\Oracle":
- Files from Oracle instant client 
