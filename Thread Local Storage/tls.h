#ifndef TLS_H 
#define TLS_H  

#include <pthread.h>
#include <signal.h>
#include <stdint.h>

struct page {
    void* address; /* start address of page */
    int ref_count; /* counter for shared pages */
};


int tls_create(unsigned int size); 

int tls_write(unsigned int offset, unsigned int length, char *buffer); 

int tls_read(unsigned int offset, unsigned int length, char *buffer);

int tls_destroy();

int tls_clone(pthread_t tid);

void tls_handle_page_fault(int sig, siginfo_t *si, void *context);

void tls_protect(struct page *p); 

void tls_unprotect(struct page *p);




#endif
