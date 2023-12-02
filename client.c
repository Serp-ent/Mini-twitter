#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>

#define UZYT_ROZMIAR 32
#define WPIS_ROZMIAR 64

struct record {
    char uzytkownik[UZYT_ROZMIAR];
    char wpis[WPIS_ROZMIAR];
    int polubienia;
}* wpisy;

int readline(char* buff, int size) {
    fgets(buff, size, stdin);
    int len = strlen(buff);
    buff[--len] = 0;  // nadpisz znak nowej lini znakiem null

    return len;
}

int main(int argc, char* argv[]) {
    int shmid;
    int semid;
    key_t klucz;   // klucz pamieci wspoldzielonej
    key_t klucz1;  // klucz semafory

    char akcja;
    int* rozmiar;
    int* max;

    struct sembuf zamow_rozmiar = {0, -1, 0};
    struct sembuf zwolnij_rozmiar = {0, 1, 0};
    struct sembuf zamow = {0, -1, 0};
    struct sembuf zwolnij = {0, 1, 0};

    struct record* wpisy;
    if (argc != 3) {
        fprintf(stderr, "uzycie: %s <klucz> <nazwa uzytkownika>\n", argv[0]);
        exit(1);
    }

    if ((klucz = ftok(argv[1], 1)) == -1) {
        perror("Nie udalo sie wygenerowac klucza");
        exit(1);
    }

    if ((shmid = shmget(klucz, 0, 0)) == -1) {
        perror("Nie udalo sie utworzyc segment pamieci dzielonej");
        exit(1);
    }

    if ((rozmiar = shmat(shmid, NULL, 0)) == (void*)-1) {
        perror("Nie mozna dolaczyc pamieci");
        exit(1);
    }
    max = rozmiar + sizeof(int);
    wpisy = (void*)max + sizeof(int);
    zamow_rozmiar.sem_num = zwolnij_rozmiar.sem_num = *max;

    if ((klucz1 = ftok(argv[1], 2)) == -1) {
        perror("Nie udalo sie wygenerowac klucza");
        exit(1);
    }
    if ((semid = semget(klucz1, 0, 0)) == -1) {
        perror("Nie mozna uzyskac dostepu do semafory");
        exit(1);
    }

    printf("Twitter 2.0 wita! (WERSJA A)\n");

    if (semop(semid, &zamow_rozmiar, 1)) {
        perror("Nie mozna zajac zasobu");
        exit(1);
    }

    printf("[Wolnych %d wpisow (na %d)]\n", *max - *rozmiar, *max);
    int size = *rozmiar;  // zapisz do lokalnej zmiennej

    if (semop(semid, &zwolnij_rozmiar, 1) == -1) {
        perror("Nie mozna zwolnic zasobu");
        exit(1);
    }

    for (int i = 0; i < size; ++i) {
        zamow.sem_num = zwolnij.sem_num = i;
        if (semop(semid, &zamow, 1)) {
            perror("Nie mozna zajac zasobu");
            exit(1);
        }

        printf("%d. %s [Autor: %s, Polubienia: %d]\n", i + 1, wpisy[i].wpis,
               wpisy[i].uzytkownik, wpisy[i].polubienia);

        if (semop(semid, &zwolnij, 1) == -1) {
            perror("Nie mozna zwolnic zasobu");
            exit(1);
        }
    }

    printf("Podaj akcje (N)owy wpis, (L)ike\n");
    printf("> ");
    scanf(" %c", &akcja);
    while (getchar() != '\n')  // ignoruj znaki az do wejscia
        ;

    if (akcja == 'N' || akcja == 'n') {
        char buff[WPIS_ROZMIAR];
        printf("Napisz co ci chodzi po glowie:\n");
        printf("> ");
        readline(buff, WPIS_ROZMIAR);

        if (semop(semid, &zamow_rozmiar, 1)) {
            perror("Nie mozna zajac zasobu");
            exit(1);
        }

        if (*rozmiar == *max) {
            fprintf(stderr, "Brak miejsca na nowe wiadomosci\n");
        } else {
            zamow.sem_num = zwolnij.sem_num = *rozmiar;
            if (semop(semid, &zamow, 1) == -1) {
                perror("Nie mozna zwolnic zasobu");
                exit(1);
            }
            strncpy(wpisy[*rozmiar].uzytkownik, argv[2], UZYT_ROZMIAR);
            strncpy(wpisy[*rozmiar].wpis, buff, WPIS_ROZMIAR);
            wpisy[*rozmiar].polubienia = 0;

            ++(*rozmiar);
            if (semop(semid, &zwolnij, 1) == -1) {
                perror("Nie mozna zwolnic zasobu");
                exit(1);
            }
        }

        if (semop(semid, &zwolnij_rozmiar, 1) == -1) {
            perror("Nie mozna zwolnic zasobu");
            exit(1);
        }

    } else if (akcja == 'L' || akcja == 'l') {
        int wpis;
        printf("Ktory wpis chcesz polubic\n");
        printf("> ");
        scanf(" %d", &wpis);
        wpis -= 1;  // komputery indexuja od 0

        if (semop(semid, &zamow_rozmiar, 1)) {
            perror("Nie mozna zajac zasobu");
            exit(1);
        }
        if (wpis >= *rozmiar || wpis < 0) {
            printf("Nie ma wpisu o takim indexie\n");
        } else {
            zamow.sem_num = zwolnij.sem_num = wpis;
            if (semop(semid, &zamow, 1)) {
                perror("Nie mozna zajac zasobu");
                exit(1);
            }
            ++wpisy[wpis].polubienia;
            if (semop(semid, &zwolnij, 1)) {
                perror("Nie mozna zajac zasobu");
                exit(1);
            }
        }
        if (semop(semid, &zwolnij_rozmiar, 1) == -1) {
            perror("Nie mozna zwolnic zasobu");
            exit(1);
        }

    } else {
        printf("niepoprawna akcja\n");
    }

    printf("Dziekuje za skorzystanie z aplikacji Twitter 2.0\n\n");

    return 0;
}
