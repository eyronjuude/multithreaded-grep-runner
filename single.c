// Libraries for debug prints
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <sys/wait.h>
// Libraries for directory traversal
#include <sys/types.h>
#include <dirent.h>
#include <stdlib.h>
#include <unistd.h>
// Libraries for task queue and synchronization
#include <pthread.h>

// Task queue implementation, with Lecture 16 from the CS 140 22.1 handlers as reference

#define MAX 251
#define SNPRINTFMAX 1001

typedef char elem[MAX];

typedef struct __node_t {
    elem value;
    struct __node_t *next;
} node_t;

typedef struct __queue_t {
    node_t *head;
    node_t *tail;
    pthread_mutex_t head_lock, tail_lock;
} queue_t;

void Queue_Init(queue_t *q){
    node_t *tmp = malloc(sizeof(node_t));
    tmp->next = NULL;
    q->head = q->tail = tmp;
    // single-threaded - no locks first!
    // end of lock
}

void Queue_Enqueue(queue_t *q, elem value, int isSilent){
    node_t *tmp = malloc(sizeof(node_t));
    assert(tmp != NULL);
    strcpy(tmp->value, value);
    tmp->next = NULL;
    // single-threaded - no locks first!
    q->tail->next = tmp;
    q->tail = tmp;
    // end of lock
    if (!isSilent) printf("[0] ENQUEUE %s\n", q->tail->value);
}

int Queue_Dequeue(queue_t *q, elem * value){
    // single-threaded - no locks first!
    node_t *tmp = q->head;
    if (tmp == NULL){
        // end of lock
        return -1; // queue was empty
    }
    node_t *new_head  = tmp->next;
    strcpy(*value, new_head->value);
    q->head = new_head;
    printf("[0] DIR %s\n", *value);
    // end of lock
    free(tmp); // free the node
    return 0;
}

void Queue_Free(queue_t *q){
    struct __node_t *tmp;
    while(q->head != NULL){
        tmp=q->head;
        q->head=q->head->next;
        free(tmp);
    }
}

int IsEmptyQueue(queue_t *q){
    return(q->head == q->tail);
}

// Initialize the task queue
queue_t taskqueue;

void dir_trav(char *path,  char *name){
    DIR *dp; // directory pointer
    struct dirent *d; // directory entry
    if(!(dp = opendir(path))) return; // empty directory
    while ((d = readdir(dp)) != NULL){
        if (d->d_type == DT_DIR) {
            if (!(strcmp(d->d_name, "."))||!(strcmp(d->d_name, "..")))
                continue;
            char newpath[MAX];
            snprintf(newpath, SNPRINTFMAX, "%s/%s", path, d->d_name);
            Queue_Enqueue(&taskqueue, newpath, 0);
        }
        else if (d->d_type == DT_REG) {
            char command[MAX];
            snprintf(command, SNPRINTFMAX, "grep %s \"%s/%s\" > /dev/null", name, path, d->d_name);
            if (system(command) == 0) printf("[0] PRESENT %s/%s\n", path, d->d_name);
            else printf("[0] ABSENT %s/%s\n", path, d->d_name);
        }
    }
    closedir(dp);
}

// Main code implementation

int main(int argc, char *argv[])
{
    // argv[0] corresponds to the executable file (ignore)
    // argv[1] corresponds to the number of workers N
    // argv[2] corresponds to the rootpath
    // argv[3] corresponds to the search string
    Queue_Init(&taskqueue);
    char arg[MAX];
    char cwd[MAX];
    // replace . or ./ with the absolute path of the working directory
    if (!(strcmp(argv[2], "."))||!(strcmp(argv[2], "./"))){
        snprintf(arg, SNPRINTFMAX, "%s", getcwd(cwd,MAX));
    }
    // relative access - append the path with the working directory
    else if (argv[2][0] != '/') {
        snprintf(arg, SNPRINTFMAX, "%s/%s", getcwd(cwd,MAX), argv[2]);
    }
    // if absolute path, stay as is
    else {
        strcpy(arg,argv[2]);
    }
    Queue_Enqueue(&taskqueue, arg, 1); // enqueue the rootpath as first task without printing ENQUEUE
    char path[MAX];
    while(!IsEmptyQueue(&taskqueue)){
        Queue_Dequeue(&taskqueue, &path);
        dir_trav(path, argv[3]);
    }
    Queue_Free(&taskqueue);
    exit(0);
}