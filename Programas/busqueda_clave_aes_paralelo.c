/*----------------------------------------------------------------------
 * UNIVERSIDAD DEL VALLE DE GUATEMALA
 * Curso:       CC3069 - Computacion Paralela y Distribuida
 * Ejercicio:   Busqueda paralela (Open MPI) de una clave AES
 *              Laboratorio 03, inciso 4
 *
 * Diseno:
 *   - El proceso 0 cifra el mensaje con la clave de prueba y distribuye el
 *     criptograma a todos los procesos (MPI_Bcast).
 *   - El rango [0, TOTAL_KEYS) se reparte en bloques contiguos y
 *     balanceados: cada proceso recibe 'base' o 'base + 1' candidatas,
 *     sin omitir ni repetir ninguna.
 *   - Cada proceso busca por tandas de CHECK_INTERVAL candidatas.
 *   - MPI_Allreduce comunica la clave encontrada y si quedan candidatas.
 *     Los procesos con rangos agotados siguen participando hasta terminar.
 *   - El tiempo se mide con MPI_Wtime() y se reporta el maximo entre
 *     procesos (MPI_Reduce con MPI_MAX) en el proceso 0.
 *----------------------------------------------------------------------*/

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <mpi.h>
#include <openssl/evp.h>
#include <openssl/err.h>
#include "aes_busqueda_opciones.h"

#define AES128_KEYSPACE_BITS 128
#define MESSAGE_LEN 16
#define CHECK_INTERVAL 2048

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
    MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
}

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

    if (EVP_CipherInit_ex(ctx, EVP_aes_128_ecb(), NULL, key, NULL, encrypt) != 1)
        fail("Error al inicializar AES.");

    if (EVP_CIPHER_CTX_set_padding(ctx, 0) != 1)
        fail("Error al configurar el relleno.");

    if (EVP_CipherUpdate(ctx, output, &written, input, MESSAGE_LEN) != 1)
        fail("Error al procesar el bloque.");

    if (EVP_CipherFinal_ex(ctx, output + written, &final_written) != 1)
        fail("Error al finalizar la operacion AES.");

    if (written + final_written != MESSAGE_LEN)
        fail("Longitud inesperada del resultado.");
}

static int keys_match(const unsigned char *decrypted,
                       const unsigned char *known_plaintext)
{
    return memcmp(decrypted, known_plaintext, MESSAGE_LEN) == 0;
}

/* Reparte [0, total_keys) en 'size' bloques balanceados y contiguos.
 * Devuelve el inicio (incl.) y fin (excl.) del bloque para 'rank'. */
static void compute_range(uint64_t total_keys, int size, int rank,
                           uint64_t *start, uint64_t *end)
{
    uint64_t base = total_keys / (uint64_t)size;
    uint64_t rem  = total_keys % (uint64_t)size;

    if ((uint64_t)rank < rem) {
        *start = (uint64_t)rank * (base + 1);
        *end   = *start + base + 1;
    } else {
        *start = rem * (base + 1) + ((uint64_t)rank - rem) * base;
        *end   = *start + base;
    }
}

int main(int argc, char *argv[])
{
    MPI_Init(&argc, &argv);

    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    unsigned search_bits;
    uint64_t total_keys, secret_key;
    if (!read_options(argc, argv, &search_bits, &total_keys, &secret_key)) {
        if (rank == 0) fprintf(stderr, "Uso: %s [bits: 1-63] [clave_secreta_decimal]\n", argv[0]);
        MPI_Finalize();
        return EXIT_FAILURE;
    }

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (ctx == NULL) fail("No se pudo crear el contexto de OpenSSL.");

    unsigned char cipher[MESSAGE_LEN + EVP_MAX_BLOCK_LENGTH] = {0};
    unsigned char plain[MESSAGE_LEN + EVP_MAX_BLOCK_LENGTH] = {0};

    /* El proceso 0 prepara el ejercicio y comparte el bloque cifrado.
     * La clave es configurable en todos los procesos para esta prueba;
     * no se utiliza para decidir si una candidata coincide. */
    if (rank == 0) {
        crypt_block(ctx, secret_key, message, cipher, 1);
    }
    MPI_Bcast(cipher, MESSAGE_LEN + EVP_MAX_BLOCK_LENGTH, MPI_UNSIGNED_CHAR,
               0, MPI_COMM_WORLD);

    uint64_t my_start, my_end;
    compute_range(total_keys, size, rank, &my_start, &my_end);

    MPI_Barrier(MPI_COMM_WORLD);
    double start_time = MPI_Wtime();

    uint64_t local_found = UINT64_MAX;
    uint64_t global_found = UINT64_MAX;
    uint64_t next_key = my_start;
    int any_active = 1;

    /* Todos participan en cada tanda, incluso con un rango vacio.
     * Cada candidata pertenece a un solo rango y se prueba una vez. */
    while (any_active && global_found == UINT64_MAX) {
        for (unsigned step = 0; step < CHECK_INTERVAL && next_key < my_end;
             ++step, ++next_key) {
            crypt_block(ctx, next_key, cipher, plain, 0);
            if (keys_match(plain, message)) {
                local_found = next_key;
                break;
            }
        }
        MPI_Allreduce(&local_found, &global_found, 1, MPI_UINT64_T,
                      MPI_MIN, MPI_COMM_WORLD);
        int local_active = next_key < my_end;
        MPI_Allreduce(&local_active, &any_active, 1, MPI_INT,
                      MPI_MAX, MPI_COMM_WORLD);
    }

    double local_elapsed = MPI_Wtime() - start_time;
    double max_elapsed = 0.0;
    MPI_Reduce(&local_elapsed, &max_elapsed, 1, MPI_DOUBLE,
               MPI_MAX, 0, MPI_COMM_WORLD);

    if (rank == 0) {
        if (global_found != UINT64_MAX) {
            /* Recalcula el mensaje descifrado con la clave encontrada
             * para mostrarlo, igual que la version secuencial. */
            crypt_block(ctx, global_found, cipher, plain, 0);
            printf("Clave encontrada: %" PRIu64 "\n", global_found);
            printf("Mensaje: ");
            fwrite(plain, 1, MESSAGE_LEN, stdout);
            putchar('\n');
        } else {
            printf("No se encontro la clave.\n");
        }

        printf("Ejecucion: paralela (Open MPI), %d procesos\n", size);
        printf("Bits de busqueda explorados: %u (2^%u = %" PRIu64 " candidatas)\n",
               search_bits, search_bits, total_keys);
        printf("Fraccion del espacio de AES-128 (2^128): 2^-%d\n",
               AES128_KEYSPACE_BITS - (int)search_bits);
        printf("Tiempo: %.6f segundos\n", max_elapsed);
    }

    EVP_CIPHER_CTX_free(ctx);
    MPI_Finalize();
    return EXIT_SUCCESS;
}
