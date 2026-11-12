# EACHrink — Parallel Byte Pair Encoding compressor

Multi-threaded file compressor written in C with **pthreads**, built for the Operating Systems course (ACH2044) at EACH-USP.
It implements **Byte Pair Encoding (BPE)**: at each round, the most frequent pair of adjacent bytes is replaced by a new unused symbol (`0x80`–`0xFE`), until no pair repeats or symbols run out.

## Highlights

- **Map-Reduce over threads:** the buffer is split into chunks; each thread counts byte pairs in a *private* 256×256 histogram (no mutexes, no atomics), and the main thread sums them to pick the winning pair.
- **Thread pool with barriers:** threads are created once and synchronized with `pthread_barrier_t` on every round, instead of being created and joined repeatedly.
- **Memory-mapped input (`mmap`)** and **double buffering** (ping-pong swap between two buffers) to avoid repeated I/O calls and `realloc`/`memmove`.
- **Deterministic output:** the compressed file is byte-for-byte identical with 1, 4 or 8 threads. Ties are broken by the numerically smallest pair, using unsigned bytes (`uint8_t`).

## Build and run

```bash
make                                  # gcc -Wall -Werror -pthread -O3 -std=c11
./eachrink c input.txt output.bin 4   # compress with 4 threads (default: all cores)
./eachrink d output.bin restored.txt  # decompress
make test                             # round-trip check
```

**Input:** ASCII text (bytes `0x00`–`0x7F`). Bytes `0x80`–`0xFF` are reserved for the new symbols, so UTF-8 text with accents is not supported.

**Output format:** the substitution table (2 bytes per pair, in order), followed by the end marker `0xFF`, followed by the compressed payload.

## Performance

Measured on an AMD Ryzen 7 5800H (8 cores / 16 threads), Ubuntu 22.04 on WSL 2, using a ~20 MB text file. Each time is the mean of 5 runs.

| Threads | Mean time (s) | Speedup |
|:-:|:-:|:-:|
| 1 | 0.514 | 1.00× |
| 2 | 0.419 | 1.23× |
| 4 | 0.382 | 1.35× |
| 8 | 0.381 | 1.35× |

Speedup levels off after 4 threads, as **Amdahl's Law** predicts. Only the pair counting runs in parallel. The pair substitution stays serial to keep the output deterministic. From these numbers, about 30% of the run time is parallel, which caps the speedup near 1.4×. For very small files (~1 KB), thread overhead outweighs the gain, and the serial version is faster.

## Testing

- **Round-trip:** `decompress(compress(file))` is byte-identical to the original (`diff`/`cmp`).
- **Determinism:** the MD5 of the compressed output is the same for 1, 4 and 8 threads.
