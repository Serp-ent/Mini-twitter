#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>

#define USERNAME_SIZE 32
#define POST_SIZE 64

struct Record {
    char username[USERNAME_SIZE];
    char post[POST_SIZE];
    int likes;
};

/* reads line without new line character to buff (at most size - 1 characters)
returns length of string that was read */
int readline(char* buff, int size) {
    fgets(buff, size, stdin);
    int len = strlen(buff);
    buff[--len] = 0; /* overwrite new line character */

    return len;
}

/* print usage and exit */
void usage(const char* progname) {
    fprintf(stderr, "usage: %s <key> <username>\n", progname);
    exit(1);
}

int main(int argc, char* argv[]) {
    key_t shmkey;
    key_t semkey;
    int shmid;
    int semid;
    int i;

    char action;
    int* size;
    int* capacity;

    struct sembuf alloc_size = {0, -1, 0};
    struct sembuf free_size = {0, 1, 0};
    struct sembuf alloc_post = {0, -1, 0};
    struct sembuf free_post = {0, 1, 0};

    struct Record* records;
    if (argc != 3) {
        usage(argv[0]);
    }

    if ((shmkey = ftok(argv[1], 1)) == -1) {
        perror("Nie udalo sie wygenerowac klucza");
        exit(1);
    }

    if ((shmid = shmget(shmkey, 0, 0)) == -1) {
        perror("Nie udalo sie uzyskac id segmentu pamieci dzielonej");
        exit(1);
    }

    if ((size = shmat(shmid, NULL, 0)) == (void*)-1) {
        perror("Nie mozna dolaczyc pamieci");
        exit(1);
    }
    capacity = size + sizeof(int);
    records = (void*)capacity + sizeof(int);
    alloc_size.sem_num = free_size.sem_num = *capacity;

    if ((semkey = ftok(argv[1], 2)) == -1) {
        perror("Nie udalo sie wygenerowac klucza");
        exit(1);
    }
    if ((semid = semget(semkey, 0, 0)) == -1) {
        perror("Nie mozna uzyskac dostepu do semafory");
        exit(1);
    }

    printf("Twitter 2.0 wita! (WERSJA A)\n");

    if (semop(semid, &alloc_size, 1)) {
        perror("Nie mozna zajac zasobu");
        exit(1);
    }

    printf("[Wolnych %d wpisow (na %d)]\n", *capacity - *size, *capacity);
    int local_size = *size; /* save to local variable */

    if (semop(semid, &free_size, 1) == -1) {
        perror("Nie mozna zwolnic zasobu");
        exit(1);
    }

    for (i = 0; i < local_size; ++i) {
        alloc_post.sem_num = free_post.sem_num = i;
        if (semop(semid, &alloc_post, 1)) {
            perror("Nie mozna zajac zasobu");
            exit(1);
        }

        printf("%d. %s [Autor: %s, Polubienia: %d]\n", i + 1, records[i].post,
               records[i].username, records[i].likes);

        if (semop(semid, &free_post, 1) == -1) {
            perror("Nie mozna zwolnic zasobu");
            exit(1);
        }
    }

    printf("Podaj akcje (N)owy wpis, (L)ike\n");
    printf("> ");
    scanf(" %c", &action);
    while (getchar() != '\n') /* ignore rest of line */
        ;

    if (action == 'N' || action == 'n') {
        char buff[POST_SIZE];
        printf("Napisz co ci chodzi po glowie:\n> ");
        readline(buff, POST_SIZE);

        if (semop(semid, &alloc_size, 1)) {
            perror("Nie mozna zajac zasobu");
            exit(1);
        }

        if (*size == *capacity) {
            fprintf(stderr, "Brak miejsca na nowe wiadomosci\n");
        } else {
            alloc_post.sem_num = free_post.sem_num = *size;
            if (semop(semid, &alloc_post, 1) == -1) {
                perror("Nie mozna zwolnic zasobu");
                exit(1);
            }
            strncpy(records[*size].username, argv[2], USERNAME_SIZE);
            strncpy(records[*size].post, buff, POST_SIZE);
            records[*size].likes = 0;

            ++(*size);
            if (semop(semid, &free_post, 1) == -1) {
                perror("Nie mozna zwolnic zasobu");
                exit(1);
            }
        }

        if (semop(semid, &free_size, 1) == -1) {
            perror("Nie mozna zwolnic zasobu");
            exit(1);
        }

    } else if (action == 'L' || action == 'l') {
        int post_index;
        printf("Ktory wpis chcesz polubic\n> ");
        scanf(" %d", &post_index);
        post_index -= 1; /* C arrays are 0 indexed */

        if (semop(semid, &alloc_size, 1)) {
            perror("Nie mozna zajac zasobu");
            exit(1);
        }
        if (post_index >= *size || post_index < 0) {
            printf("Nie ma wpisu o takim indexie\n");
        } else {
            alloc_post.sem_num = free_post.sem_num = post_index;
            if (semop(semid, &alloc_post, 1)) {
                perror("Nie mozna zajac zasobu");
                exit(1);
            }
            ++records[post_index].likes;
            if (semop(semid, &free_post, 1)) {
                perror("Nie mozna zajac zasobu");
                exit(1);
            }
        }
        if (semop(semid, &free_size, 1) == -1) {
            perror("Nie mozna zwolnic zasobu");
            exit(1);
        }

    } else {
        printf("niepoprawna akcja\n");
    }

    printf("Dziekuje za skorzystanie z aplikacji Twitter 2.0\n\n");

    return 0;
}
