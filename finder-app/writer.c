#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>


int main(int argc, char *argv[])
{
    openlog(NULL, 0, LOG_USER);

    if (argc < 3) {
        syslog(LOG_ERR, "Two arguments required: <writefile> <writestr>");
        fprintf(stderr, "Error: two arguments required: <writefile> <writestr>\n");
        closelog();
        return 1;
    }

    const char *writefile = argv[1];
    const char *writestr  = argv[2];

    FILE* f = fopen(writefile, "w");
    if (f == NULL) {
        int err = errno;
        fprintf(stderr, "Error: could not create file '%s': %s\n", writefile, strerror(err));
        syslog(LOG_ERR, "Could not create file '%s': %s", writefile, strerror(err));
        closelog();
        return 1;
    }

    size_t len = strlen(writestr);
    size_t written = fwrite(writestr, 1, len, f);
    if (written != len) {
        int err = errno;
        fprintf(stderr, "Error: Failed to write content to '%s': %s\n", writefile, strerror(err));
        syslog(LOG_ERR, "Failed to write content to '%s': %s", writefile, strerror(err));
        fclose(f);
        closelog();
        return 1;
    }

    fclose(f);
    closelog();
    return 0;
}