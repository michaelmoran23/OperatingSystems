#ifndef HW2_H
#define HW2_H 
#include "pthread.h"
#include <setjmp.h>
#include <semaphore.h>
#define MAX_THREADS 128


int threadCount = 0, currentThread = 0, joinCall = 0;  
struct TCB* threadsArray[MAX_THREADS]; 


struct TCB {
    pthread_t PID;
    int id; 
    jmp_buf threadContext; 
    void* stack; 
    struct TCB* next; 
    int state;  // EXITED = 0, READY = 1, RUNNING = 2, BLOCKED = 3
    void* exitStatus; 
};


typedef struct {
    unsigned value; 
    struct TCB* waiting;
    int initFlag; 
} my_sem_t;



void init_helper(); 

int pthread_create(pthread_t *thread, const pthread_attr_t *attr, void* (*start_routine)(void*), void* arg); 

void pthread_exit(void *ptr); 

pthread_t pthread_self(void); 

void schedule(int signalNum);

unsigned long int ptr_mangle(unsigned long int p);

void *start_thunk(); 

void lock(); 

void unlock(); 

int pthread_join(pthread_t thread, void** value_ptr); 

void pthread_exit_wrapper();

int sem_init(sem_t *sem, int pshared, unsigned value);

int sem_wait(sem_t *sem);

int sem_post(sem_t *sem);

int sem_destroy(sem_t *sem);





#endif
