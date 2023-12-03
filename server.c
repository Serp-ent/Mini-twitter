#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/types.h>

#define USERNAME_SIZE 32
#define POST_SIZE 64

int* size;
int* capacity;
struct Record {
    char username[USERNAME_SIZE];
    char post[POST_SIZE];
    int likes;
}* records;

int shmid;
int semid;

void print_posts(int sig) {
    struct sembuf alloc_size = {*capacity, -1, 0};
    struct sembuf free_size = {*capacity, 1, 0};
    struct sembuf alloc_record = {0, -1, 0};
    struct sembuf free_record = {0, 1, 0};
    int i;

    printf("\n");

    if (semop(semid, &alloc_size, 1) == -1) {
        perror("Nie mozna zajac zasobu");
        return;
    }

    if (*size == 0) {
        printf("Brak wpisow\n");
    } else {
        printf("_____________  Twitter 2.0:  _____________\n");
        for (i = 0; i < *size; ++i) {
            alloc_record.sem_num = free_record.sem_num = i;

            if (semop(semid, &alloc_record, 1) == -1) {
                perror("Nie mozna zwolnic zasobu");
                return;
            }

            printf("%d. [%s]: %s [Polubienia: %d]\n", i + 1,
                   records[i].username, records[i].post, records[i].likes);

            if (semop(semid, &free_record, 1) == -1) {
                perror("Nie mozna zwolnic zasobu");
                return;
            }
        }
    }

    if (semop(semid, &free_size, 1) == -1) {
        perror("Nie mozna zwolnic zasobu");
        return;
    }
}

void cleanup(int sig) {
    printf("[SERWER]: dostalem SIGINT => koncze i sprzatam... ");
    printf("odlaczanie: %s, usuniecie: %s\n",
           (shmdt(size) == 0) ? "OK" : strerror(errno),
           (shmctl(shmid, IPC_RMID, 0) == 0) ? "OK" : strerror(errno));
    printf("usuwanie semafor: %s\n",
           (semctl(semid, IPC_RMID, 0) == 0) ? "OK" : strerror(errno));
    exit(0);
}

/* this structure was copied from semctl(2) manual */
union semun {
    int val;               /* Value for SETVAL */
    struct semid_ds* buf;  /* Buffer for IPC_STAT, IPC_SET */
    unsigned short* array; /* Array for GETALL, SETALL */
    struct seminfo* __buf; /* Buffer for IPC_INFO
                              (Linux-specific) */
};

int main(int argc, char* argv[]) {
    key_t shmkey;
    key_t semkey;
    int n;
    struct shmid_ds shmds;
    union semun semval;
    int i;

    signal(SIGTSTP, print_posts);
    signal(SIGINT, cleanup);

    if (argc != 3) {
        fprintf(stderr, "uzycie: %s, <klucz> <liczba wpisow>\n", argv[0]);
        exit(1);
    }

    errno = 0; /* recommendation from manual of strtol to check for errors */
    n = strtol(argv[2], NULL, 10);
    if (errno != 0) {
        perror("Drugim argumentem powinnna byc liczba calkowita");
        exit(1);
    }

    printf("[SERWER]: Twitter 2.0 (Wersja A)\n");
    printf("[SERWER]: tworze klucz na podstawie pliku %s... ", argv[1]);
    shmkey = ftok(argv[1], 1);
    if (shmkey == -1) {
        perror("Nie udalo sie wygenerowac klucza");
        exit(1);
    }
    printf("OK (klucz: %d)\n", shmkey);

    semkey = ftok(argv[1], 2);
    if (semkey == -1) {
        perror("Nie udalo sie wygenerowac klucza");
        cleanup(0);
    }

    /* jeden wiecej dla kontrolowania rozmiaru i pojemnosci */
    if ((semid = semget(semkey, n + 1, 0666 | IPC_CREAT)) == -1) {
        perror("Nie mozna utworzyc zbioru semafor");
        cleanup(0);
    }

    semval.val = 1;

    for (i = 0; i < n + 1; ++i) {  /* + 1 zeby zainicjalizowac tez rozmiar */
        if (semctl(semid, i, SETVAL, semval) == -1) {
            perror("Nie mozna zainicjalizowac semafory");
            cleanup(0);
        }
    }

    printf("[SERWER]: Tworze segment pamieci wspolnej na %d wpisow po %lub... ",
           n, sizeof(struct Record));
    shmid = shmget(shmkey, sizeof(int) * 2 + sizeof(*records) * n,
                   IPC_CREAT | 0666);
    if (shmid == -1) {
        perror("Nie udalo sie utworzyc segment pamieci dzielonej");
        exit(1);
    }

    /* zaladuj informacje a segmencie pamieci dzielonej */
    if (shmctl(shmid, IPC_STAT, &shmds) == -1) {
        perror("Nie mozna odczytac informacji o pamieci wspoldzielonej");
        cleanup(0);
    }
    printf("OK (id: %d, rozmiar: %ld)\n", shmid, shmds.shm_segsz);

    printf("[SERWER]: dolaczam pamiec wspolna... ");
    size = shmat(shmid, NULL, 0);
    capacity = size + sizeof(int);
    records = (void*)capacity + sizeof(int);

    if (records == (void*)-1) {
        perror("Nie mozna dolaczyc segmentu pamieci");
        cleanup(0);
    }
    *capacity = n;  /* set twitter capacity */

    printf("OK (adres: %lu)\n", (long)records);
    printf("[SERWER]: nacisnij Ctrl^Z by wyswietlic stan serwisu\n");
    printf("[SERWER]: nacisnij Ctrl^C by zakonczyc program\n");

    while (1) {
        /* wait for signals */
    }

    return 0;
}
