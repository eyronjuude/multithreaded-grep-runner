// Libraries for debug prints
#include <stdio.h>
#include <string.h>
// Libraries for directory traversal
#include <sys/types.h>
#include <dirent.h>
#include <stdlib.h>
#include <unistd.h>
// Libraries for synchronization primitives
#include <pthread.h>
#include <semaphore.h>

// Define MAX to be the maximum size of PATH in bytes
#define MAX 251
// Define SNPRINTFMAX to be the maximum size of the snprintf buffer
#define SNPRINTFMAX 1001

// Task queue implementation as a threadsafe linked list, with Lecture 16 and 18 from the CS 140 22.1 handlers as reference

// Synchronization primitives
sem_t c; // initialize the semaphore variable

// PATH as elem type 
typedef char elem[MAX];

// Struct for the queue nodes
typedef struct __node_t {
    elem value;
    struct __node_t *next;
} node_t;

// Struct for the task queue
typedef struct __queue_t {
    node_t *head; // head of the queue
    node_t *tail; // tail of the queue
    pthread_mutex_t queue_lock; // initialize the queue lock
    int workers_sleeping; // count how many workers undergo sleeping
    int max_workers; // count the maximum amount of workers for the queue
} queue_t;

// Struct for the worker threads
typedef struct __worker {
    int wid; // worker ID
    elem string; // store the string to be searched on the globally shared queue
} worker;

// Declare the task queue
queue_t taskqueue;

// Initializing the task queue.
// This is run by the main thread.
void Queue_Init(queue_t *q, int workers_sleeping, int max_workers){
    node_t *tmp = malloc(sizeof(node_t));
    tmp->next = NULL;
    q->head = q->tail = tmp;
    q->workers_sleeping = workers_sleeping;
    q->max_workers = max_workers;
    pthread_mutex_init(&q->queue_lock, NULL);
    return;
}

// Threadsafe enqueueing in the task queue
void Queue_Enqueue(queue_t *q, elem value, int isSilent, worker * wkr){
    node_t *tmp = malloc(sizeof(node_t));
    snprintf(tmp->value, SNPRINTFMAX, "%s", value);
    tmp->next = NULL;
    pthread_mutex_lock(&q->queue_lock);
    q->tail->next = tmp;
    q->tail = tmp;
    pthread_mutex_unlock(&q->queue_lock);
    if (!isSilent) printf("[%d] ENQUEUE %s\n", wkr->wid, value);
    sem_post(&c);
    return;
}

// Thread termination routine
void thread_terminate(worker * wkr){
    char exitpath[MAX];
    snprintf(exitpath, SNPRINTFMAX, "\n");
    Queue_Enqueue(&taskqueue, exitpath, 1, wkr);
    return;
}

// Threadsafe dequeueing in the task queue
void Queue_Dequeue(queue_t *q, elem * value, worker * wkr){
    pthread_mutex_lock(&q->queue_lock);
    q->workers_sleeping++;
    while(q->head == q->tail){
        if (q->workers_sleeping == q->max_workers){
            // release the queue locks first
            pthread_mutex_unlock(&q->queue_lock);
            // this will call sem_post so the later sem_wait call will be negated
            thread_terminate(wkr);
            // reacquire the queue locks after enqueueing the thread termination signals
            pthread_mutex_lock(&q->queue_lock);
        }
        // release the queue locks first
        pthread_mutex_unlock(&q->queue_lock);
        sem_wait(&c);
        // reacquire the queue locks after waking up from sleep
        pthread_mutex_lock(&q->queue_lock);
    }
    q->workers_sleeping--;
    node_t *tmp = q->head;
    node_t *new_head  = tmp->next;
    snprintf(*value, SNPRINTFMAX, "%s", new_head->value);
    q->head = new_head;
    pthread_mutex_unlock(&q->queue_lock);
    if(strcmp(*value, "\n"))
        printf("[%d] DIR %s\n", wkr->wid, *value);
    free(tmp); // free the node
    return;
}

// Threadsafe freeing the task queue
void Queue_Free(queue_t *q){
    node_t * tmp;
    while(q->head != NULL){
        tmp = q->head;
        q->head = q->head->next;
        free(tmp);
    }
    return;
}

// THREADSAFE traversal of directory under path
void dir_trav(char *path, worker *wkr){
    DIR *dp; // directory pointer
    struct dirent *d; // directory entry
    if (!strcmp(path, "\n")) return;
    dp = opendir(path);
    if(dp == NULL){
        return; // empty directory
    }
    while ((d = readdir(dp)) != NULL){
        if (d->d_type == DT_DIR) {
            if (!(strcmp(d->d_name, "."))||!(strcmp(d->d_name, "..")))
                continue;
            char newpath[MAX];
            snprintf(newpath, SNPRINTFMAX, "%s/%s", path, d->d_name);
            Queue_Enqueue(&taskqueue, newpath, 0, wkr);
        }
        else if (d->d_type == DT_REG) {
            char command[MAX];
            snprintf(command, SNPRINTFMAX, "grep \"%s\" \"%s/%s\" > /dev/null", wkr->string, path, d->d_name);
            if (system(command) == 0) printf("[%d] PRESENT %s/%s\n", wkr->wid, path, d->d_name);
            else printf("[%d] ABSENT %s/%s\n", wkr->wid, path, d->d_name);
        }
    }
    closedir(dp);
    return;
}

// Worker function.
void *f(void * arg)
{
    // typecast void * to worker *
    worker * wkr = (worker *)arg;
    // initialize the path variable
    char path[MAX];
    // essentially an infinite loop until a thread exits
    while(1) {
        // Threads trying to dequeue will enter spinlock if a thread is dequeueing
        Queue_Dequeue(&taskqueue, &path, wkr);
        // THIS IS THREADSAFE - directories in the queue are disjoint
        dir_trav(path, wkr);
        // If the path obtained is a newline character, break from the loop. The newline character is the thread termination signal.
        if (!strcmp(path, "\n")) break;
    }
    // Terminate the remaining threads
    thread_terminate(wkr);
    free(wkr);
    return NULL;
}

// Main code implementation
int main(int argc, char *argv[])
{
    // argv[0] corresponds to the executable file (ignore)
    // argv[1] corresponds to the number of workers N
    // argv[2] corresponds to the rootpath
    // argv[3] corresponds to the search string

    // Parse the number of workers N as an integer
    int n = atoi(argv[1]);
    // Initialize the threads
    pthread_t tid[n];
    // Initialize the task queue
    Queue_Init(&taskqueue, 0, n);
    // Initialize the semaphore for synchronization
    sem_init(&c, 0, 0);
    // Parse the rootpath
    char arg[MAX];
    // replace . or ./ with the absolute path of the working directory
    if (!(strcmp(argv[2], "."))||!(strcmp(argv[2], "./"))){
        char cwd[MAX];
        snprintf(arg, SNPRINTFMAX, "%s", getcwd(cwd,MAX));
    }
    // relative access - append the path with the working directory
    else if (argv[2][0] != '/') {
        char cwd[MAX];
        snprintf(arg, SNPRINTFMAX, "%s/%s", getcwd(cwd,MAX), argv[2]);
    }
    // if absolute path, stay as is
    else {
        snprintf(arg, SNPRINTFMAX, "%s", argv[2]);
    }
    // Enqueue the rootpath
    Queue_Enqueue(&taskqueue, arg, 1, 0); // enqueue the rootpath as first task without printing ENQUEUE
    // ==================
    // CONCURRENT SECTION
    for (int i = 0; i < n; i++) {
        // store the worker ID on the heap
        worker * wkr = malloc(sizeof(worker));
        wkr->wid = i; // assign the worker ID to a worker
        snprintf(wkr->string, SNPRINTFMAX, "%s", argv[3]); // assign the string task to a worker
        pthread_create(&tid[i], NULL, f, wkr);
    }
    for (int i = 0; i < n; i++) {
        pthread_join(tid[i], NULL);
    }
    // ==================
    sem_destroy(&c); // destroy the initialized semaphore
    // Free the taskqueue from the heap memory
    // At this point, the task queue MUST consist of ONE terminator task
    Queue_Free(&taskqueue);
    exit(0);
}