#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <stdarg.h>
#include <libgen.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <assert.h>

static char* PROGNAME = NULL;

static int
error(char* fmt, ...)
{
    fprintf(stderr, "%s: ", PROGNAME);

	va_list args = {};
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);

	return 1;
}

static void
event(char* fmt, ...)
{

	va_list args = {};
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
}

static int
sem_lock(int semid)
{
    struct sembuf op = {.sem_num = 0, .sem_op = -1};
    return semop(semid, &op, 1);
}

static int
sem_unlock(int semid)
{
    struct sembuf op = {.sem_num = 0, .sem_op = 1};
    return semop(semid, &op, 1);
}

const int MOMMY_CAPACITY = 5;
const int N_EAGLETS = 10;
const int N_CYCLES = 7; 

static int
mommy(int* plate, int plate_semid, int request_semid)
{
    while (1)
    {
        sem_lock(request_semid);

        /* signal that all eaglets grew old */
        if (*plate == -1)
        {
            event("Mommy is flying away.\n");
            return 0;
        }

        event("Mommy is looking for food.\n");
        (*plate) += MOMMY_CAPACITY;
        
        event("Mommy is tired. She's going to sleep.\n");
        sem_unlock(plate_semid);
        sleep(1);
    }
    
    assert(0 && "unreachable");

    return -1;
}

static int
eaglet(int eaglet_id, int* plate, int plate_semid, int request_semid)
{
    event("Eaglet #%d has born\n", eaglet_id);
    
    for (int i = 0; i < N_CYCLES; i++)
    {
        sem_lock(plate_semid);

        if (*plate == 0)
        {
            event("Eaglet #%d requests food\n", eaglet_id);
            sem_unlock(request_semid);
            sem_lock(plate_semid);
        }

        event("Eaglet #%d eats (%d are left)\n", eaglet_id, *plate - 1);
        (*plate)--;

        event("Eaglet #%d sleeps\n", eaglet_id);
        sem_unlock(plate_semid);

        sleep(1);
    }

    event("Eaglet #%d grew old\n", eaglet_id);
    
    return 0;
}

int
main(int argc, char* argv[])
{
    PROGNAME = argv[0];

    if (argc > 1)
        return error("wrong usage\n");

    int retval = 0;

    int plate_sem = semget(IPC_PRIVATE, 1, IPC_CREAT | 0666);
    if (plate_sem == -1)
        return error("cannot semget: %s\n", strerror(errno));

    int request_sem = semget(IPC_PRIVATE, 1, IPC_CREAT | 0666);
    if (request_sem == -1)
        return error("cannot semget: %s\n", strerror(errno));

    int plate_shmid = shmget(IPC_PRIVATE, sizeof(int), IPC_CREAT | 0666);
    if (plate_shmid == -1)
        return error("cannot shmget: %s\n", strerror(errno));

    int* plate = (int*) shmat(plate_shmid, 0, 0);
    if (plate == (int*) -1)
        return error("cannot attach: %s\n", strerror(errno));

    sem_unlock(plate_sem);
    *plate = MOMMY_CAPACITY;

//    fprintf(stderr, "Shared initialized\n");

//    fprintf(stderr, "Starting forks\n");

    int pid = fork();
    if (pid == 0)
    {
        retval = mommy(plate, plate_sem, request_sem);

        shmdt(plate);
        return retval;
    }

    for (int i = 0; i < N_EAGLETS; i++)
    {
        pid = fork();
        if (pid == 0)
        {
            retval = eaglet(i, plate, plate_sem, request_sem);

            shmdt(plate);
            return retval;
        }
    }

    int status = 0;
    for (long i = 0; i < N_EAGLETS; i++)
        wait(&status);

    event("All eaglets grew old.\n");

    sem_lock(plate_sem);
    *plate = -1;
    sem_unlock(plate_sem);
    sem_unlock(request_sem);

    wait(&status);

//    fprintf(stderr, "Destructing shared\n");

    shmdt(plate);
    if (shmctl(plate_shmid, IPC_RMID, 0))
        return error("shmctl failed: %s\n", strerror(errno));

    if (semctl(plate_sem, 1, IPC_RMID))
        return error("semctl failed: %s\n", strerror(errno));
    if (semctl(request_sem, 1, IPC_RMID))
        return error("semctl failed: %s\n", strerror(errno));

    return retval;
}
