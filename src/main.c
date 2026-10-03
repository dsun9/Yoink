#include <stdio.h>
#include <string.h>
#include <stdlib.h>
int pp_overlay_probe(const char *mode, unsigned duration_ms);
int main(int argc, char **argv) {
    if ((argc == 3 || argc == 5) && !strcmp(argv[1], "--overlay-probe")) {
        unsigned duration = 0;
        if (argc == 5) {
            if (strcmp(argv[3], "--duration-ms")) return 2;
            char *end; unsigned long parsed = strtoul(argv[4], &end, 10);
            if (*end || !parsed || parsed > 3600000) return 2;
            duration = (unsigned)parsed;
        }
        return pp_overlay_probe(argv[2], duration);
    }
    puts("Usage: PointyPointer --overlay-probe ring|changing-alpha [--duration-ms N]");
    return argc == 1 ? 0 : 2;
}
