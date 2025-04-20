#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <semaphore.h>
#include <time.h>
#include <signal.h>

#define LIB_SHM        "/lib_shm"
#define CATALOG_SHM    "/catalog_shm"


typedef struct {
    int title;      
    int row;
    int col;
    int pos;
} Book;

// Глобальные переменные для очистки
static int shm_lib_fd = -1;
static int shm_cat_fd = -1;
static int *lib = NULL;
static void *cat = NULL;
static size_t cat_size = 0;
static sem_t *sem_cat = NULL;
static pid_t *child_pids = NULL;
static int child_count = 0;
static int total = 0;
static pid_t parent_pid = 0;


int cmp_book(const void *a, const void *b) {
    return (((Book *)a) ->title - ((Book *)b) ->title);
}


void clean(void) {
    if (getpid() == parent_pid && child_pids) {
        for (int i = 0; i < child_count; i++) {
            if (child_pids[i] > 0) {
                kill(child_pids[i], SIGTERM);
            }
        }
    }
    if (lib && total > 0) munmap(lib, sizeof(int) * total);
    if (cat) munmap(cat, cat_size);
    if (shm_lib_fd != -1) close(shm_lib_fd);
    if (shm_cat_fd != -1) close(shm_cat_fd);
    shm_unlink(LIB_SHM);
    shm_unlink(CATALOG_SHM);
    if (sem_cat) {
        sem_destroy(sem_cat);
    }
    free(child_pids);
}

void handle_signal(int sig) {
    exit(1);
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Введите M N K\n");
        return 1;
    }

    int M = atoi(argv[1]); 
    int N = atoi(argv[2]); 
    int K = atoi(argv[3]); 
    total = M * N * K;


    atexit(clean);
    struct sigaction sa = {0};
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    child_pids = calloc(M, sizeof(pid_t));
    if (!child_pids) { 
        perror("calloc"); 
        return 1; 
    }

    //Разделяемая памаять для библиотеки
    shm_lib_fd = shm_open(LIB_SHM, O_CREAT | O_RDWR, 0666);

    if (shm_lib_fd < 0) {
        perror("shm_open lib");
        return 1;
    }

    if (ftruncate(shm_lib_fd, sizeof(int) * total) < 0) {
        perror("ftruncate lib");
        return 1;
    }


    lib = mmap(NULL, sizeof(int) * total, PROT_READ | PROT_WRITE, MAP_SHARED,
                          shm_lib_fd, 0);

    if (!lib) {
        perror("mmap lib");
        return 1;
    }

    //Разделяемая памаять для каталога
    shm_cat_fd = shm_open(CATALOG_SHM, O_CREAT | O_RDWR, 0666);
    if (shm_cat_fd < 0) {
        perror("shm_open cat ");
        return 1;
    }
    size_t cat_size = sizeof(int) + total * sizeof(Book) + sizeof(sem_t);

    if (ftruncate(shm_cat_fd, cat_size) < 0) {
        perror("ftruncate cat");
        return 1;
    }


    cat = mmap(NULL, cat_size,
                         PROT_READ | PROT_WRITE, MAP_SHARED,
                         shm_cat_fd, 0);

    if(!cat) {
        perror("mmap");
        return 1;
    }



    //Создаем симафор
    sem_cat = (sem_t *)cat;

    if (sem_init(sem_cat, 1, 1) < 0) {
        perror("sem_init");
        return EXIT_FAILURE;
    }

    int *catalog_size = (int *)((char *)cat + sizeof(sem_t)); //Указатель на размер
    *catalog_size = 0;
    Book *catalog = (Book *)((char *)catalog_size + sizeof(int)); //Указатель на начало каталога


    //Инициализируем книги и рандомном месте
    int *titles = malloc(sizeof(int) * total);
    for (int i = 0; i < total; i++) {
        titles[i] = i + 1;
    }
    srand(time(NULL));
    
    for (int i = total - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        int tmp = titles[i]; 
        titles[i] = titles[j]; 
        titles[j] = tmp;
    }

    for (int idx = 0; idx < total; idx++) {
        lib[idx] = titles[idx];
    }
    free(titles);


    printf("Блок отделенный --- это ряд библитеки, каждая строка в блоке это шкаф\n");
    printf("Исходное положение(ряд(row), колонка(col), номер в шкафу(pos) -> название):\n");
    for (int r = 0; r < M; r++) {
        for (int c = 0; c < N; c++) {
            for (int p = 0; p < K; p++) {
                int idx = r * N * K + c * K + p;
                printf("(%d,%d,%d)->%d  ", r, c, p, lib[idx]);
            }
            printf("\n");
        }
        printf("------------------------------------\n");
    }


    for (int r = 0; r < M; r++) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            exit(1);
        }
        if (pid == 0) {

            signal(SIGINT, SIG_DFL);
            signal(SIGTERM, SIG_DFL);
            Book *row_books = malloc(sizeof(Book) * N * K);
            int count = 0;
            for (int c = 0; c < N; c++) {
                for (int p = 0; p < K; p++) {
                    int idx = r * N * K + c * K + p;
                    row_books[count].title = lib[idx];
                    row_books[count].row = r;
                    row_books[count].col = c;
                    row_books[count].pos = p;
                    count++;
                }
            }
            //сортируем, что получилось
            qsort(row_books, count, sizeof(Book), cmp_book);

            // блокируем 
            sem_wait(sem_cat);
            int old_size = *catalog_size;
            Book *merged = malloc(sizeof(Book) * (old_size + count));
            // сливаем два отсортированых масива, как в сортировке слиянием
            int i = 0, j = 0, k_idx = 0;
            while (i < old_size && j < count) {
                if (catalog[i].title < row_books[j].title)
                    merged[k_idx++] = catalog[i++];
                else
                    merged[k_idx++] = row_books[j++];
            }
            while (i < old_size) {
                merged[k_idx++] = catalog[i++];
            }
            while (j < count) {
                merged[k_idx++] = row_books[j++];
            }
            //Копируем новый массив в каталог
            memcpy(catalog, merged, sizeof(Book) * (old_size + count));
            *catalog_size = old_size + count;
            //освобождаем
            sem_post(sem_cat);

            free(merged);
            free(row_books);

            munmap(lib, sizeof(int)*total);
            munmap(cat, cat_size);
            sem_close(sem_cat);
            exit(0);
        }
        child_pids[r] = pid;
    }
    // ждем завершения всех дочерних процессов
    for (int i = 0; i < M; i++) {
        waitpid(child_pids[i], NULL, 0);
    }

    printf("\nКаталог:\n");
    for (int i = 0; i < *catalog_size; i++) {
        printf("Название:%d -> (ряд(row) %d, колонка(col) %d, номер в шкафу(pos) %d)\n",
               catalog[i].title,
               catalog[i].row,
               catalog[i].col,
               catalog[i].pos);
    }

    
    munmap(lib, sizeof(int)*total);
    munmap(cat, cat_size);
    close(shm_lib_fd);
    close(shm_cat_fd);
    shm_unlink(LIB_SHM);
    shm_unlink(CATALOG_SHM);
    sem_close(sem_cat);
}






