#include <errno.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

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

/* reads line without new line character to buff (at most size - 1 characters)
returns length of string that was read */
int readline(char* buff, int size) {
    fgets(buff, size, stdin);
    int len = strlen(buff);
    buff[--len] = 0; /* overwrite new line character */

    return len;
}

void sys_err(const char* errmsg) {
    perror(errmsg);
    exit(EXIT_FAILURE);
}

/* print usage and exit */
void usage(const char* progname) {
    fprintf(stderr, "usage: %s <key> <username>\n", progname);
    exit(1);
}

struct Twitter* connect_to_twitter(int shmid) {
    struct Twitter* t;

    t = mmap(NULL, sizeof(*twitter), PROT_READ | PROT_WRITE, MAP_SHARED, shmid,
             0);
    if (t == MAP_FAILED) {
        sys_err("Nie mozna dolaczyc segmentu pamieci");
    }

    int nposts = t->capacity;  // remap to adjust size
    t = mmap(NULL, sizeof(*twitter) + nposts * sizeof(*twitter->posts),
             PROT_READ | PROT_WRITE, MAP_SHARED, shmid, 0);
    if (t == MAP_FAILED) {
        sys_err("Nie mozna dolaczyc segmentu pamieci");
    }

    return t;
}

// WARNING: returned semaphore set should be freed
sem_t** connect_to_semset(int nsem, const char* key) {
    sem_t** semset;
    int i;

    if ((semset = calloc(nsem + 1, sizeof(sem_t*))) == NULL) {
        sys_err("Can't alloc memory for semaphore set");
    }

    char name[NAME_MAX] = "/";
    strcat(name, key);
    char* semnum = name + strlen(name);

    for (i = 0; i < nsem; ++i) {
        sprintf(semnum, "%d", i);
        semset[i] = sem_open(name, 0);
        if (semset[i] == SEM_FAILED) {
            sys_err(name);
        }
    }

    // INFO: special semaphore to reserve size
    strcpy(semnum, "size");
    semset[nsem] = sem_open(name, 0);
    if (semset[nsem] == SEM_FAILED) {
        sys_err(name);
    }

    return semset;
}

int semset_close(sem_t** semset, int nsem) {
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

int connect_to_shm(const char* key) {
    int shmfd;
    char name[NAME_MAX] = "/";

    strncat(name, key, NAME_MAX - 2);  // null and leading slash
    shmfd = shm_open(name, O_RDWR, 0);
    if (shmfd == -1) {
        sys_err("Nie udalo sie utworzyc pamieci dzielonej dla podanego klucza");
    }

    return shmfd;
}

int main(int argc, char* argv[]) {
    int shmid;
    sem_t** semset;
    int i;

    char action;

    struct Twitter* twitter;
    if (argc != 3) {
        usage(argv[0]);
    }

    shmid = connect_to_shm(argv[1]);
    twitter = connect_to_twitter(shmid);

    semset = connect_to_semset(twitter->capacity, argv[1]);

    printf("Twitter 2.0 wita! (WERSJA A)\n");

    if (sem_wait(semset[twitter->capacity]) == -1) {
        sys_err("Nie mozna zajac rozmiaru");
    }
    /* save to local variable */
    int local_capacity = twitter->capacity;
    int local_size = twitter->size;

    if (sem_post(semset[twitter->capacity]) == -1) {
        sys_err("Nie mozna zwolnic rozmiaru");
    }

    printf("[Wolnych %d wpisow (na %d)]\n", local_capacity - local_size,
           local_capacity);

    for (i = 0; i < local_size; ++i) {
        if (sem_wait(semset[i]) == -1) {
            sys_err("Nie mozna zarezerwowac postu");
        }

        printf("%d. %s [Autor: %s, Polubienia: %d]\n", i + 1,
               twitter->posts[i].post, twitter->posts[i].username,
               twitter->posts[i].likes);

        if (sem_post(semset[i]) == -1) {
            sys_err("Nie mozna oddac postu");
        }
    }

    printf("Podaj akcje (N)owy wpis, (L)ike\n> ");
    scanf(" %c", &action);
    while (getchar() != '\n') /* ignore rest of line */
        ;

    if (action == 'n' || action == 'N') {
        char buff[POST_SIZE];
        printf("Napisz co ci chodzi po glowie:\n> ");
        readline(buff, POST_SIZE);

        if (sem_wait(semset[twitter->capacity]) == -1) {
            sys_err("Nie mozna zarezerwowac rozmiaru");
        }

        if (twitter->size == twitter->capacity) {
            fprintf(stderr, "Brak miejsca na nowe wiadomosci\n");
        } else {
            if (sem_wait(semset[twitter->size]) == -1) {
                sys_err("Nie mozna zarezerwowac postu");
            }

            strncpy(twitter->posts[twitter->size].username, argv[2],
                    USERNAME_SIZE);
            strncpy(twitter->posts[twitter->size].post, buff, POST_SIZE);
            twitter->posts[twitter->size].likes = 0;

            if (sem_post(semset[twitter->size]) == -1) {
                sys_err("Nie mozna oddac postu");
            }

            ++twitter->size;
        }

        if (sem_post(semset[twitter->capacity]) == -1) {
            sys_err("Nie mozna zwolnic rozmiaru");
        }
    } else if (action == 'l' || action == 'L') {
        int post_index;
        printf("Ktory wpis chcesz polubic\n> ");
        scanf(" %d", &post_index);
        post_index -= 1; /* C arrays are 0 indexed */

        if (sem_wait(semset[twitter->capacity]) == -1) {
            sys_err("Nie mozna zarezerwowac rozmiaru");
        }

        if (post_index >= twitter->size || post_index < 0) {
            printf("Nie ma wpisu o takim indexie\n");
        } else {
            if (sem_wait(semset[post_index]) == -1) {
                sys_err("Nie mozna zarezerwowac postu");
            }
            ++twitter->posts[post_index].likes;
            if (sem_post(semset[post_index]) == -1) {
                sys_err("Nie mozna oddac postu");
            }
        }
        if (sem_post(semset[twitter->capacity]) == -1) {
            sys_err("Nie mozna zwolnic rozmiaru");
        }
    } else {
        printf("niepoprawna akcja\n");
    }

    if (semset_close(semset, twitter->capacity) == -1) {
        perror("Nie udalo sie zamknac wszystkich semafor");
    }

    free(semset);

    if (munmap(twitter, sizeof(*twitter) + twitter->capacity *
                                               sizeof(*twitter->posts)) == -1) {
        perror("Nie udalo sie odlaczyc pamieci wspoldzielonej");
    }

    printf("Dziekuje za skorzystanie z aplikacji Twitter 2.0\n\n");

    return 0;
}
