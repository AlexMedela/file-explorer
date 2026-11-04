// pool.c - worker thread pool + queue of callbacks to run on the main thread

typedef void (*JobFn)(void *arg);

typedef struct Job {
    JobFn fn;
    void *arg;
    struct Job *next;
} Job;

typedef struct {
    pthread_mutex_t mtx;
    pthread_cond_t cv;
    Job *head, *tail;
} JobQueue;

static JobQueue g_jobs  = {PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, NULL, NULL};
static JobQueue g_mainq = {PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, NULL, NULL};
static int g_nworkers;
static u32 g_wake_event; // SDL user event used to wake the main loop

static void jq_push(JobQueue *q, JobFn fn, void *arg) {
    Job *j = xmalloc(sizeof *j);
    j->fn = fn;
    j->arg = arg;
    j->next = NULL;
    pthread_mutex_lock(&q->mtx);
    if (q->tail) q->tail->next = j;
    else q->head = j;
    q->tail = j;
    pthread_cond_signal(&q->cv);
    pthread_mutex_unlock(&q->mtx);
}

static void *worker_main(void *arg) {
    (void)arg;
    for (;;) {
        pthread_mutex_lock(&g_jobs.mtx);
        while (!g_jobs.head) pthread_cond_wait(&g_jobs.cv, &g_jobs.mtx);
        Job *j = g_jobs.head;
        g_jobs.head = j->next;
        if (!g_jobs.head) g_jobs.tail = NULL;
        pthread_mutex_unlock(&g_jobs.mtx);
        j->fn(j->arg);
        free(j);
    }
    return NULL;
}

static void pool_init(void) {
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    g_nworkers = (int)CLAMP(n - 1, 2, 12);
    for (int i = 0; i < g_nworkers; i++) {
        pthread_t t;
        pthread_create(&t, NULL, worker_main, NULL);
        pthread_setname_np(t, "fe-pool");
        pthread_detach(t);
    }
}

static void pool_submit(JobFn fn, void *arg) { jq_push(&g_jobs, fn, arg); }

static void main_wake(void) {
    if (!g_wake_event) return;
    SDL_Event ev;
    SDL_zero(ev);
    ev.type = g_wake_event;
    SDL_PushEvent(&ev);
}

static void main_post(JobFn fn, void *arg) {
    jq_push(&g_mainq, fn, arg);
    main_wake();
}

static void main_drain(void) {
    pthread_mutex_lock(&g_mainq.mtx);
    Job *j = g_mainq.head;
    g_mainq.head = g_mainq.tail = NULL;
    pthread_mutex_unlock(&g_mainq.mtx);
    while (j) {
        Job *n = j->next;
        j->fn(j->arg);
        free(j);
        j = n;
    }
}
