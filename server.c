#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/types.h>

#define UZYT_ROZMIAR 32
#define WPIS_ROZMIAR 64

int* rozmiar;
int* max;
struct record {
    char uzytkownik[UZYT_ROZMIAR];
    char wpis[WPIS_ROZMIAR];
    int polubienia;
}* wpisy;

int shmid;
int semid;

void sigstop_wypisz_wpisy(int sig) {
    printf("\n");

    struct sembuf zamow_rozmiar = {*max, -1, 0};
    struct sembuf zwolnij_rozmiar = {*max, 1, 0};
    struct sembuf zamow = {0, -1, 0};
    struct sembuf zwolnij = {0, 1, 0};

    if (semop(semid, &zamow_rozmiar, 1) == -1) {
        perror("Nie mozna zajac zasobu");
        return;
    }

    if (*rozmiar == 0) {
        printf("Brak wpisow\n");
    } else {
        printf("___________  Twitter 2.0:  ___________\n");
        for (int i = 0; i < *rozmiar; ++i) {
            zamow.sem_num = zwolnij.sem_num = i;

            if (semop(semid, &zamow, 1) == -1) {
                perror("Nie mozna zwolnic zasobu");
                return;
            }

            printf("%d. [%s]: %s [Polubienia: %d]\n", i + 1,
                   wpisy[i].uzytkownik, wpisy[i].wpis, wpisy[i].polubienia);

            if (semop(semid, &zwolnij, 1) == -1) {
                perror("Nie mozna zwolnic zasobu");
                return;
            }
        }
    }

    if (semop(semid, &zwolnij_rozmiar, 1) == -1) {
        perror("Nie mozna zwolnic zasobu");
        return;
    }
}

void sigint_cleanup(int sig) {
    printf("[SERWER]: dostalem SIGINT => koncze i sprzatam... ");
    printf("odlaczanie: %s, usuniecie: %s\n",
           (shmdt(rozmiar) == 0) ? "OK" : strerror(errno),
           (shmctl(shmid, IPC_RMID, 0) == 0) ? "OK" : strerror(errno));
    printf("usuwanie semafor: %s\n",
           (semctl(semid, IPC_RMID, 0) == 0) ? "OK" : strerror(errno));
    exit(0);
}

union semun {
    int val;               /* Value for SETVAL */
    struct semid_ds* buf;  /* Buffer for IPC_STAT, IPC_SET */
    unsigned short* array; /* Array for GETALL, SETALL */
    struct seminfo* __buf; /* Buffer for IPC_INFO
                              (Linux-specific) */
};

int main(int argc, char* argv[]) {
    key_t klucz;   // klucz pamieci wspoldzielonej
    key_t klucz1;  // klucz semafory
    int n;
    struct shmid_ds shmd;
    union semun wartosc;

    signal(SIGTSTP, sigstop_wypisz_wpisy);
    signal(SIGINT, sigint_cleanup);

    if (argc != 3) {
        fprintf(stderr, "uzycie: %s, <klucz> <liczba wpisow>\n", argv[0]);
        exit(1);
    }

    errno = 0;  // zalecenie z notatki w manualu
    n = strtol(argv[2], NULL, 10);
    if (errno != 0) {
        perror("Drugim argumentem powinien byc int");
        exit(1);
    }

    printf("[SERWER]: Twitter 2.0 (Wersja A)\n");
    printf("[SERWER]: tworze klucz na podstawie pliku %s... ", argv[1]);
    klucz = ftok(argv[1], 1);
    if (klucz == -1) {
        perror("Nie udalo sie wygenerowac klucza");
        exit(1);
    }
    printf("OK (klucz: %d)\n", klucz);

    klucz1 = ftok(argv[1], 2);
    if (klucz1 == -1) {
        perror("Nie udalo sie wygenerowac klucza");
        sigint_cleanup(0);
    }

    // jeden wiecej dla kontrolowania rozmiaru i pojemnosci
    if ((semid = semget(klucz1, n + 1, 0666 | IPC_CREAT)) == -1) {
        perror("Nie mozna utworzyc zbioru semafor");
        sigint_cleanup(0);
    }

    wartosc.val = 1;
    for (int i = 0; i < n + 1; ++i) {  // + 1 zeby zainicjalizowac tez rozmiar
        if (semctl(semid, i, SETVAL, wartosc) == -1) {
            perror("Nie mozna zainicjalizowac semafory");
            sigint_cleanup(0);
        }
    }

    printf("[SERWER]: Tworze segment pamieci wspolnej na %d wpisow po %lub... ",
           n, sizeof(struct record));
    shmid =
        shmget(klucz, sizeof(int) * 2 + sizeof(*wpisy) * n, IPC_CREAT | 0666);
    if (shmid == -1) {
        perror("Nie udalo sie utworzyc segment pamieci dzielonej");
        exit(1);
    }

    // zaladuj informacje a segmencie pamieci dzielonej
    if (shmctl(shmid, IPC_STAT, &shmd) == -1) {
        perror("Nie mozna odczytac informacji o pamieci wspoldzielonej");
        sigint_cleanup(0);
    }
    printf("OK (id: %d, rozmiar: %ld)\n", shmid, shmd.shm_segsz);

    printf("[SERWER]: dolaczam pamiec wspolna... ");
    rozmiar = shmat(shmid, NULL, 0);
    max = rozmiar + sizeof(int);
    wpisy = (void*)max + sizeof(int);

    if (wpisy == (void*)-1) {
        perror("Nie mozna dolaczyc segmentu pamieci");
        sigint_cleanup(0);
    }
    *max = n;  // ustaw pojemnosc twittera

    printf("OK (adres: %lu)\n", (long)wpisy);
    printf("[SERWER]: nacisnij Ctrl^Z by wyswietlic stan serwisu\n");
    printf("[SERWER]: nacisnij Ctrl^C by zakonczyc program\n");

    while (1) {
        // zacznij czekac na sygnaly
    }

    return 0;
}
