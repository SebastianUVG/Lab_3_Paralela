#ifndef AES_BUSQUEDA_OPCIONES_H
#define AES_BUSQUEDA_OPCIONES_H

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>

/* Solo se aceptan numeros decimales completos, sin signo ni espacios. */
static int parse_decimal(const char *text, uint64_t *value)
{
    if (*text == '\0') return 0;
    for (const char *p = text; *p; ++p) {
        if (*p < '0' || *p > '9') return 0;
    }
    errno = 0;
    char *end = NULL;
    unsigned long long parsed = strtoull(text, &end, 10);
    if (errno == ERANGE || *end != '\0' || parsed > UINT64_MAX) return 0;
    *value = (uint64_t)parsed;
    return 1;
}

static int read_options(int argc, char **argv, unsigned *bits,
                        uint64_t *total, uint64_t *secret)
{
    uint64_t parsed_bits = 20;
    if (argc > 3 || (argc > 1 && !parse_decimal(argv[1], &parsed_bits)) ||
        parsed_bits < 1 || parsed_bits > 63) return 0;
    *bits = (unsigned)parsed_bits;
    *total = UINT64_C(1) << *bits;
    *secret = *total > 37 ? *total - 37 : UINT64_C(12345) % *total;
    if (argc > 2 && !parse_decimal(argv[2], secret)) return 0;
    return 1;
}
#endif
