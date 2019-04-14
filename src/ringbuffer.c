#include <stdlib.h>
#include <stdio.h>

#include <linux/memfd.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <sys/types.h>

#include <Python.h>

#include "ringbuffer.h"

/** Convenience wrapper around memfd_create syscall, because apparently this is
  * so scary that glibc doesn't provide it...
  */
static inline int memfd_create(const char *name, unsigned int flags) {
    return syscall(__NR_memfd_create, name, flags);
}

/** Convenience wrappers for erroring out
  */
static inline void queue_error(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "queue error: ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
    abort();
}
static inline void queue_error_errno(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "queue error: ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, " (errno %d)\n", errno);
    va_end(args);
    abort();
}


/* initialize the ringbuffer */
static void initialize_Ringbuffer(Ringbuffer* b, size_t capacity) {
    if(capacity % getpagesize() != 0) {
            // capacity += getpagesize() - (capacity % getpagesize());
            queue_error("Requested capacity (%lu) is not a multiple of the page capacity (%d)", capacity, getpagesize());
    }
    b->capacity = capacity;
    b->arr = PyMem_RawMalloc(b->capacity);

    b->head = 0;
    b->to_end = b->capacity;
}


/* free the memory when finished */
static void deallocate_Ringbuffer(Ringbuffer* b) {
    PyMem_RawFree(b->arr);
    b->arr = NULL;
}

static void ringbuffer_put(Ringbuffer* b, uint8_t* buffer, size_t size) {
    size_t wrap;

    if (size <= b->to_end) {
        memcpy(&b->arr[b->head], buffer, size);
        b->head += size;
        b->to_end = b->capacity - b->head;
        printf("Wrote %d bytes; capacity %d; head at %d; to_end %d\n", size, b->capacity, b->head, b->to_end);
    } else {
        wrap = size - b->to_end;
        ringbuffer_put(b, buffer, b->to_end);

        b->head = 0;
        b->to_end = b->capacity;
        ringbuffer_put(b, buffer + size - wrap, wrap);
    }
}


/* This is where we define the PyRingbuffer object structure */
typedef struct {
    PyObject_HEAD
    /* Type-specific fields go below. */
    Ringbuffer arr;
} PyRingbuffer;


/* This is the __init__ function, implemented in C */
static int PyRingbuffer_init(PyRingbuffer *self, PyObject *args, PyObject *kwds) {
    size_t capacity = 0;

    // init may have already been called
    if (self->arr.arr != NULL);
        deallocate_Ringbuffer(&self->arr);

    static char *kwlist[] = {"capacity", NULL};
    if (! PyArg_ParseTupleAndKeywords(args, kwds, "|i", kwlist, &capacity))
        return -1;

    if (capacity < 0)
        capacity = 0;

    initialize_Ringbuffer(&self->arr, capacity);
    return 0;
}


/* this function is called when the object is deallocated */
static void
PyRingbuffer_dealloc(PyRingbuffer* self)
{
    deallocate_Ringbuffer(&self->arr);
    Py_TYPE(self)->tp_free((PyObject*)self);
}


static PyObject *
PyRingbuffer_put(PyRingbuffer *self, PyObject *args) {
    PyObject* mview;
    Py_buffer* buffer;

    PyArg_ParseTuple(args, "O:ref", &mview);
    if (! PyMemoryView_Check(mview)) {
        PyErr_SetString(PyExc_ValueError, "Feed a memoryview!");
        return 0;
    }
    buffer = PyMemoryView_GET_BUFFER(mview);
    printf("%d, %d, %d", buffer->buf, buffer->len, buffer->itemsize);

    ringbuffer_put(&self->arr, buffer->buf, buffer->len);
    return PyLong_FromSize_t(buffer->len);
}


/* Here is the buffer interface function */
static int
PyRingbuffer_getbuffer(PyObject *obj, Py_buffer *view, int flags)
{
  if (view == NULL) {
    PyErr_SetString(PyExc_ValueError, "NULL view in getbuffer");
    return -1;
  }

  PyRingbuffer* self = (PyRingbuffer*)obj;
  view->obj = (PyObject*) self;
  view->buf = self->arr.arr;
  view->len = self->arr.capacity;
  view->readonly = 1;
  view->itemsize = NULL;
  view->format = NULL;
  view->ndim = NULL;
  view->shape = NULL;  // length-1 sequence of dimensions
  view->strides = NULL;  // for the simple case we can do this
  view->suboffsets = NULL;
  view->internal = NULL;

  Py_INCREF(self);  // need to increase the reference count
  return 0;
}

static PyBufferProcs PyRingbuffer_as_buffer = {
  // this definition is only compatible with Python 3.3 and above
  (getbufferproc)PyRingbuffer_getbuffer,
  (releasebufferproc)0,  // we do not require any special release function
};


static PyMethodDef Ringbuffer_methods[] = {
    {"put", (PyCFunction)PyRingbuffer_put, METH_VARARGS,
     "Put bytes in the ringbuffer"
    },
    {NULL}  /* Sentinel */
};


/* Here is the type structure: we put the above functions in the appropriate place
   in order to actually define the Python object type */
static PyTypeObject PyRingbufferType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "ringnes.Ringbuffer",        /* tp_name */
    sizeof(PyRingbuffer),            /* tp_basicsize */
    0,                            /* tp_itemsize */
    (destructor)PyRingbuffer_dealloc,/* tp_dealloc */
    0,                            /* tp_print */
    0,                            /* tp_getattr */
    0,                            /* tp_setattr */
    0,                            /* tp_reserved */
    0,                            /* tp_repr */
    0,                            /* tp_as_number */
    0,                            /* tp_as_sequence */
    0,                            /* tp_as_mapping */
    0,                            /* tp_hash  */
    0,                            /* tp_call */
    0,                            /* tp_str */
    0,                            /* tp_getattro */
    0,                            /* tp_setattro */
    &PyRingbuffer_as_buffer,         /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT,           /* tp_flags */
    "Ringbuffer object",           /* tp_doc */
    0,                            /* tp_traverse */
    0,                            /* tp_clear */
    0,                            /* tp_richcompare */
    0,                            /* tp_weaklistoffset */
    0,                            /* tp_iter */
    0,                            /* tp_iternext */
    &Ringbuffer_methods,          /* tp_methods */
    0,                            /* tp_members */
    0,                            /* tp_getset */
    0,                            /* tp_base */
    0,                            /* tp_dict */
    0,                            /* tp_descr_get */
    0,                            /* tp_descr_set */
    0,                            /* tp_dictoffset */
    (initproc)PyRingbuffer_init,     /* tp_init */
};

/* now we initialize the Python module which contains our new object: */
static PyModuleDef ringbuffer_module = {
    PyModuleDef_HEAD_INIT,
    "ringbuffer",
    "Extension type for ringbuffer object.",
    -1,
    NULL, NULL, NULL, NULL, NULL
};

PyMODINIT_FUNC
PyInit_ringbuffer(void) 
{
    PyObject* m;

    PyRingbufferType.tp_new = PyType_GenericNew;
    if (PyType_Ready(&PyRingbufferType) < 0)
        return NULL;

    m = PyModule_Create(&ringbuffer_module);
    if (m == NULL)
        return NULL;

    Py_INCREF(&PyRingbufferType);
    PyModule_AddObject(m, "Ringbuffer", (PyObject *)&PyRingbufferType);
    return m;
}
