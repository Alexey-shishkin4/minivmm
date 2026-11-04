/*
 * Driver used when building fuzz targets without libFuzzer: feeds every file
 * given on the command line (or found directly inside a given directory) to
 * LLVMFuzzerTestOneInput once.
 */

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "fuzz.h"

static int run_file(const char *path)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        perror(path);
        return 1;
    }

    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size < 0) {
        perror(path);
        close(fd);
        return 1;
    }

    size_t size = (size_t)st.st_size;
    uint8_t *buf = malloc(size ? size : 1);
    if (!buf) {
        close(fd);
        return 1;
    }

    size_t done = 0;
    while (done < size) {
        ssize_t n = read(fd, buf + done, size - done);
        if (n <= 0) {
            break;
        }
        done += (size_t)n;
    }
    close(fd);

    LLVMFuzzerTestOneInput(buf, done);
    free(buf);
    return 0;
}

static int skip_hidden(const struct dirent *de)
{
    return de->d_name[0] != '.';
}

static int run_dir(const char *path, unsigned *count)
{
    struct dirent **entries;
    int n = scandir(path, &entries, skip_hidden, alphasort);
    if (n < 0) {
        perror(path);
        return 1;
    }

    int rc = 0;
    for (int i = 0; i < n; i++) {
        char child[4096];
        int len =
            snprintf(child, sizeof(child), "%s/%s", path, entries[i]->d_name);
        if (len > 0 && (size_t)len < sizeof(child)) {
            (*count)++;
            if (run_file(child) != 0) {
                rc = 1;
            }
        }
        free(entries[i]);
    }
    free((void *)entries);
    return rc;
}

int main(int argc, char **argv)
{
    unsigned count = 0;
    int rc = 0;

    for (int i = 1; i < argc; i++) {
        struct stat st;
        if (stat(argv[i], &st) != 0) {
            perror(argv[i]);
            rc = 1;
            continue;
        }
        int r;
        if (S_ISDIR(st.st_mode)) {
            r = run_dir(argv[i], &count);
        } else {
            count++;
            r = run_file(argv[i]);
        }
        if (r != 0) {
            rc = 1;
        }
    }

    printf("replayed %u input(s)\n", count);
    return rc;
}
