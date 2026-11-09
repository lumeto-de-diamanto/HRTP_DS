#include <stdio.h>
#include <stdlib.h>

// Taken from https://blocksds.skylyrac.net/tutorial/intermediate/nitrofs/

bool file_load(const char *path, void **buffer, size_t *size)
{
    // Open the file in read binary mode
    FILE *f = fopen(path, "rb");
    if (f == NULL)
    {
        perror("fopen");
        return false;
    }

    // Move read cursor to the end of the file
    int ret = fseek(f, 0, SEEK_END);
    if (ret != 0)
    {
        perror("fseek");
        return false;
    }

    // Check position of the cursor (we're at the end, so this is the size)
    *size = ftell(f);
    if (*size == 0)
    {
        printf("Size is 0!");
        fclose(f);
        return false;
    }

    // Move cursor to the start of the file again
    rewind(f);

    // Allocate buffer to hold data
    *buffer = malloc(*size);
    if (*buffer == NULL)
    {
        printf("Not enought memory to load %s!", path);
        fclose(f);
        return false;
    }

    // Read all data into the buffer
    if (fread(*buffer, *size, 1, f) != 1)
    {
        perror("fread");
        fclose(f);
        free(*buffer);
        return false;
    }

    // Close file
    ret = fclose(f);
    if (ret != 0)
    {
        perror("fclose");
        free(*buffer);
        return false;
    }

    return true;
}