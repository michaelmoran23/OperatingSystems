#define _POSIX_C_SOURCE 200809L         // Done with help from: https://stackoverflow.com/questions/26284110/strdup-confused-about-warnings-implicit-declaration-makes-pointer-with
#define STD_INPUT 0
#define STD_OUTPUT 1
#include <unistd.h> 
#include <stdlib.h>
#include <stdio.h>
#include <string.h> 
#include <sys/wait.h>
#include <sys/types.h>
#include <ctype.h>
#include <fcntl.h>
#include <signal.h>
#include "HW1FUN.h"

#define TRUE 1

void signal_handler(int sigNum) {       // signal handler for SIGCHLD
   while (waitpid(-1, NULL, WNOHANG) > 0);  // use WNOHANG to not block parent process
}

int main(int argc, char* argv[]) {


pid_t pids[32] = {0}; 
int status; 
int j = 0, parseCounter = 0, inputFlag = 0, outputFlag = 0, pipeFlag = 0, ampFlag = 0, inoutErr = 0;  
char* command[32] = {0};
char* tokens2;
int nFlagCheck = 0, commandCounter = 0, start = 1;


if(argc > 1 && (strcmp(argv[1], "-n") == 0 || strcmp(argv[1], "-N") == 0)) {    // Checks if -n flag is present when program is started
    nFlagCheck = 1; 
} 


struct sigaction sa;    
sa.sa_handler = signal_handler; 
sigemptyset(&sa.sa_mask); 
sa.sa_flags = SA_RESTART;

int sigAct = sigaction(SIGCHLD, &sa, NULL);     // check for SIGCHLD signal

if(sigAct < 0) {
    perror("ERROR: SIGCHLD "); 
    exit(1); 
}


while(TRUE) {
    int parse1 = 0, parse2 = 0, numCommands = 0; 
    j = 0; 
    inputFlag = 0; 
    outputFlag = 0; 
    ampFlag = 0; 
    inoutErr = 0; 
    int i = 0; 
    for(; i < 32; i++) {
        command[i] = NULL;
    }


    if(command[commandCounter] == NULL || start == 1) {     // checks if all commands have been executed
        start = 0; 
        if(nFlagCheck == 0) {
            type_prompt();                  
        }   
    }
        char *input = (char*) malloc(512 * sizeof(char *));
        char* tokens1;
        command[31] = NULL; 

        if(fgets(input, 512, stdin) == NULL) {  // if CTRL-D is pressed, exit
            exit(0);
        } else {
            tokens1 = strtok(input, "|\n\0"); 
            while(tokens1 != NULL) {    // parses input command by characters to separate commands
                command[parse1] = strdup(tokens1); 
                parse1++;
                tokens1 = strtok(NULL, "|&\n"); 
            }
                commandCounter = 0; 
    }
    numCommands = parse1; 


    if(command[0] != NULL && command[1] != NULL) {  // check for pipes
        pipeFlag = 1; 
    }


    i = 0; 
    for(; i < strlen(command[numCommands - 1]); i++) {      // checks if the user wants to run background process
        if(command[numCommands - 1][i] == '&' && ampFlag == 1) {
            printf("ERROR: Only one ampersand character is allowed per command line\n");
            inoutErr = 1;
            break;
        }
        if(command[numCommands - 1][i] == '&' && ampFlag == 0) {
            ampFlag = 1; 
            command[numCommands - 1][i] = ' '; 
        }
    }


    i = 0; 
    for(; i < strlen(command[0]); i++) {    // checks if input redirection is present
        if(command[0][i] == '<' && inputFlag == 1) {
            printf("ERROR: Only one input redirection is allowed for a single command\n"); 
            inoutErr = 1; 
            break; 
        }
        if(command[0][i] == '<' && inputFlag == 0) {
            inputFlag = 1; 
            command[0][i] = ' '; 
        }
    }

    i = 0; 
    for(; i < strlen(command[numCommands - 1]); i++) {      // checks if output redirection is present 
        if(command[numCommands - 1][i] == '>' && outputFlag == 1) {
            printf("ERROR: Only one output redirection is allowed for a single command\n"); 
            inoutErr = 1; 
            break; 
        }
        if(command[numCommands - 1][i] == '>' && outputFlag == 0) {
            outputFlag = 1; 
            command[numCommands - 1][i] = ' '; 
        }
    }

    i = 0; 
    int fds[2 * (numCommands - 1)];  // need to create 2 pipe ends per command
    j = 0; 
    for(; j < numCommands - 1; j++) {
        if(pipe(fds + j * 2) < 0) { // create pipes using pointer arithmetic 
            perror("ERROR: Pipe failure"); 
            exit(1);
        }
    }

    if(!inoutErr) {
        for(; i < numCommands; i++) { 
            char* commandParsed[32] = {0};
            j = 0; 
            tokens2 = strtok(command[i], " "); // separates tokenized commands by spaces  ** FIX **
            while(tokens2 != NULL) {
                commandParsed[j] = tokens2;
                j++;
                tokens2 = strtok(NULL, " "); 
            }
            commandParsed[j] = NULL; 
            int cpCount = j;    // get number of arguments

            

            pids[i] = fork(); 
            if(pids[i] < 0) { 
                perror("ERROR: Fork failure"); 
                exit(1);
            } else if(pids[i] == 0) {  // child process 


            if(inputFlag && i == 0) {     // input redirection
                int inputFd = open(commandParsed[1], O_RDONLY); 
                if (inputFd < 0) {
                perror("ERROR: Input file ");
            }
                dup2(inputFd, STD_INPUT);   // get input from file
                close(inputFd);  
                inputFlag = 0; 
                commandParsed[1] = NULL;    // set filename to null to prepare arguments for execution
            }  
            if(outputFlag && i == numCommands - 1) {        // output redirection
                int outputFd = open(commandParsed[cpCount-1], O_RDWR | O_CREAT, 0666);
                if (outputFd < 0) {
                perror("ERROR: Output file ");
            }
                dup2(outputFd, STD_OUTPUT);     // send output to file
                close(outputFd);
                outputFlag = 0; 
                commandParsed[cpCount - 1] = NULL;      // set filename to null to prepare arguments for execution
            }
            
            
            if(pipeFlag) {  // handle piping
                if(i > 0) { // not first command
                    dup2(fds[(i - 1) * 2],STD_INPUT); // dup the read end of the previous pipe
                }
                if(i < numCommands - 1) { // not last command
                    dup2(fds[(i * 2 + 1)], STD_OUTPUT);     // dup the write end of the current pipe
                }
            
                j = 0; 
                for(; j < 2 * (numCommands - 1); j++) {     // close file descriptors in child 
                    close(fds[j]); 
                }
            }
            
                execvp(commandParsed[0], commandParsed);    
                perror("ERROR: Execvp failed");
            } 
        }
    }

        if(pipeFlag) {
            j = 0; 
            for(; j < 2 * (numCommands - 1); j++) {     // close file descriptors in parent
                close(fds[j]); 
            }
        }
        pipeFlag = 0; 

        if(ampFlag == 0) {      // if ampersand is present, continue without making parent wait for child process
            j = 0; 
            for(; j < numCommands; j++) {       // collect child pids
                waitpid(pids[j], &status, 0);  
            }
        }

            free(input);
            j = 0; 
            for(; j < numCommands; j++) {
                free(command[i]); 
            }
        }   
    
    return 0; 
}
