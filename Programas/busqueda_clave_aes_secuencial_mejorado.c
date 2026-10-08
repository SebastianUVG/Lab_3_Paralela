/*----------------------------------------------------------------------
 * UNIVERSIDAD DEL VALLE DE GUATEMALA
 * Curso:       CC3069 - Computacion Paralela y Distribuida
 * Ejercicio:   Busqueda secuencial de una clave AES mediante fuerza bruta
 *              (version mejorada - Laboratorio 03, inciso 3b)
 *
 * Mejoras: rango y clave de prueba configurables, argumentos validados
 * y reporte del espacio explorado. El prefijo fijo es educativo y no
 * aumenta la seguridad. La clave por defecto queda al final del rango
 * para comparar tiempos con MPI usando el mismo problema.
 *----------------------------------------------------------------------*/

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <time.h>
#include <openssl/evp.h>
#include <openssl/err.h>
#include "aes_busqueda_opciones.h"

#define AES128_KEYSPACE_BITS 128
#define MESSAGE_LEN 16

/* Prefijo fijo conocido: no agrega entropia ni seguridad. */
static const unsigned char KEY_PREFIX[8] = {
    0xA5, 0x3C, 0x91, 0x7E, 0x2D, 0xF0, 0x64, 0xB8
};

static const unsigned char message[] = "Puedes lograrlo!";

_Static_assert(
    sizeof(message) - 1 == MESSAGE_LEN,
    "El mensaje debe tener exactamente 16 bytes."
);

static void fail(const char *description)
{
    fprintf(stderr, "%s\n", description);
    ERR_print_errors_fp(stderr);
    exit(EXIT_FAILURE);
}

/* Construye la clave AES-128 con un prefijo fijo y la candidata. */
static void make_key(uint64_t candidate, unsigned char key[16])
{
    memcpy(key, KEY_PREFIX, 8);

    for (int i = 0; i < 8; i++) {
        key[15 - i] = (unsigned char)(
            (candidate >> (8 * i)) & UINT64_C(0xFF)
        );
    }
}

static void crypt_block(
    EVP_CIPHER_CTX *ctx,
    uint64_t candidate,
    const unsigned char *input,
    unsigned char *output,
    int encrypt)
{
    unsigned char key[16];
    int written = 0;
    int final_written = 0;

    make_key(candidate, key);

    if (EVP_CipherInit_ex(
            ctx, EVP_aes_128_ecb(), NULL,
            key, NULL, encrypt) != 1) {
        fail("Error al inicializar AES.");
    }

    if (EVP_CIPHER_CTX_set_padding(ctx, 0) != 1) {
        fail("Error al configurar el relleno.");
    }

    if (EVP_CipherUpdate(
            ctx, output, &written,
            input, MESSAGE_LEN) != 1) {
        fail("Error al procesar el bloque.");
    }

    if (EVP_CipherFinal_ex(
            ctx, output + written, &final_written) != 1) {
        fail("Error al finalizar la operacion AES.");
    }

    if (written + final_written != MESSAGE_LEN) {
        fail("Longitud inesperada del resultado.");
    }
}

/* Verificacion de texto plano conocido completo: se asume
 * que el atacante conoce el mensaje original completo de 16 bytes. */
static int keys_match(const unsigned char *decrypted,
                       const unsigned char *known_plaintext)
{
    return memcmp(decrypted, known_plaintext, MESSAGE_LEN) == 0;
}

static double get_time(void)
{
    struct timespec current;

    if (clock_gettime(CLOCK_MONOTONIC, &current) != 0) {
        perror("Error al consultar el reloj");
        exit(EXIT_FAILURE);
    }

    return (double)current.tv_sec +
           (double)current.tv_nsec / 1000000000.0;
}

int main(int argc, char *argv[])
{
    unsigned search_bits;
    uint64_t total_keys, secret_key;
    if (!read_options(argc, argv, &search_bits, &total_keys, &secret_key)) {
        fprintf(stderr, "Uso: %s [bits: 1-63] [clave_secreta_decimal]\n", argv[0]);
        return EXIT_FAILURE;
    }

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (ctx == NULL) {
        fail("No se pudo crear el contexto de OpenSSL.");
    }

    unsigned char cipher[MESSAGE_LEN + EVP_MAX_BLOCK_LENGTH] = {0};
    unsigned char plain[MESSAGE_LEN + EVP_MAX_BLOCK_LENGTH] = {0};

    crypt_block(ctx, secret_key, message, cipher, 1);

    uint64_t found = UINT64_MAX;
    double start = get_time();

    for (uint64_t key = 0; key < total_keys; key++) {
        crypt_block(ctx, key, cipher, plain, 0);

        if (keys_match(plain, message)) {
            found = key;
            break;
        }
    }

    double elapsed = get_time() - start;

    if (found != UINT64_MAX) {
        printf("Clave encontrada: %" PRIu64 "\n", found);
        printf("Mensaje: ");
        fwrite(plain, 1, MESSAGE_LEN, stdout);
        putchar('\n');
    } else {
        printf("No se encontro la clave.\n");
    }

    /* Mejora 3: transparencia sobre el espacio real explorado. */
    printf("Ejecucion: secuencial (mejorada)\n");
    printf("Bits de busqueda explorados: %u (2^%u = %" PRIu64 " candidatas)\n",
           search_bits, search_bits, total_keys);
    printf("Fraccion del espacio de AES-128 (2^128): 2^-%d\n",
           AES128_KEYSPACE_BITS - (int)search_bits);
    printf("Tiempo: %.6f segundos\n", elapsed);

    EVP_CIPHER_CTX_free(ctx);
    return EXIT_SUCCESS;
}
