#include "threads.h"
#include <pthread.h>
#include <stdlib.h> 
#include <unistd.h>
#include <setjmp.h>
#include <signal.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <semaphore.h>

#define STACK_SIZE 32767
#define MAX_THREADS 128

int validThreadFlag = 0; 
void* threadRetVal; 
int started = 0; 

sigset_t set, oset; 

void init_helper() {    // initialization helper function, initializes thread subsystem, main thread attributes, and signal handling

    int i = 0;
    for(; i < 128; i++) {
        threadsArray[i] = NULL; 
    }

    struct TCB* mainThreadTCB = (struct TCB*) malloc(sizeof(struct TCB));
    threadsArray[threadCount] = mainThreadTCB; 
    mainThreadTCB->state = 2;   // set state of main to RUNNING 
    mainThreadTCB->id = threadCount; 
    threadCount++; 


    struct sigaction sa;
    sa.sa_handler = schedule; 
    sigemptyset(&sa.sa_mask); 
    sa.sa_flags = SA_NODEFER; 

    if (sigaction(SIGALRM, &sa, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    ualarm(50000, 50000);   // alarm every 50ms
 


    started = 1; 

}

int pthread_create(pthread_t* thread, const pthread_attr_t *attr, void* (*start_routine)(void*), void* arg) { // function to create new threads

    if(!started) {
        init_helper();      // if pthread_create is being called for the first time, call initialization function
    } 

    struct TCB* threadTCB = (struct TCB*) malloc(sizeof(struct TCB));   // create TCB for new thread
    threadTCB->stack = malloc(STACK_SIZE);      // allocate stack for new thread
    threadTCB->id = (pthread_t)threadCount;    
    threadTCB->state = 1;   // set state to READY
    *thread = (pthread_t)threadCount; 
    threadCount++;
    

    char* stackTop = (char*) threadTCB->stack + STACK_SIZE - sizeof(void*); // pointer to the top of the stack minus some space for return value
    
    *(void**)stackTop = (void*) pthread_exit_wrapper; // put pthread_exit at the top of the stack since start_routine to invoke pthread_exit upon completion

    
    unsigned long* jb = (unsigned long*)threadTCB->threadContext;   
    jb[6] = ptr_mangle((unsigned long) stackTop);   // place mangled stack pointer into JB_RSP
    jb[7] = ptr_mangle((unsigned long) start_thunk);    // place mangled start_thunk pointer into RIP (PC) to be jumped to when thread starts
    jb[2] = (unsigned long) start_routine;  // places start routine address into JB_R12 to be executed when the thread starts and start_thunk is executed
    jb[3] = (unsigned long) arg; // place value of *arg into JB_R13 to be passed to start_routine when the thread begins

    lock(); 
    threadsArray[threadTCB->id] = threadTCB;    // add the current TCB into the threads array 
    unlock(); 
    
    raise(SIGALRM);
    return 0; 
    }
   
    


void pthread_exit(void *ptr) {      // function to clean up allocated resources and exit thread when finished 
    threadsArray[currentThread]->exitStatus = ptr; // retain return value of exiting thread
    threadsArray[currentThread]->state = 0; // set state of current thread to EXITED 
    lock();
    threadCount--; 
    unlock();
    raise(SIGALRM); 
    if(threadCount <= 0) // if there are no threads left, exit the program with 0 
        exit(0); 
    else
        while(1){};     // otherwise make the current thread idle to prevent 'noreturn funtion does return' error
}


pthread_t pthread_self(void) {  // returns id of current thread 
    return threadsArray[currentThread]->id; 
}


void schedule(int signalNum) {
    if(threadsArray[currentThread]->state == 2)     // if the current thread state is RUNNING set it to READY
        threadsArray[currentThread]->state = 1; 

    lock(); 
    if(setjmp(threadsArray[currentThread]->threadContext) == 0) {       // check if current thread has saved its context 
        validThreadFlag = 0;    // flag to indicated if a valid thread has been found
        int i = 0; 
        for(; i < MAX_THREADS; i++) {
            int threadIndex = (i + currentThread + 1) % MAX_THREADS;    // find next ready thread in queue after the currently running thread, goes back to start of array if one is not found (Round Robin)

            if(threadsArray[threadIndex] != NULL && threadsArray[threadIndex]->state == 1) {    // check if thread is valid and is READY
                currentThread = threadIndex;    // set the current thread to next READY thread
                threadsArray[currentThread]->state = 2;    // set current threads state to RUNNING
                validThreadFlag = 1;    // valid thread found
                unlock(); 
                longjmp(threadsArray[currentThread]->threadContext, 42);    // context switch to next READY thread
                break; 
            }
        }

        if(validThreadFlag == 0) {      // if no valid threads are found, exit with 0. Otherwise continue 
            exit(0); 
        } else {
            return; 
        }
    }
}


void lock() {
    sigemptyset(&set); 
    sigaddset(&set, SIGALRM); 
    sigprocmask(SIG_BLOCK, &set, &oset);    // block SIGALRM
}

void unlock() {
    sigemptyset(&oset); 
    sigaddset(&oset, SIGALRM); 
    sigprocmask(SIG_UNBLOCK, &oset, &set);  // unblock SIGALRM
}

int pthread_join(pthread_t thread, void** value_ptr) {      // function to join threads
    int i = 0;
    struct TCB* target = NULL; 
    lock();
    for(; i < MAX_THREADS; i++) {
        if(threadsArray[i] != NULL && threadsArray[i]->id == thread) {      // find thread with matching ID 
            target = threadsArray[i]; 
            break;
        }
    }
    unlock(); 

    if(target == NULL) {
        return -1; 
    }
    

    while (target->state != 0) { // while target thread has not EXITED
        raise(SIGALRM); // trigger context switch
    }
    
    
    threadsArray[currentThread]->state = 1;     // set the current threads state back to ready

    if(value_ptr != NULL) { // if the target thread terminates, collect the exit status
        *value_ptr = target->exitStatus;
    }


    lock(); 
    if(target->state == 0)
        free(target->stack); // free allocated stack 

    threadsArray[target->id] = NULL;    // clear the target thread from the array
    unlock();
    return 0; 
} 




int sem_init(sem_t *sem, int pshared, unsigned value) {     // initializes a semaphore 
    my_sem_t* mySem = malloc(sizeof(my_sem_t));
    mySem->value = value;  // initialize semaphore with value
    mySem->initFlag = 1;    // flag as initialized 
    mySem->waiting = NULL;  // empty waiting queue 

    sem->__align = (long) mySem;    // reference custom semaphore through sem_t struct 
    return 0; 
}


int sem_wait(sem_t *sem) {      // decrement (lock) the semaphore 
    my_sem_t* mySem = (my_sem_t*) sem->__align;  

    lock(); 

    while(mySem->value == 0) {  // if semaphore value is 0 block until decrement can be performed 
        threadsArray[currentThread]->state = 3; // set state of current thread to BLOCKED
        
        if(mySem->waiting == NULL) {       // add waiting thread to queue if empty 
            mySem->waiting = threadsArray[currentThread];
            threadsArray[currentThread]->next = NULL; 
        } else {
            struct TCB* endPtr = mySem->waiting; 
            while(endPtr->next != NULL) {   // find the last element of waiting queue 
                endPtr = endPtr->next; 
            }
            endPtr->next = threadsArray[currentThread];     // add thread to the end of the queue 
            threadsArray[currentThread]->next = NULL; 
        }

        unlock(); 
        raise(SIGALRM); 
        lock(); 
    }

    mySem->value--;     // perform decrement 
    unlock(); 

    return 0; 
}


int sem_post(sem_t *sem) {      // increment (unlock) semaphore 
    my_sem_t* mySem = (my_sem_t*) sem->__align;     
    if(mySem == NULL || mySem->initFlag != 1)
        return -1; 

    lock(); 
    mySem->value++;     // perform increment 

    if(mySem->waiting != NULL) {
        struct TCB* waitingThread = mySem->waiting;  // find the first waiting thread in the queue 
        mySem->waiting = mySem->waiting->next; 
        waitingThread->state = 1; // set state of first thread in queue to READY
    }
    unlock(); 
    return 0; 
}

int sem_destroy(sem_t *sem) {       // destroy the semaphore 
    my_sem_t* mySem = (my_sem_t*) sem->__align; 
    if(mySem == NULL || mySem->initFlag != 1)
        return -1; 

    free(sem);  // free allocated resources 
    return 0; 
}




unsigned long int ptr_mangle(unsigned long int p)       // mangles a pointer to account for demangling 
{
    unsigned long int ret;

    asm("movq %1, %%rax;\n"
        "xorq %%fs:0x30, %%rax;"
        "rolq $0x11, %%rax;"
        "movq %%rax, %0;"
    : "=r"(ret)
    : "r"(p)
    : "%rax"
    );
    return ret;
}

void *start_thunk() {
  asm("popq %%rbp;\n"           //clean up the function prolog
      "movq %%r13, %%rdi;\n"    //put arg in $rdi
      "pushq %%r12;\n"          //push &start_routine
      "retq;\n"                 //return to &start_routine
      :
      :
      : "%rdi"
  );
  __builtin_unreachable();
}

void pthread_exit_wrapper()    // access return value of thread 
{
    unsigned long int res;
    asm("movq %%rax, %0\n":"=r"(res));
    pthread_exit((void *) res);
}
