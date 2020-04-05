# DbSync
**Data synchronization in tables of two databases**
*This utility allows you to fix errors in the failure of data replication.*

**Usage**: dbsync [options] TableFile SqlDriver Source Destination

**Options**:

    -?, -h, --help  Displays this help.
    -v, --version   Displays version information.
    -l                    Write to the log file.
    -x                   Show differences in tables only.
    -u                   Ignore rows update.
    -i                    ignore rows adding.
    -d                   Ignore rows deletion.
    -t                    Disable trigger processing (off/on).
    

**Arguments**:

      TableFile       Path to table data file.
      SqlDriver       Driver name for connecting to the database: QOCI QPSQL
      Source          Source Db: user/password@alias[:host]
      Destination   Destination Db: user/password@alias[:host]
    
**TableFile**:
- table columns
- key table columns
- name table
- [condition (where)]
- [triggers]

note:
key table columns - columns to define unique rows;
where - processing only part of the data in the table;
triggers - disabling replication triggers during data synchronization.
  
**Program start:**
1. determine the existence and type of differences in the tables: "dbsync -l -x ..."
2. synchronize data in tables: "dbsync -l ..."
3. check the log file

The Examples folder contains examples of tables and calls to the program.
