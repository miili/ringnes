#include <stdio.h>
#include <unistd.h>
#include <Python.h>
#include "numpy/arrayobject.h"

#include "queue.h"

#define BUFFER_SIZE (getpagesize())
#define NUM_THREADS (8)
#define MESSAGES_PER_THREAD (getpagesize() * 2)

struct module_state {
    PyObject *error;
};

#if PY_MAJOR_VERSION >= 3
#define GETSTATE(m) ((struct module_state*)PyModule_GetState(m))
#else
#define GETSTATE(m) (&_state); (void) m;
static struct module_state _state;                                                                                 
#endif


static PyMethodDef queue_ext_methods[] = {};

#if PY_MAJOR_VERSION >= 3                                                                                          
static int queue_ext_traverse(PyObject *m, visitproc visit, void *arg) {
    Py_VISIT(GETSTATE(m)->error);
    return 0;
}

static int queue_ext_clear(PyObject *m) {
    Py_CLEAR(GETSTATE(m)->error);
    return 0;
}

static struct PyModuleDef moduledef = {
        PyModuleDef_HEAD_INIT,
        "queue_ext",
        NULL,
        sizeof(struct module_state),
        queue_ext_methods,
        NULL,
        queue_ext_traverse,
        queue_ext_clear,
        NULL
};

#define INITERROR return NULL

PyMODINIT_FUNC
PyInit_queue_ext(void)

#else
#define INITERROR return

void
initqueue_ext(void)
#endif

{
#if PY_MAJOR_VERSION >= 3
    PyObject *module = PyModule_Create(&moduledef);
#else
    PyObject *module = Py_InitModule("queue", queue_ext_methods);
#endif
    import_array();

    if (module == NULL)
        INITERROR;
    struct module_state *st = GETSTATE(module);

    st->error = PyErr_NewException("queue_ext.QueueExtError", NULL, NULL);
    if (st->error == NULL) {
        Py_DECREF(module);
        INITERROR;
    }

    Py_INCREF(st->error);
    PyModule_AddObject(module, "QueueExtError", st->error);

#if PY_MAJOR_VERSION >= 3
    return module;
#endif
}                      


void *consumer_loop(void *arg) {
    queue_t *q = (queue_t *) arg;
    size_t count = 0;
    size_t i;
    for(i = 0; i < MESSAGES_PER_THREAD; i++){
        size_t x;
        queue_get(q, (uint8_t *) &x, sizeof(size_t));
        count++;
    }
    return (void *) count;
}

void *publisher_loop(void *arg) {
    queue_t *q = (queue_t *) arg;
    size_t i;
    for(i = 0; i < NUM_THREADS * MESSAGES_PER_THREAD; i++){
        queue_put(q, (uint8_t *) &i, sizeof(size_t));
    }
    return (void *) i;
}



int main(int argc, char *argv[]){

    queue_t q;
    queue_init(&q, BUFFER_SIZE);

    pthread_t publisher;
    pthread_t consumers[NUM_THREADS];

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    
    pthread_create(&publisher, &attr, &publisher_loop, (void *) &q);
    
    intptr_t i;
    for(i = 0; i < NUM_THREADS; i++){
        pthread_create(&consumers[i], &attr, &consumer_loop, (void *) &q);
    }
    
    intptr_t sent;
    pthread_join(publisher, (void **) &sent);
    printf("publisher sent %ld messages\n", sent);
    
    intptr_t recd[NUM_THREADS];
    for(i = 0; i < NUM_THREADS; i++){
        pthread_join(consumers[i], (void **) &recd[i]);
        printf("consumer %ld received %ld messages\n", i, recd[i]);
    }
    
    pthread_attr_destroy(&attr);
    
    queue_destroy(&q);
    
    return 0;
}
