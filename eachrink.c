/*
 * Disciplina: ACH2044 - Sistemas Operacionais
 * Exercício-Programa 2: Compactador EACHrink (EACH-USP)
 * Autor: Nícolas Josué dos Santos
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>

#define SIMBOLO_INICIAL 0x80
#define SIMBOLO_FINAL   0xFF
#define MAX_PARES       (SIMBOLO_FINAL - SIMBOLO_INICIAL)

typedef struct {
    uint8_t b1;
    uint8_t b2;
} Par;

typedef struct {
    int id;
    int total_threads;
    uint8_t *buffer;          
    size_t tam_total;         
    unsigned int *hist_local; 
} ThreadArgs;

Par tabela_pares[MAX_PARES];
int qtd_pares = 0;

pthread_barrier_t barreira_inicio;
pthread_barrier_t barreira_fim;
volatile int parar_execucao = 0;

void* worker(void* arg) {
    ThreadArgs* t = (ThreadArgs*)arg;
    
    while (1) {
        int b_ret = pthread_barrier_wait(&barreira_inicio);
        if (b_ret != 0 && b_ret != PTHREAD_BARRIER_SERIAL_THREAD) {}

        if (parar_execucao) break;

        memset(t->hist_local, 0, 65536 * sizeof(unsigned int));

        size_t inicio = (t->id * t->tam_total) / (size_t)t->total_threads;
        size_t fim = ((t->id + 1) * t->tam_total) / (size_t)t->total_threads;
        
        size_t limite = (t->id == t->total_threads - 1) ? 
                        ((t->tam_total > 0) ? t->tam_total - 1 : 0) : fim;

        if (inicio < limite) {
            for (size_t i = inicio; i < limite; i++) {
                int idx = (int)t->buffer[i] * 256 + (int)t->buffer[i+1];
                t->hist_local[idx]++;
            }
        }

        pthread_barrier_wait(&barreira_fim);
    }
    return NULL;
}

void compactar(const char* arq_entrada, const char* arq_saida, int num_threads) {
    int fd_in = open(arq_entrada, O_RDONLY);
    if (fd_in < 0) { perror("Erro ao abrir entrada"); exit(1); }
    
    struct stat sb;
    if (fstat(fd_in, &sb) == -1) { perror("fstat"); exit(1); }
    size_t tam_orig = (size_t)sb.st_size;
    
    if (tam_orig < 2) {
        FILE* f = fopen(arq_saida, "wb");
        if (!f) { perror("fopen saida"); exit(1); }
        fputc(SIMBOLO_FINAL, f);
        if (tam_orig == 1) {
            uint8_t b;
            if (read(fd_in, &b, 1) == 1) {
                if(fwrite(&b, 1, 1, f) != 1) {};
            }
        }
        fclose(f); close(fd_in);
        return; 
    }

    uint8_t *dados = mmap(NULL, tam_orig, PROT_READ, MAP_PRIVATE, fd_in, 0);
    if (dados == MAP_FAILED) { perror("mmap"); exit(1); }
    
    uint8_t *buf_atual = malloc(tam_orig);
    uint8_t *buf_prox = malloc(tam_orig);
    if (!buf_atual || !buf_prox) { fprintf(stderr, "Erro malloc\n"); exit(1); }

    memcpy(buf_atual, dados, tam_orig);
    size_t tam_atual = tam_orig;

    if (num_threads <= 0) num_threads = get_nprocs();

    pthread_t *threads = malloc((size_t)num_threads * sizeof(pthread_t));
    ThreadArgs *args = malloc((size_t)num_threads * sizeof(ThreadArgs));
    unsigned int *hists = malloc((size_t)num_threads * 65536 * sizeof(unsigned int));
    
    if (!threads || !args || !hists) exit(1);

    pthread_barrier_init(&barreira_inicio, NULL, (unsigned)(num_threads + 1));
    pthread_barrier_init(&barreira_fim, NULL, (unsigned)(num_threads + 1));

    for (int i = 0; i < num_threads; i++) {
        args[i].id = i;
        args[i].total_threads = num_threads;
        args[i].hist_local = &hists[i * 65536];
        args[i].buffer = buf_atual; 
        pthread_create(&threads[i], NULL, worker, &args[i]);
    }

    uint8_t prox_simbolo = SIMBOLO_INICIAL;

    while (prox_simbolo < SIMBOLO_FINAL) {
        
        for(int i=0; i<num_threads; i++) {
            args[i].buffer = buf_atual;
            args[i].tam_total = tam_atual;
        }

        pthread_barrier_wait(&barreira_inicio); 
        pthread_barrier_wait(&barreira_fim);    

        unsigned int max = 0;
        int b1 = -1, b2 = -1;

        for (int r = 0; r < 256; r++) {
            for (int c = 0; c < 256; c++) {
                unsigned int tot = 0;
                for (int t = 0; t < num_threads; t++) {
                    tot += hists[t * 65536 + r * 256 + c];
                }
                
                if (tot > max) { 
                    max = tot;
                    b1 = r; b2 = c;
                }
            }
        }

        if (max <= 1) break;

        tabela_pares[qtd_pares].b1 = (uint8_t)b1;
        tabela_pares[qtd_pares].b2 = (uint8_t)b2;

        size_t w = 0;
        for (size_t i = 0; i < tam_atual; i++) {
            if (i < tam_atual - 1 && buf_atual[i] == b1 && buf_atual[i+1] == b2) {
                buf_prox[w++] = prox_simbolo;
                i++; 
            } else {
                buf_prox[w++] = buf_atual[i];
            }
        }

        uint8_t *tmp = buf_atual;
        buf_atual = buf_prox;
        buf_prox = tmp;
        tam_atual = w;

        prox_simbolo++;
        qtd_pares++;
    }

    parar_execucao = 1;
    pthread_barrier_wait(&barreira_inicio);
    
    for (int i = 0; i < num_threads; i++) pthread_join(threads[i], NULL);
    
    pthread_barrier_destroy(&barreira_inicio);
    pthread_barrier_destroy(&barreira_fim);

    FILE *fo = fopen(arq_saida, "wb");
    if (!fo) { perror("fopen saida"); exit(1); }
    
    for (int i = 0; i < qtd_pares; i++) {
        fputc(tabela_pares[i].b1, fo);
        fputc(tabela_pares[i].b2, fo);
    }
    fputc(SIMBOLO_FINAL, fo);
    if (tam_atual > 0) fwrite(buf_atual, 1, tam_atual, fo);
    fclose(fo);

    munmap(dados, tam_orig);
    close(fd_in);
    free(buf_atual); free(buf_prox);
    free(threads); free(args); free(hists);
}

void descompactar(const char* arq_entrada, const char* arq_saida) {
    int fd = open(arq_entrada, O_RDONLY);
    if (fd < 0) { perror("open"); exit(1); }
    
    struct stat sb;
    if (fstat(fd, &sb) == -1) { perror("fstat"); exit(1); }
    size_t tam_arq = (size_t)sb.st_size;
    
    uint8_t *dados = mmap(NULL, tam_arq, PROT_READ, MAP_PRIVATE, fd, 0);
    if (dados == MAP_FAILED) { perror("mmap"); exit(1); }

    size_t cursor = 0;
    qtd_pares = 0;
    while (cursor < tam_arq) {
        if (dados[cursor] == SIMBOLO_FINAL) { cursor++; break; }
        if (cursor + 1 >= tam_arq) break; 
        tabela_pares[qtd_pares].b1 = dados[cursor];
        tabela_pares[qtd_pares].b2 = dados[cursor+1];
        qtd_pares++;
        cursor += 2;
    }

    size_t tam_pl = tam_arq - cursor;
    size_t cap = tam_pl * 2 + 8192;
    
    uint8_t *b_in = malloc(cap);
    uint8_t *b_out = malloc(cap);
    if (!b_in || !b_out) { fprintf(stderr, "Malloc fail\n"); exit(1); }
    
    memcpy(b_in, dados + cursor, tam_pl);
    size_t tam_atual = tam_pl;

    for (int i = qtd_pares - 1; i >= 0; i--) {
        uint8_t sym = (uint8_t)(SIMBOLO_INICIAL + i);
        Par p = tabela_pares[i];
        
        if (tam_atual * 2 > cap) {
            cap = tam_atual * 2 + 16384;
            uint8_t *n_in = realloc(b_in, cap);
            uint8_t *n_out = realloc(b_out, cap);
            if (!n_in || !n_out) { fprintf(stderr, "Realloc fail\n"); exit(1); }
            b_in = n_in;
            b_out = n_out;
        }

        size_t w = 0;
        for (size_t j = 0; j < tam_atual; j++) {
            if (b_in[j] == sym) {
                b_out[w++] = p.b1;
                b_out[w++] = p.b2;
            } else {
                b_out[w++] = b_in[j];
            }
        }

        uint8_t *tmp = b_in;
        b_in = b_out;
        b_out = tmp;
        tam_atual = w;
    }

    FILE *f = fopen(arq_saida, "wb");
    if (!f) exit(1);
    fwrite(b_in, 1, tam_atual, f);
    fclose(f);
    
    free(b_in); free(b_out);
    munmap(dados, tam_arq);
    close(fd);
}

int main(int argc, char *argv[]) {
    if (argc < 4) return 1;
    
    int t = (argc > 4) ? atoi(argv[4]) : 0;
    char cmd = argv[1][0];
    
    if (cmd == 'c') compactar(argv[2], argv[3], t);
    else if (cmd == 'd') descompactar(argv[2], argv[3]);
    else return 1;

    return 0;
}