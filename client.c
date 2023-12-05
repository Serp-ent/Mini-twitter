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
semset_t* connect_to_semset(int nsem, const char* key) {
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

void like_post(struct Twitter* t, semset_t* semset, int post) {
    if (sem_wait(semset[t->capacity]) == -1) {
        sys_err("Nie mozna zarezerwowac rozmiaru");
    }

    if (post >= t->size || post < 0) {
        printf("Nie ma wpisu o takim indexie\n");
    } else {
        if (sem_wait(semset[post]) == -1) {
            sys_err("Nie mozna zarezerwowac postu");
        }
        ++t->posts[post].likes;
        if (sem_post(semset[post]) == -1) {
            sys_err("Nie mozna oddac postu");
        }
    }
    if (sem_post(semset[t->capacity]) == -1) {
        sys_err("Nie mozna zwolnic rozmiaru");
    }
}

void action_like_post(struct Twitter* t, semset_t* semset) {
    int post_index;
    printf("Ktory wpis chcesz polubic\n> ");
    scanf(" %d", &post_index);
    post_index -= 1; /* C arrays are 0 indexed */

    like_post(t, semset, post_index);
}

void print_posts(struct Twitter* t, semset_t* semset) {
    int i;

    if (sem_wait(semset[t->capacity]) == -1) {
        sys_err("Nie mozna zajac rozmiaru");
    }
    /* save to local variable */
    int local_capacity = t->capacity;
    int local_size = t->size;

    if (sem_post(semset[t->capacity]) == -1) {
        sys_err("Nie mozna zwolnic rozmiaru");
    }

    printf("[Wolnych %d wpisow (na %d)]\n", local_capacity - local_size,
           local_capacity);

    for (i = 0; i < local_size; ++i) {
        if (sem_wait(semset[i]) == -1) {
            sys_err("Nie mozna zarezerwowac postu");
        }

        printf("%d. %s [Autor: %s, Polubienia: %d]\n", i + 1, t->posts[i].post,
               t->posts[i].username, t->posts[i].likes);

        if (sem_post(semset[i]) == -1) {
            sys_err("Nie mozna oddac postu");
        }
    }
}

void action_create_post_by(struct Twitter* t, semset_t* semset,
                           const char* username) {
    struct Record record = {};
    strncpy(record.username, username, USERNAME_SIZE);

    printf("Napisz co ci chodzi po glowie:\n> ");
    readline(record.post, POST_SIZE);

    if (sem_wait(semset[t->capacity]) == -1) {
        sys_err("Nie mozna zarezerwowac rozmiaru");
    }

    if (t->size == t->capacity) {
        fprintf(stderr, "Brak miejsca na nowe wiadomosci\n");
    } else {
        if (sem_wait(semset[t->size]) == -1) {
            sys_err("Nie mozna zarezerwowac postu");
        }

        t->posts[t->size] = record;

        if (sem_post(semset[t->size]) == -1) {
            sys_err("Nie mozna oddac postu");
        }

        ++t->size;
    }

    if (sem_post(semset[t->capacity]) == -1) {
        sys_err("Nie mozna zwolnic rozmiaru");
    }
}

int main(int argc, char* argv[]) {
    int shmid;
    semset_t* semset;

    char action;

    struct Twitter* twitter;
    if (argc != 3) {
        usage(argv[0]);
    }

    shmid = connect_to_shm(argv[1]);
    twitter = connect_to_twitter(shmid);

    semset = connect_to_semset(twitter->capacity, argv[1]);

    printf("Twitter 2.0 wita! (WERSJA A)\n");
    print_posts(twitter, semset);

    printf("Podaj akcje (N)owy wpis, (L)ike\n> ");
    scanf(" %c", &action);
    while (getchar() != '\n') /* ignore rest of line */
        ;

    switch (action) {
        case 'n':
        case 'N':
            action_create_post_by(twitter, semset, argv[2]);
            break;
        case 'l':
        case 'L':
            action_like_post(twitter, semset);
            break;
        default:
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
