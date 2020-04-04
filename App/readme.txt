Usage: dbsync [options] TableFile SqlDriver Source Destination
Data synchronization in tables of two databases

Options:
  -?, -h, --help  Displays this help.
  -v, --version   Displays version information.
  -l              Write to the log file.
  -x              Show differences in tables only.
  -u              Ignore rows update.
  -i              ignore rows adding.
  -d              Ignore rows deletion.
  -t              Disable trigger processing (off/on).

Arguments:
  TableFile       Path to table data file.
  SqlDriver       Driver name for connecting to the database
  Source          Source Db: user/password@alias[:host]
  Destination     Destination Db: user/password@alias[:host]

