/*
 * SOURCE => 
 * https://preshing.com/20120515/memory-reordering-caught-in-the-act/
 * https://github.com/CoffeeBeforeArch/CoffeeBeforeArch.github.io/blob/master/_posts/2020-11-29-hardware-memory-ordering.md
 * https://www.youtube.com/watch?v=5sZo3SrLrGA&t=568s&ab_channel=MITOpenCourseWare
 *
 * This code is POC to show that CPU re-orders instructions in normal executions!
 * We are disabling Compiler re-ordering, so that instructions in memory are in
 * the order that we expect. However, CPU will re-order them while executing!
 *
 * We are NOT talking about race conditions here! Race conditions do NOT imply
 * re-ordering of instructions!
 *
 * Example used here:
 *
 *      thread1                  thread2
 * 1:   X = 1               3:   Y = 1
 * 2:   read Y              4:   read X
 *
 * Intuitively, we always assume that the instructions are Sequentially Consistent!
 * That means that "read Y" is ALWAYS executed after "X = 1". This seems like a given!
 * However we will see that this is NOT the case!
 *
 * If a system were Sequentially Consistent, what would be the possible values of X & Y
 * at the end of the execution?
 *
 * All possible Interleavings in a Sequentially Consistent System:
 * 
 * [ 1,2,3,4 ]      [ 1,3,2,4 ]     [ 1,3,4,2 ]     [ 3,1,2,4 ]     [ 3,4,1,2 ]     [ 3,1,4,2 ]
 * X = 1, Y = 0     X = 1, Y = 1    X = 1, Y = 1    X = 1, Y = 1    X = 0, Y = 1    X = 1, Y = 1
 *
 * In all the above Interleavings, 2 follows 1 and 4 follows 3!
 * 
 * As you see that there is NO case where X = 0 and Y = 0 in a Seq Consistent system!
 *
 * We will prove that the processors that we work on, like x86 or any commercial processor,
 * do NOT guarantee Sequential Consistency!
 *
 * The issue ONLY comes up if the threads execute on different CPU's. When we say that the
 * instructions are OUT OF ORDER, we mean that the HW re-orders them for a particular CPU!
 * So for a single CPU, the instructions are Sequentially Consistent, HOWEVER since the memory
 * is shared between CPU's, the HW re-ordering affects the way other CPU's look at those
 * instructions!
 *
 * UT RESULTS:
 * ____ distributed_systems > ./seq_c_poc 
 * ____ Number of processors: 16
 * ____ 1 reorders detected after 9754303 iterations
 * 
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <semaphore.h>
#include <pthread.h>
#include <unistd.h>
#include <sched.h>

sem_t beginSema1;
sem_t endSema1;
sem_t beginSema2;
sem_t endSema2;

int X, Y;
int r1, r2; //read X into r1 and Y into r2

void *thread1Func(void *param)
{
    time_t t;
    srand((unsigned) time(&t));

    // Set thread affinity to CPU 10
    cpu_set_t cpus;
    CPU_ZERO(&cpus);
    CPU_SET(10, &cpus);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpus);

    for (;;)                                    // Loop indefinitely
    {
        sem_wait(&beginSema1);                  // Wait for signal from main thread
        while (rand() % 8 != 0) {}              // Add a short, random delay

        // ----- THE TRANSACTION! -----
        X = 1;
        asm volatile("" ::: "memory");          // Prevent compiler reordering
        r1 = Y;

        sem_post(&endSema1);                    // Notify transaction complete
    }

    return NULL;  // Never returns
};

void *thread2Func(void *param)
{
    time_t t;
    srand((unsigned) time(&t));
    // Set thread affinity to CPU 1
    cpu_set_t cpus;
    CPU_ZERO(&cpus);
    CPU_SET(1, &cpus);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpus);

    for (;;)                                    // Loop indefinitely
    {
        sem_wait(&beginSema2);                  // Wait for signal from main thread
        while (rand() % 8 != 0) {}              // Add a short, random delay

        // ----- THE TRANSACTION! -----
        Y = 1;
        asm volatile("" ::: "memory");          // Prevent compiler reordering
        r2 = X;

        sem_post(&endSema2);                    // Notify transaction complete
    }

    return NULL;  // Never returns
};

int main()
{
    // How many CPU's that share Memory?
    int numberOfProcessors = sysconf(_SC_NPROCESSORS_ONLN);
    printf("Number of processors: %d\n\n", numberOfProcessors);

    // Initialize the semaphores
    sem_init(&beginSema1, 0, 0);
    sem_init(&beginSema2, 0, 0);
    sem_init(&endSema1, 0, 0);
    sem_init(&endSema2, 0, 0);

    // Spawn the threads
    pthread_t thread1, thread2;
    pthread_create(&thread1, NULL, thread1Func, NULL);
    pthread_create(&thread2, NULL, thread2Func, NULL);

    // Repeat the experiment ad infinitum
    int detected = 0;
    int iterations = 1;

    for (; ; iterations++)
    {
        // Reset X and Y
        X = 0;
        Y = 0;
        // Signal both threads [ The sem_post runs one of the Interleavings [ even the non-Seq Consistent ones ]
        sem_post(&beginSema1);
        sem_post(&beginSema2);
        // Wait for both threads
        sem_wait(&endSema1);
        sem_wait(&endSema2);

        // Check if there was a simultaneous reorder
        if (r1 == 0 && r2 == 0)
        {
            // Once in this code, it proves that Sequential Consistency is violated!
            detected++;
            printf("%d reorders detected after %d iterations\n", detected, iterations);
        }
    }

    return 0;  // Never returns
}
