#include <stdlib.h>
#include <stdio.h>

#include <sys/stat.h>
#include <assert.h>

const char*
read_file(const char *filename)
{
    struct stat stat_buffer;
    char *buffer = NULL;
    FILE *f = NULL;
    size_t file_size, offset = 0;

    if (stat(filename, &stat_buffer) < 0)
        return NULL;

    assert(stat_buffer.st_size >= 0);

    file_size = (size_t)stat_buffer.st_size;

    // Do not read empty files
    if (!stat_buffer.st_size)
        goto error;

    buffer = (char*)malloc(file_size + 1);
    if (!buffer)
        goto error;

    f = fopen(filename, "r");
    if (!f)
        goto error;

    do {
        size_t bytes_read = fread(buffer + offset, 1, file_size - offset, f);
        if (bytes_read < file_size - offset && ferror(f))
            goto error;
        offset += bytes_read;
    } while (offset < file_size);

    buffer[file_size] = 0;

    return buffer;
error:
    if (buffer)
        free(buffer);
    if (f)
        fclose(f);
    return NULL;
}
