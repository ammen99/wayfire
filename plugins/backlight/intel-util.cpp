#include <stdio.h>
#include <assert.h>
#include <stdlib.h>

static const char *path = "/sys/class/backlight/intel_backlight/brightness";
static const char *max_path = "/sys/class/backlight/intel_backlight/max_brightness";

int main(int argc, char **argv)
{
    assert(argc > 1);

    int max_br;

    FILE *file = fopen(max_path, "r");
    if (file == NULL) {
        fprintf(stderr, "Failed to query max brightness.\n");
        return -1;
    }

    fscanf(file, "%d", &max_br);
    fclose(file);

    if (max_br < 0) {
        fprintf(stderr, "Invalid maximum brightness, possibly a bug in the driver?\n");
        return -1;
    }

    char *end;
    int value = strtol(argv[1], &end, 10);

    if (value < 0 || value > max_br) {
        fprintf(stderr, "Invalid argument %d. Possible range is 0-%d\n.", value, max_br);
        return -1;
    }

    file = fopen(path, "w");
    if (!file) {
        fprintf(stderr, "Failed to open file for writing.\n");
        return -1;
    }
    fprintf(file, "%d", value);
    fclose(file);

    return 0;
}
