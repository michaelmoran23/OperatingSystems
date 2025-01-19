#include "tls.h"
#include <pthread.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <signal.h>

#define MAX_THREADS 128

unsigned int PAGE_SIZE;     
int initialized = 0;    // initialization flag

typedef struct thread_local_storage // TLS struct
{
 pthread_t tid;
 unsigned int size; /* size in bytes */
 unsigned int page_num; /* number of pages */
 struct page **pages; /* array of pointers to pages */
} tls;


tls* tls_array[MAX_THREADS];    // global tls array


void tls_init()
{
    int i = 0;
    for(; i < MAX_THREADS; i++) {
        tls_array[i] = NULL;
    }
    struct sigaction sigact;
    /* get the size of a page */
    /* install the signal handler for page faults (SIGSEGV, SIGBUS) */
    PAGE_SIZE = getpagesize();
    sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = SA_SIGINFO; /* use extended signal handling */
    sigact.sa_sigaction = tls_handle_page_fault;
    sigaction(SIGBUS, &sigact, NULL);
    sigaction(SIGSEGV, &sigact, NULL);
}


int tls_create(unsigned int size) {     // create TLS area for the executing thread
    pthread_t tid = pthread_self();     // get ID of current thread

    if(!initialized) {  // initialize substructure 
        tls_init();
        initialized = 1;
    }

    if(size <= 0) {
        perror("tls_create: invalid size (0) provided\n");
        return -1; 
    }

    tls* currentTLS = NULL; 
    int openIdx;    // index for open slot in the global array

    int i = 0; 
    for(; i < MAX_THREADS; i++) {   // find open slot
        currentTLS = tls_array[i];
        if(currentTLS == NULL) {
            openIdx = i;
            continue;
        } 
        
        if(currentTLS->tid == tid)  // if there is already a TLS, return error 
            return -1; 
    }

    // allocate new TLS structure
    currentTLS = calloc(1, sizeof(tls));
    currentTLS->tid = tid; 
    currentTLS->size = size;
    currentTLS->page_num = (size + (PAGE_SIZE - 1)) / PAGE_SIZE;
    currentTLS->pages = (struct page**)calloc(currentTLS->page_num, sizeof(struct page*));




        int j = 0; 
        for(; j < currentTLS->page_num; j++) {
            struct page* p = calloc(1, sizeof(struct page));    // allocate all pages for TLS
            if (!p) {
                    printf("tls_create: Failed to allocate memory for page %d\n", j);
                    return -1;
                }

            p->address = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, 0, 0);   // map memory for the page
            p->ref_count = 1;   // initial reference count
            tls_protect(p); // protect the page 
            currentTLS->pages[j] = p;  
        }
        
        
    
    tls_array[openIdx] = currentTLS;    // add the TLS to the global array

    return 0; 

}


int tls_write(unsigned int offset, unsigned int length, char *buffer) { // writes to LSA of current thread
    pthread_t tid = pthread_self(); 
    tls* currentTLS = NULL; 

    int i = 0; 
    for(; i < MAX_THREADS; i++) {   // find the TLS of currently executing thread
        currentTLS = tls_array[i];
        if(currentTLS == NULL)
            continue; 
        if(currentTLS->tid == tid) {
            break; 
        }
    }

    if(currentTLS == NULL || currentTLS->size == 0) // TLS exists error check
        return -1; 
    
    if((offset + length) > currentTLS->size)    // TLS size error check
        return -1; 


    int count, index; 
    for(count = 0, index = offset; index < (offset + length); ++count, ++index) {        // perform write operation
        struct page *p, *copy;
        unsigned int pn, offset; 
        pn = index / PAGE_SIZE;     // page number
        offset = index % PAGE_SIZE;  // page offset
        p = currentTLS->pages[pn]; 

        tls_unprotect(currentTLS->pages[pn]);

        if(p->ref_count > 1) {      // this page is shared, create a private copy (COW) 
            copy = (struct page*) calloc(1, sizeof(struct page)); 
            copy->address = mmap(0, PAGE_SIZE, PROT_WRITE, MAP_ANON | MAP_PRIVATE, 0, 0); 
            
            tls_unprotect(copy);

            copy->ref_count = 1; 
            p->ref_count--; // update original page references 
            memcpy(copy->address, p->address, PAGE_SIZE);
            tls_protect(p);

            
            currentTLS->pages[pn] = copy; 
            p = copy; 
        } 

        char* dst = ((char*) p->address) + offset; 
        *dst = buffer[count];       // write the data to the page
        tls_protect(currentTLS->pages[pn]);

    }

    i = 0; 
    for(; i < currentTLS->page_num; i++) {  // reprotect all pages
        tls_protect(currentTLS->pages[i]); 
    }

    return 0; 
}


int tls_read(unsigned int offset, unsigned int length, char *buffer) {      // read from the current thread's TLS into buffer
    pthread_t tid = pthread_self();     // current thread ID
    tls* currentTLS = NULL; 



    int i = 0;
    for(; i < MAX_THREADS; i++) {   // find current thread's TLS
        currentTLS = tls_array[i]; 
        if(currentTLS == NULL)
            continue;
        if(currentTLS->tid == tid) {
            break; 
        }
    }

    if(currentTLS == NULL || currentTLS->size == 0) {   // TLS exists error check
        perror("Error: TLS not allocated\n");
        return -1; 
    }

    if((offset + length) > currentTLS->size) {  // reading past the end of the LSA error check
        perror("Error: Size error\n");
        return -1; 
    }


    int count, index; 
    for(count = 0, index = offset; index < (offset + length); ++count, ++index) {       // perform read operation
        struct page *p; 
        unsigned int pn, poff; 
        pn = index / PAGE_SIZE;     // page number
        poff = index % PAGE_SIZE;   // page offset
        p = currentTLS->pages[pn]; 

        tls_unprotect(currentTLS->pages[pn]);
        char* src = (char*) (p->address) + poff; 
        buffer[count] = *src; 
        tls_protect(currentTLS->pages[pn]);

    }

    return 0;
}


int tls_destroy() {     // free allocated TLS for current thread
    pthread_t tid = pthread_self();     // current thread ID
    tls* currentTLS = NULL; 

    int i = 0; 
    for(; i < MAX_THREADS; i++) {   // find the current thread's TLS
        currentTLS = tls_array[i]; 
        if(currentTLS == NULL)
            continue;
        if(currentTLS->tid == tid) {
            break; 
        }
    }

    if(currentTLS == NULL || currentTLS->size == 0)     // TLS exists error check
        return -1; 

    i = 0; 
    for(; i < currentTLS->page_num; i++) {  // free each allocated page
        struct page* p = currentTLS->pages[i]; 

        p->ref_count--; 
        if(p->ref_count == 0)
            free(p); 
    }

    free(currentTLS->pages);    // free allocated page array
    free(currentTLS);   // free the allocated TLS itself
    
    currentTLS = NULL;  // remove TLS from the global array

    return 0; 
}


int tls_clone(pthread_t tid) {      //  clone the local storage area of a target thread identified by tid
    pthread_t curTid = pthread_self();  // current thread ID
    tls* currentTLS = NULL;
    tls* targetTLS = NULL; 

    int openIdx;    

    int curTidCheck = 0, tarTidCheck = 0; 
    int i = 0; 
    for(; i < MAX_THREADS; i++) {   // check if a TLS exists for the current thread
        currentTLS = tls_array[i];
        if(currentTLS == NULL) {
            openIdx = i;
            continue;
        }
        if(currentTLS->tid == curTid) {     // if TLS already exists for current thread, set error flag 
            curTidCheck = 1;  
            break;
        }
    }

    if(curTidCheck == 1)    // if TLS exists for current thread, return error
        return -1; 

    i = 0; 
    for(; i < MAX_THREADS; i++) {   // check if TLS exists for the target thread
        targetTLS = tls_array[i]; 
        if(targetTLS == NULL)
            continue;
        if(targetTLS->tid == tid) {
            tarTidCheck = 1; 
            break;
        }
    }

    if(tarTidCheck == 0)    // target thread does not have a TLS to clone
        return -1; 


    // clone the target thread's TLS
    tls* newTLS = calloc(1, sizeof(tls));
    newTLS->tid = curTid; 
    newTLS->page_num = targetTLS->page_num;
    newTLS->size = targetTLS->size; 
    newTLS->pages = calloc(targetTLS->page_num, sizeof(struct page*));

    i = 0; 
    for(; i < targetTLS->page_num; i++) {   // clone pages
        newTLS->pages[i] = targetTLS->pages[i];
        newTLS->pages[i]->ref_count++; 
    }

    tls_array[openIdx] = newTLS;    // add cloned TLS to global TLS array

    return 0; 

}


void tls_handle_page_fault(int sig, siginfo_t *si, void *context)       // handle seg fault for page faults vs standard seg fault
{
    uintptr_t p_fault = ((uintptr_t) si->si_addr) & ~(PAGE_SIZE - 1);   // faulting address page aligned
    int faultFlag = 0;
    tls* currentTLS; 

    int i = 0; 
    for(; i < MAX_THREADS; i++) {   // check if faulting address belongs to any TLS
        currentTLS = tls_array[i]; 
        if(currentTLS == NULL)
            continue; 
            int j = 0;
            for(; j < currentTLS->page_num; j++) {      // check if faulting address matches any page
                if(currentTLS->pages[j]->address == (void*)p_fault) {
                faultFlag = 1;  // TLS page fault
                break;
                }
            }
        }
    

    if(!faultFlag) {    // if not TLS page fault, handle seg fault normally 
        signal(SIGSEGV, SIG_DFL);
        signal(SIGBUS, SIG_DFL);
        raise(sig);
    }
    else {
        pthread_exit(NULL);     // TLS page fault, just kill thread
    }
}

void tls_protect(struct page *p)
{
    if (mprotect((void *)(uintptr_t) p->address, PAGE_SIZE, 0)) {
    fprintf(stderr, "tls_protect: could not protect page\n");
    exit(1);
    }
}

void tls_unprotect(struct page* p) {
    if (mprotect((void*)(uintptr_t)p->address, PAGE_SIZE, PROT_READ | PROT_WRITE)) {
        perror("tls_unprotect: mprotect failed");
        exit(1);
    } 
}



