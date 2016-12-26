#include <stdio.h>
#include <memory.h>

void handle_error (int status, int errnum, const char *format) {
#if __APPLE__
    printf("Error: %s, Code: %d", strerror(errnum), errnum);
    perror(format);
#elif __unix__
    error(status, errnum, format);
#else
    // Behavior not defined
#endif
}