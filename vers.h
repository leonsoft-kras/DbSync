//
#define FVERSION_MAJOR              2
#define FVERSION_MINOR              3

#define FVERSION_MINOR2             0

#ifndef MACROSTRING
#define MACROSTRING
#define STRINGIZE2(s) #s
#define STRINGIZE(s) STRINGIZE2(s)
#endif

#define VER_FILE_VERSION            FVERSION_MAJOR,FVERSION_MINOR,FVERSION_MINOR2,0
#define VER_FILE_VERSION_STR        STRINGIZE(FVERSION_MAJOR)        \
                                    "." STRINGIZE(FVERSION_MINOR)    \
									"." STRINGIZE(FVERSION_MINOR2)

