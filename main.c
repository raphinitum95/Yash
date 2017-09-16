#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <signal.h>
#include <errno.h>

#define RED     "\033[31m"
#define RESET   "\033[0m"

typedef struct process back_p;
struct process{
    pid_t lead;
    char user_in[2000];
    back_p *next;
    back_p *prev;
    back_p *end;
    int job;
    int running;
    bool amp;
};
void sig_int(int sig);
void sig_tstp(int sig);
void pidwait(int status, pid_t pid, back_p **head);
void pDone(back_p **head, bool pAll);
void LList(back_p **head, pid_t pid, char *user_in, bool ampersand);
int sig_cont(pid_t pid, int *status, bool fg_bg);
void running(back_p **head, pid_t pid, bool state);
void removeNode(back_p **head, back_p *temp);
back_p *findNode(back_p **head, pid_t pid);

void sig_int(int sig){}
void sig_tstp(int sig){}

int main(int argc, char **argv){
    back_p *head = NULL;
    char user_in[2000];
    char cwd[1024];
    char *fileRedir[3] = {[0 ... 2]=NULL};
    char *myArgs[2][1000];
    bool ampersand;
    FILE *file[3];
    int pipefd[2];
    bool pipeexist = false;
    int status, pid;
    pid_t pid_child1, pid_child2;

    if(signal(SIGINT, sig_int) == SIG_ERR){ //look for the ctrl c signal
        perror("signal(SIGINT) error");
    }
    if(signal(SIGTSTP, sig_tstp) == SIG_ERR){ //look for the ctrl z signal
        perror("signal stop (SIGTSTP) error");
    }
    signal(SIGTTOU, SIG_IGN);

    while(1){
        pipeexist = false;
        ampersand = false;
        //clearing file redirection arrays
        if(fileRedir[0] != NULL){
            fclose(file[0]);
            fileRedir[0] = NULL;
        }
        if(fileRedir[1] != NULL){
            fclose(file[1]);
            fileRedir[1] = NULL;
        }
        if(fileRedir[2] != NULL){
            fclose(file[2]);
            fileRedir[2] = NULL;
        }

        getcwd(cwd, sizeof(cwd));
        printf("%s # ", cwd); //printing the prompt message

        int c = scanf("%[^\n]%*c", user_in); //scanning for user input
        if(c == 0 || c == EOF) {
            if(c == EOF){
                while(head != NULL) {
                    kill(head->lead, SIGKILL);
                    back_p *temp = head -> next;
                    free(head);
                    head = temp;
                }
                exit(1);
            }
            pDone(&head, false);

            getchar();
            continue;
        }
        fflush(stdout);
        pDone(&head, false);

        char *token;
        int cmd = 0, count = 0, count_temp;
        char imp[2000];
        strcpy(imp, user_in);

        token = strtok(imp, " \0");

        while(token != NULL){   //parsing the input
            if(strcmp(token, "fg") == 0){   //if input is fg, turn on the foreground process
                if(head != NULL) {
                    pid_t t = head->end->lead;
                    tcsetpgrp(STDIN_FILENO, t);
                    pid = sig_cont(t, &status, true);
                    //printf("signaled is %d and exited is %d and amp is %d\n", WIFSIGNALED(status), WIFEXITED(status), head->end->amp);
                    tcsetpgrp(STDIN_FILENO, getpgid(0));
                    pidwait(status, pid, &head);
                } else{
                    perror("fg");
                }
                break;
            } else if(strcmp(token, "bg") == 0){
                if(head != NULL) {
                    back_p *temp = head->end;
                    pid_t t;
                    while(temp != NULL){
                        if(temp->running == 0){
                            t = temp->lead;
                            break;
                        }
                        temp = temp->prev;
                    }
                    printf("[%d]%c\t%s\t\t%s\n", temp->job, '+', "Running", temp->user_in);
                    pid = sig_cont(t, &status, false);
                    pidwait(status, pid, &head);
                } else{
                    perror("bg");
                }
                break;
            } else if(strcmp(token, "jobs") == 0){
                if(head == NULL){
                    //fprintf(stderr, RED "NO JOBS" RESET "\n");
                    break;
                }
                pDone(&head, true);
                break;
            } else if(strcmp(token, "&") == 0){
                ampersand = true;
                break;
            }
            if(strcmp(token, "|\0") == 0){ //There is a | present and therefore 2 commands
                pipeexist = true;
                cmd = 1;
                count_temp = count;
                count = 0;
            } else if(strcmp(token, "<\0") == 0){ //file redirection for stdin
                token = strtok(NULL, " \0");
                fileRedir[0] = strdup(token);
                file[0] = fopen(fileRedir[0], "r");
            } else if(strcmp(token, ">\0") == 0){ //file redirection for stdout
                token = strtok(NULL, " \0");
                fileRedir[1] = strdup(token);
                file[1] = fopen(fileRedir[1], "w");
            } else if(strcmp(token, "2>\0") == 0){ //file redirection for stderr
                token = strtok(NULL, " \0");
                fileRedir[2] = strdup(token);
                file[2] = fopen(fileRedir[2], "w");
            } else {
                myArgs[cmd][count] = strdup(token); //commands and arguments going into the array
                count++;
            }
            token = strtok(NULL, " \0");
        }
        if(pipeexist){
            if(pipe(pipefd) == -1){
                perror("pipe");
                exit(1);
            }
            myArgs[0][count_temp] = NULL;
            myArgs[1][count] = NULL;
        } else{
            myArgs[0][count] = NULL;
            myArgs[1][0] = NULL;
        }

        if(myArgs[0][0] != NULL) {
            pid_child1 = fork();
            if (pid_child1 == 0) {
                //child 1
                if (fileRedir[0] != NULL) {
                    dup2(fileno(file[0]), STDIN_FILENO);
                }
                if (fileRedir[2] != NULL) {
                    dup2(fileno(file[2]), STDERR_FILENO);
                }
                if (pipeexist) {
                    close(pipefd[0]);
                    dup2(pipefd[1], STDOUT_FILENO);
                } else if (fileRedir[1] != NULL) {
                    dup2(fileno(file[1]), STDOUT_FILENO);
                }
                execvp(myArgs[0][0], myArgs[0]);
                perror("exec Child 1");
                exit(-1);

            } else {
                //Parent
                if (strcmp(myArgs[0][0], "cd\0") == 0) {
                    if (myArgs[0][1] == NULL) {
                        chdir(getenv("HOME"));
                    } else {
                        chdir(myArgs[0][1]);
                    }
                }

                if (pipeexist) {
                    pid_child2 = fork();
                    if (pid_child2 == 0) {
                        //Child 2
                        close(pipefd[1]);
                        dup2(pipefd[0], STDIN_FILENO);
                        if (fileRedir[2] != NULL) {
                            dup2(fileno(file[2]), STDERR_FILENO);
                        }
                        if (fileRedir[1] != NULL) {
                            dup2(fileno(file[1]), STDOUT_FILENO);
                        }
                        execvp(myArgs[1][0], myArgs[1]);
                        perror("exec Child 2");
                        exit(1);
                    }
                    close(pipefd[0]);
                    close(pipefd[1]);
                    setpgid(pid_child2, pid_child1);
                }
                setpgid(pid_child1, pid_child1);
                LList(&head, pid_child1, user_in, ampersand);

                pid = 0;
                if(!ampersand) {
                    tcsetpgrp(STDIN_FILENO, pid_child1);
                    pid = waitpid(pid_child1, &status, WUNTRACED | WCONTINUED);
                    tcsetpgrp(STDIN_FILENO, getpgid(0));
                    if (pid == -1) {
                        if (errno != 4) {
                            perror("waitpid");
                            exit(EXIT_FAILURE);
                        } else{
                            pid = pid_child1;
                        }
                    }
                } else{
                    pid = waitpid(pid_child1, &status, WNOHANG);
                }

                pidwait(status, pid, &head);

                for (int i = 0; i < 2; ++i) {
                    for (int j = 0; j < 1000; ++j) {
                        if (myArgs[i][j] != NULL) {
                            free(myArgs[i][j]);
                        } else {
                            break;
                        }
                    }

                }
                if(fileRedir[0] != NULL){
                    fclose(file[0]);
                    free(fileRedir[0]);
                    fileRedir[0] = NULL;
                }
                if(fileRedir[1] != NULL){
                    fclose(file[1]);
                    free(fileRedir[1]);
                    fileRedir[1] = NULL;
                }
                if(fileRedir[2] != NULL){
                    fclose(file[2]);
                    free(fileRedir[2]);
                    fileRedir[2] = NULL;
                }
            }
        }
    }
}

void pidwait(int status, pid_t pid, back_p **head){
    if (WIFSTOPPED(status)) {
        running(head, pid, false);
    } else if(WIFCONTINUED(status)){
        running(head, pid, true);
    }  else if (WIFEXITED(status)) {
        back_p *temp = findNode(head, pid);
        if(temp != NULL){
            if(temp->amp){
                temp->running = 2;
            } else {
                removeNode(head, temp);
            }
        }
    } else if(WIFSIGNALED(status)){
        back_p *temp = findNode(head, pid);
        if(temp != NULL){
            removeNode(head, temp);
        }
    }
}

void pDone(back_p **head, bool pAll){
    back_p *temp = *head;
    while(temp != NULL) {
        int pid;
        char stat;
        int status;
        char *action;
        if(temp->amp) {
            pid = waitpid(temp->lead, &status, WNOHANG);
            fflush(stdout);


            pidwait(status, pid, head);
        }
        if(temp == (*head)->end){
            stat = '+';
        } else{
            stat = '-';
        }
        if(temp->running == 1){
            action = strdup("Running");
        } else if(temp->running == 0){
            action = strdup("Stopped");
        } else {
            action = strdup("Done");
        }
        if(pAll) {
            printf("[%d]%c\t%s\t\t%s\n", temp->job, stat, action, temp->user_in);
        } else if(temp->amp && temp->running == 2){
            printf("[%d]%c\t%s\t\t%s\n", temp->job, stat, action, temp->user_in);
        }
        if(temp->amp && temp->running == 2){
            temp->amp = false;
            pidwait(status, pid, head);
        }
        free(action);
        temp = temp -> next;
    }
}

void LList(back_p **head, pid_t pid, char *user_in, bool ampersand){
    back_p *temp = (back_p *) malloc(sizeof(back_p));
    strcpy(temp -> user_in, user_in);
    temp->next = NULL;
    temp->running = true;
    temp->lead = pid;
    temp->end = temp;
    if(ampersand){
        temp->amp = true;
    }
    if ((*head) == NULL) {
        temp->prev = NULL;
        temp->job = 1;
        (*head) = temp;
    } else {
        (*head)->end->next = temp;
        temp->prev = (*head)->end;
        temp->job = temp->prev->job + 1;
        (*head)->end = temp;
    }
}

int sig_cont(pid_t pid, int *status, bool fg_bg){
    if(kill(pid, SIGCONT)< 0){
        perror("sigcont");
    }
    if(fg_bg) { //if fg called this function
        return waitpid(pid, status, WUNTRACED);
    } else{ //if bg called this function
        return waitpid(pid, status, WCONTINUED);
    }
}

void running(back_p **head, pid_t pid, bool state){
    back_p *temp = findNode(head, pid);
    if(temp != NULL) {
        temp->running = state;
    }
}

void removeNode(back_p **head, back_p *temp){
    if(temp == (*head)){
        (*head) = (*head)->next;
        if((*head) != NULL) {
            (*head)->prev = NULL;
            (*head)->end = temp->end;
        }
    } else {
        if (temp == (*head)->end) {
            (*head)->end = temp->prev;
        }
        if (temp->next != NULL) {
            temp->next->prev = temp->prev;
        }
        temp->prev->next = temp->next;
    }
    free(temp);
}

back_p *findNode(back_p **head, pid_t pid){
    back_p *temp = *head;
    while(temp != NULL){
        if(pid == temp->lead){
            return temp;
        }
        temp = temp -> next;
    }
    return NULL;
}