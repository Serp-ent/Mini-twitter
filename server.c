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

struct Record {
    char username[USERNAME_SIZE];
    char post[POST_SIZE];
    int likes;
};

struct Twitter {
    int size;
    int capacity;
    struct Record posts[];
}* twitter;

/* this structure was copied from semctl(2) manual */
union semun {
    int val;               /* Value for SETVAL */
    struct semid_ds* buf;  /* Buffer for IPC_STAT, IPC_SET */
    unsigned short* array; /* Array for GETALL, SETALL */
    struct seminfo* __buf; /* Buffer for IPC_INFO
                              (Linux-specific) */
};

void cleanup(int sig);

void sys_err(const char* errmsg) {
    perror(errmsg);
    exit(EXIT_FAILURE);
}

void sys_err_with_cleanup(const char* errmsg) {
    perror(errmsg);
    cleanup(0);
}

int shmid;
int semid;

void print_posts(int sig) {
    struct sembuf alloc_size = {twitter->capacity, -1, 0};
    struct sembuf free_size = {twitter->capacity, 1, 0};
    struct sembuf alloc_record = {0, -1, 0};
    struct sembuf free_record = {0, 1, 0};
    int i;

    printf("\n");

    if (semop(semid, &alloc_size, 1) == -1) {
        perror("Nie mozna zajac zasobu");
        return;
    }

    if (twitter->size == 0) {
        printf("Brak wpisow\n");
    } else {
        printf("_____________  Twitter 2.0:  _____________\n");
        for (i = 0; i < twitter->size; ++i) {
            alloc_record.sem_num = free_record.sem_num = i;

            if (semop(semid, &alloc_record, 1) == -1) {
                perror("Nie mozna zwolnic zasobu");
                return;
            }

            printf("%d. [%s]: %s [Polubienia: %d]\n", i + 1,
                   twitter->posts[i].username, twitter->posts[i].post,
                   twitter->posts[i].likes);

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
           (shmdt(twitter) == 0) ? "OK" : strerror(errno),
           (shmctl(shmid, IPC_RMID, 0) == 0) ? "OK" : strerror(errno));
    printf("usuwanie semafor: %s\n",
           (semctl(semid, IPC_RMID, 0) == 0) ? "OK" : strerror(errno));
    exit(0);
}

int init_semset(int nsem, char* keyfile, int id) {
    key_t semkey;
    int semsetid;
    int i;

    union semun semval;
    semval.val = 1;

    if ((semkey = ftok(keyfile, id)) == -1) {
        sys_err_with_cleanup("Nie udalo sie wygenerowac klucza dla semafory");
    }

    /* jeden wiecej dla kontrolowania rozmiaru i pojemnosci */
    if ((semsetid = semget(semkey, nsem + 1, 0666 | IPC_CREAT)) == -1) {
        sys_err_with_cleanup("Nie mozna utworzyc zbioru semafor");
    }

    for (i = 0; i < nsem + 1; ++i) { /* + 1 zeby zainicjalizowac tez rozmiar */
        if (semctl(semsetid, i, SETVAL, semval) == -1) {
            sys_err_with_cleanup("Nie mozna zainicjalizowac semafory");
        }
    }

    return semsetid;
}

int main(int argc, char* argv[]) {
    key_t shmkey;
    int n;
    struct shmid_ds shmds;

    signal(SIGTSTP, print_posts);
    signal(SIGINT, cleanup);

    if (argc != 3) {
        fprintf(stderr, "uzycie: %s, <klucz> <liczba wpisow>\n", argv[0]);
        exit(1);
    }

    errno = 0; /* recommendation from manual of strtol to check for errors */
    n = strtol(argv[2], NULL, 10);
    if (errno != 0) {
        sys_err("Drugim argumentem powinnna byc liczba calkowita");
    }

    printf("[SERWER]: Twitter 2.0 (Wersja A)\n");
    printf("[SERWER]: tworze klucz na podstawie pliku %s... ", argv[1]);
    shmkey = ftok(argv[1], 1);
    if (shmkey == -1) {
        sys_err("Nie udalo sie wygenerowac klucza dla pamieci dzielonej");
    }
    printf("OK (klucz: %d)\n", shmkey);
    printf("[SERWER]: Tworze segment pamieci wspolnej na %d wpisow po %lub... ",
           n, sizeof(struct Record));
    shmid = shmget(shmkey, sizeof(*twitter) + n * sizeof(*twitter->posts),
                   IPC_CREAT | 0666);
    if (shmid == -1) {
        sys_err("Nie udalo sie utworzyc segment pamieci dzielonej");
    }

    /* zaladuj informacje a segmencie pamieci dzielonej */
    if (shmctl(shmid, IPC_STAT, &shmds) == -1) {
        sys_err_with_cleanup(
            "Nie mozna odczytac informacji o pamieci wspoldzielonej");
    }
    printf("OK (id: %d, rozmiar: %ld)\n", shmid, shmds.shm_segsz);

    semid = init_semset(n, argv[1], 2);

    printf("[SERWER]: dolaczam pamiec wspolna... ");
    twitter = shmat(shmid, NULL, 0);
    if (twitter == (void*)-1) {
        sys_err_with_cleanup("Nie mozna dolaczyc segmentu pamieci");
    }
    twitter->capacity = n; /* set twitter capacity */

    printf("OK (adres: %lu)\n", (long)twitter);
    printf("[SERWER]: nacisnij Ctrl^Z by wyswietlic stan serwisu\n");
    printf("[SERWER]: nacisnij Ctrl^C by zakonczyc program\n");

    for (;;) {
        /* wait for signals */
    }

    return 0;
}
