/* The platform for the native tests: the log to stdout, nothing else. */
#include "platform.h"
#include <stdio.h>

void plat_log(const char *msg) { printf("%s\n", msg); }
