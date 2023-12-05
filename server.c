#include <errno.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define USERNAME_SIZE 32
#define POST_SIZE 64

typedef sem_t* semset_t;

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
semset_t* semset;
const char* keyfile;

void print_posts(int sig) {
    int i;

    printf("\n");

    if (sem_wait(semset[twitter->capacity]) == -1) {
        sys_err("Nie mozna zarezerwowac rozmiaru");
    }

    if (twitter->size == 0) {
        printf("Brak wpisow\n");
    } else {
        printf("_____________  Twitter 2.0:  _____________\n");
        for (i = 0; i < twitter->size; ++i) {
            if (sem_wait(semset[i]) == -1) {
                sys_err("Nie mozna zarezerwowac postu");
            }

            printf("%d. [%s]: %s [Polubienia: %d]\n", i + 1,
                   twitter->posts[i].username, twitter->posts[i].post,
                   twitter->posts[i].likes);

            if (sem_post(semset[i]) == -1) {
                sys_err("Nie mozna zwolnic rezerwacji postu");
            }
        }
    }

    if (sem_post(semset[twitter->capacity]) == -1) {
        sys_err("Nie mozna zwolnic rezerwacji rozmiaru");
    }
}

int semset_close(semset_t* semset, int nsem) {
    // TODO: on error try to close rest of semaphores
    // +1 because on this pos we hold size
    int i;
    for (i = 0; i < nsem + 1; ++i) {
        if (sem_close(semset[i]) == -1) {
            return -1;
        }
    }

    return 0;
}

int semset_unlink(const char* key, int nsem) {
    int i;
    int is_success;

    char name[NAME_MAX] = "/";
    strcat(name, key);

    char* semnum = name + strlen(name);

    is_success = 0;
    for (i = 0; i < nsem; ++i) {
        sprintf(semnum, "%d", i);

        if (sem_unlink(name) == -1) {
            is_success = -1;
            perror(name);
            /*remove rest of files thus not fatal */
        }
    }

    // INFO: special semaphore to reserve size
    strcpy(semnum, "size");
    if (sem_unlink(name) == -1) {
        is_success = -1;
        perror(name);
    }

    return is_success;
}

void cleanup(int sig) {
    char shmname[NAME_MAX] = "/";
    strncat(shmname, keyfile, NAME_MAX - 2);  // null and leading slash

    printf("[SERWER]: dostalem SIGINT => koncze i sprzatam...\n");

    printf("semafory: odlaczanie: %s, ",
           (semset_close(semset, twitter->capacity) == 0) ? "OK"
                                                          : strerror(errno));
    free(semset);
    printf("Zwolniono pamiec, ");
    printf("usuwanie semafor: %s\n",
           (semset_unlink(keyfile, twitter->capacity) == 0) ? "OK"
                                                            : strerror(errno));

    printf(
        "shared memory: odlaczanie: %s, usuniecie: %s\n",
        (munmap(twitter, sizeof(*twitter) +
                             twitter->capacity * sizeof(*twitter->posts)) == 0)
            ? "OK"
            : strerror(errno),
        (shm_unlink(shmname) == 0) ? "OK" : strerror(errno));

    exit(EXIT_SUCCESS);
}

// WARNING: returned semaphore set should be freed
semset_t* create_semset(int nsem, const char* key) {
    semset_t* semset;
    int i;

    if ((semset = calloc(nsem + 1, sizeof(sem_t*))) == NULL) {
        sys_err("Can't alloc memory for semaphore set");
    }

    char name[NAME_MAX] = "/";
    strcat(name, key);
    char* semnum = name + strlen(name);

    for (i = 0; i < nsem; ++i) {
        sprintf(semnum, "%d", i);
        semset[i] = sem_open(name, O_CREAT, 0666, 1);
        if (semset[i] == SEM_FAILED) {
            sys_err(name);
        }
    }

    // INFO: special semaphore to reserve size
    strcpy(semnum, "size");
    semset[nsem] = sem_open(name, O_CREAT, 0666, 1);
    if (semset[nsem] == SEM_FAILED) {
        sys_err(name);
    }

    return semset;
}

int create_shm(int nposts, const char* key) {
    int shmfd;
    char name[NAME_MAX] = "/";

    printf("[SERWER]: tworze klucz na podstawie pliku %s... ", key);
    strncat(name, key, NAME_MAX - 2);  // null and leading slash
    printf("OK (klucz: %s)\n", name);

    printf(
        "[SERWER]: Tworze segment pamieci wspoldzielonej na %d wpisow po "
        "%lub... ",
        nposts, sizeof(*twitter->posts));

    shmfd = shm_open(name, O_RDWR | O_CREAT, 0666);
    if (shmfd == -1) {
        sys_err("Nie udalo sie utworzyc pamieci dzielonej dla podanego klucza");
    }

    // set size of shared memory object
    if (ftruncate(shmfd, sizeof(*twitter) + nposts * sizeof(*twitter->posts)) ==
        -1) {
        sys_err("Nie mozna ustawic rozmiaru pamieci wspoldzielonej");
    }

    return shmfd;
}

void print_shminfo(int shmfd) {
    struct stat shminfo;

    /* zaladuj informacje a segmencie pamieci dzielonej */
    if (fstat(shmfd, &shminfo) == -1) {
        sys_err_with_cleanup(
            "Nie mozna odczytac informacji o pamieci wspoldzielonej");
    }
    printf("OK (id: %d, rozmiar: %ld)\n", shmid, shminfo.st_size);
}

struct Twitter* init_twitter(int shmid, int maxposts) {
    struct Twitter* t;
    printf("[SERWER]: dolaczam pamiec wspolna... ");
    t = mmap(NULL, sizeof(*twitter) + maxposts * sizeof(*twitter->posts),
             PROT_READ | PROT_WRITE, MAP_SHARED, shmid, 0);
    if (t == MAP_FAILED) {
        sys_err_with_cleanup("Nie mozna dolaczyc segmentu pamieci");
    }

    t->capacity = maxposts; /* set twitter capacity */
    t->size = 0;            /* no posts by default */

    printf("OK (adres: %p)\n", (void*)t);

    return t;
}

int main(int argc, char* argv[]) {
    int nposts;

    signal(SIGTSTP, print_posts);
    signal(SIGINT, cleanup);

    if (argc != 3) {
        fprintf(stderr, "uzycie: %s <klucz> <liczba wpisow>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    errno = 0; /* recommendation from manual of strtol to check for errors */
    nposts = strtol(argv[2], NULL, 10);
    if (errno != 0) {
        sys_err("Drugim argumentem powinnna byc liczba calkowita");
    }
    keyfile = argv[1];

    printf("[SERWER]: Twitter 2.0 (Wersja A)\n");
    shmid = create_shm(nposts, keyfile);
    print_shminfo(shmid);

    semset = create_semset(nposts, keyfile);

    twitter = init_twitter(shmid, nposts);
    close(shmid);  // after mmap no needed

    printf("[SERWER]: nacisnij Ctrl^Z by wyswietlic stan serwisu\n");
    printf("[SERWER]: nacisnij Ctrl^C by zakonczyc program\n");

    for (;;) {
        /* wait for signals */
    }

    return 0;
}
