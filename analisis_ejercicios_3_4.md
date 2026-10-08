# Laboratorio 03 — Ejercicios 3 y 4

## 3. Problemas del programa secuencial

### a) Qué encontramos

**1. La clave siempre tiene ceros arriba (`make_key`)**
El candidato de 64 bits solo se pone en los 8 bytes de abajo de la clave de 16 bytes. Los 8 bytes de arriba siempre son `0x00`, y además solo probamos 2²⁰ claves. Una clave real de AES-128 no se vería así, entonces el ataque es mucho más fácil de lo que sería en la vida real.

**2. Modo ECB**
Como solo ciframos un bloque de 16 bytes, ECB no causa problemas aquí. Pero ECB no es seguro cuando hay varios bloques, y el verdadero problema del programa es que el espacio de búsqueda es muy pequeño, no el modo de cifrado.

**3. Comparamos con el mensaje completo**
El programa compara los 16 bytes del mensaje descifrado con el original. Eso quiere decir que el atacante ya sabe todo el texto, y eso casi nunca pasa. Normalmente solo se conoce una parte.

**4. Solo se prueba una parte mínima de las claves**
AES-128 tiene 2¹²⁸ claves y el programa solo prueba 2²⁰ (1,048,576), o sea 2⁻¹⁰⁸ del total. Está bien para un laboratorio, pero el programa no lo dice, y podría parecer que AES-128 se rompe fácil.

**5. La clave secreta está muy al inicio (afecta las mediciones)**
`SECRET_KEY = 12345` está casi al principio del rango (`0 .. 2²⁰-1`). En la versión paralela, el proceso 0 la encuentra casi de inmediato y el tiempo medido depende más de la comunicación que del trabajo repartido. Por eso el *speedup* sale mal.

### b) Mejoras que hicimos

| # | Mejora | Por qué | Propuesta por |
|---|--------|---------|---------------|
| 1 | Los bytes de arriba de la clave ahora llevan una "sal" fija distinta de cero | Así la clave no tiene la mitad en ceros y se parece más a una real | Cisco |
| 2 | `SECRET_KEY` se movió cerca del final del rango | Así se prueban casi todas las claves y el tiempo es más real | Ana Laura |
| 3 | El programa imprime cuántas claves prueba y qué fracción es de 2¹²⁸ | Para que se vea que es una prueba chiquita y no el AES-128 completo | Sebastian |
| 4 | Se hizo la función `keys_match()` para comparar el mensaje y se revisan los errores de `malloc` en la versión paralela | El código queda más ordenado y no se cae si falla la memoria | Ana Laura |
| 5 | Se agregó un *Makefile* y el número de claves se puede pasar por argumento | Así probamos con otros tamaños sin recompilar | Cisco |
| 6 | La versión paralela reparte las claves en bloques casi iguales y usa `MPI_Iprobe` para avisar cuando alguien la encuentra | Cuando uno la encuentra, los demás paran y no trabajan de más | Sebastian |

---

## 4. Versión paralela con Open MPI

### a) Cómo repartimos el trabajo

- El proceso 0 cifra el mensaje con `SECRET_KEY` y lo manda a todos con `MPI_Bcast`.
- El rango `[0, TOTAL_KEYS)` se divide en bloques seguidos. Cada proceso recibe `TOTAL_KEYS / n` claves, y los primeros `TOTAL_KEYS % n` reciben una más. Así todos tienen trabajo y no se repite ninguna clave.
- Cada proceso prueba su bloque. Cada cierto número de claves (`CHECK_INTERVAL`) revisa con `MPI_Iprobe` si otro ya encontró la clave.
- Quien la encuentra avisa a todos con `MPI_Isend` (tag `TAG_FOUND`).
- Al final todos hacen un `MPI_Allreduce` con `MPI_MIN` para tener la misma respuesta.
- El tiempo se mide con `MPI_Wtime()` desde después del `Bcast` hasta después del `Allreduce`. Se toma el máximo entre procesos con `MPI_Reduce` y lo imprime el proceso 0.

### b) Cómo correrlo

```bash
mpicc -std=c11 -O2 -Wall -Wextra busqueda_clave_aes_paralelo.c -o busqueda_clave_aes_paralelo -lcrypto
mpirun -np 2 ./busqueda_clave_aes_paralelo
mpirun -np 3 ./busqueda_clave_aes_paralelo
mpirun -np 4 ./busqueda_clave_aes_paralelo
```

Resultados (falta llenar con nuestras mediciones):

| Procesos (n) | Tiempo (s) | Speedup (T₁ / Tₙ) |
|---|---|---|
| 1 (secuencial mejorado) | | 1.00 |
| 2 | | |
| 3 | | |
| 4 | | |

`T₁` es el tiempo de la versión secuencial **mejorada**.
