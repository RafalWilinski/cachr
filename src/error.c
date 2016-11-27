#include <stdio.h>

void handle_error (int status, int errnum, const char *format) {
#if __APPLE__
    perror(format);
#elif __unix__
    error(status, errnum, format);
#else
    // Behavior not defined
#endif
}