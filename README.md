# DbSync

**Data synchronization utility in tables of two databases**

*This utility allows you to fix errors in the failure of data replication.*

**Usage**: dbsync [options] TableFile SqlDriver Source Destination

**Options**:

    -?, -h, --help  Displays this help.
    -v, --version   Displays version information.
    -l              Write to the log file.
    -x              Show differences in tables only.
    -u              Ignore rows updating.
    -i              ignore rows adding.
    -d              Ignore rows deletion.
    -t              Disable trigger execution (off/on).
    -y              Automatic actions confirmation.
    

**Arguments**:

      TableFile     Path to the table data file.
      SqlDriver     Driver name for connecting to the database: QOCI QPSQL
      Source        Source Db: user/password@alias[:host]
      Destination   Destination Db: user/password@alias[:host]
    
	
**TableFile**:
- table columns
- key table columns
- name table
- [condition (where)]
- [triggers]

note:
- key table columns - columns to define unique rows;
- where - processing only part of the data in the table;
- triggers - disabling replication triggers during data synchronization.
  
**Program start:**
1. Determine the existence and type of differences in the tables: "dbsync -l -x ..."
2. Check the log file
3. Synchronize data in tables: "dbsync -l ..."
4. Check the log file

The Examples folder contains examples of tables and calls to the program.

**Project building:**
1. qmake -makefile
2. make (gcc/mingw) or nmake (msvs)

You need:
- Qt 5 (v.5.11 and higher)
- compiler: MinGW, GCC, Microsoft Visual C++
- libraries for working with the database: oracle, postgresql
