#include <stdlib.h>
#include <stdio.h>

#include <linux/memfd.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <sys/types.h>

#include <Python.h>
#include <stdbool.h>

#include "ringbuffer.h"


/** Convenience wrapper around memfd_create syscall, because apparently this is
  * so scary that glibc doesn't provide it...
  */
static inline int memfd_create(const char *name, unsigned int flags) {
    return syscall(__NR_memfd_create, name, flags);
}


/* initialize the ringbuffer */
static PyObject *
initialize_Ringbuffer(Ringbuffer* b, size_t capacity) {

    if(capacity % getpagesize() != 0) {
        return PyErr_Format(PyExc_ValueError, "Requested capacity (%lu) is not a multiple of the page capacity (%d)", capacity, getpagesize());
        return NULL;
    }

    // Create an anonymous file backed by memory
    if((b->fd = memfd_create("queue_region", 0)) == -1){
        PyErr_Format(PyExc_SystemError, "Could not obtain anonymous file");
        return NULL;
    }

    // Set buffer size
    if(ftruncate(b->fd, capacity) != 0){
        PyErr_Format(PyExc_SystemError, "Could not set size of anonymous file");
        return NULL;
    }

    // Ask mmap for a good address
    if((b->buffer = mmap(NULL, 2 * capacity, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)) == MAP_FAILED){
        PyErr_Format(PyExc_SystemError, "Could not allocate virtual memory");
        return NULL;
    }
    
    // Mmap first region
    if(mmap(b->buffer, capacity, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, b->fd, 0) == MAP_FAILED){
        PyErr_Format(PyExc_SystemError, "Could not map buffer into virtual memory");
        return NULL;
    }
    
    // Mmap second region, with exact address
    if(mmap(b->buffer + capacity, capacity, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, b->fd, 0) == MAP_FAILED){
        PyErr_Format(PyExc_SystemError, "Could not map buffer into virtual memory");
        return NULL;
    }

    b->capacity = capacity;

    b->head = 0;
    b->to_end = b->capacity;
    b->wrap = 0;
    b->wrapped = false;
    Py_RETURN_NONE;
}


/* free the memory when finished */
static PyObject*
deallocate_Ringbuffer(Ringbuffer* b) {

    if (b->fd == 0) {
        return 0;
    }
    
    if(munmap(b->buffer + b->capacity, b->capacity) != 0){
        PyErr_Format(PyExc_SystemError, "Could not unmap buffer");
        return NULL;
    }
    
    if(munmap(b->buffer, b->capacity) != 0){
        PyErr_Format(PyExc_SystemError, "Could not unmap buffer");
        return NULL;
    }
    
    if(close(b->fd) != 0){
        PyErr_Format(PyExc_SystemError, "Could not close anonymous file");
        return NULL;
    }
    Py_RETURN_NONE;
}

static void
ringbuffer_put(Ringbuffer* b, uint8_t* buffer, size_t size) {

    if (size <= b->to_end) {
        memcpy(&b->buffer[b->head], buffer, size);
        b->head += size;
        b->to_end -= size;

        printf("Wrote %lu bytes; capacity %lu; head at %lu; to_end %lu\n", size, b->capacity, b->head, b->to_end);
    } else {
        b->wrap = size - b->to_end;
        ringbuffer_put(b, buffer, b->to_end);

        b->head = 0;
        b->to_end = b->capacity;

        ringbuffer_put(b, buffer + size - b->wrap, b->wrap);
        b->wrapped = true;
    }

};


/* This is where we define the PyRingbuffer object structure */
typedef struct {
    PyObject_HEAD
    /* Type-specific fields go below. */
    Ringbuffer arr;
} PyRingbuffer;


/* This is the __init__ function, implemented in C */
static PyObject *
PyRingbuffer_init(PyRingbuffer *self, PyObject *args, PyObject *kwds) {
    size_t capacity = 0;

    // init may have already been called
    if (self->arr.fd != 0) {
        deallocate_Ringbuffer(&self->arr);
    }

    static char *kwlist[] = {"capacity", NULL};
    if (! PyArg_ParseTupleAndKeywords(args, kwds, "i", kwlist, &capacity)) {
        PyErr_BadArgument();
        return NULL;
    }

    if (capacity <= 0) {
        PyErr_SetString(PyExc_ValueError, "Initialized with a valid capacity");
        return NULL;
    }

    return initialize_Ringbuffer(&self->arr, capacity);
};


/* this function is called when the object is deallocated */
static void
PyRingbuffer_dealloc(PyRingbuffer* self)
{
    deallocate_Ringbuffer(&self->arr);
    Py_TYPE(self)->tp_free((PyObject*)self);
};


static PyObject*
PyRingbuffer_Put(PyRingbuffer *self, PyObject *args) {
    PyObject* mview;
    Py_buffer* buffer;

    PyArg_ParseTuple(args, "O:ref", &mview);
    if (! PyMemoryView_Check(mview)) {
        PyErr_SetString(PyExc_ValueError, "Feed a memoryview to Ringbuffer.put!");
        return NULL;
    }

    buffer = PyMemoryView_GET_BUFFER(mview);
    ringbuffer_put(&self->arr, buffer->buf, buffer->len);

    return PyLong_FromSize_t(buffer->len);
};

static PyObject*
PyRingbuffer_GetHead(PyRingbuffer *self) {
    return PyLong_FromSize_t(self->arr.head);
};

static PyObject*
PyRingbuffer_GetUsed(PyRingbuffer *self) {
    if (self->arr.wrapped) {
        return PyLong_FromSize_t(self->arr.capacity);    
    }
    return PyLong_FromSize_t(self->arr.head);
};

/* Here is the buffer interface function */
static int PyRingbuffer_GetBuffer(PyObject *obj, Py_buffer *view, int flags)
{
    if (view == NULL) {
      PyErr_SetString(PyExc_ValueError, "NULL view in getbuffer");
      return 0;
    }

    PyRingbuffer* self = (PyRingbuffer*)obj;

    view->obj = (PyObject*) self;
    view->buf = self->arr.buffer + self->arr.head;
    view->len = self->arr.capacity;
    view->readonly = 1;
    view->itemsize = 0;
    view->format = NULL;
    view->ndim = 0;
    view->shape = NULL;  // length-1 sequence of dimensions
    view->strides = NULL;  // for the simple case we can do this
    view->suboffsets = NULL;
    view->internal = NULL;
    Py_INCREF(self);  // need to increase the reference count
    return 1;
}

static PyBufferProcs PyRingbuffer_as_buffer = {
  // this definition is only compatible with Python 3.3 and above
  (getbufferproc)PyRingbuffer_GetBuffer,
  (releasebufferproc)0,  // we do not require any special release function
};


static PyMethodDef Ringbuffer_methods[] = {
    {"put", (PyCFunction) PyRingbuffer_Put, METH_VARARGS, "Put bytes in the ringbuffer" },
    {NULL}  // Sentinel
};


static PyGetSetDef Ringbuffer_getset[] = {
    {"head", (getter) PyRingbuffer_GetHead, NULL, "Position of the head in bytes"},
    {"used", (getter) PyRingbuffer_GetUsed, NULL, "Bytes used of the buffer"},
    {NULL} // Sentinel
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
    PyObject_GenericGetAttr,      /* tp_getattro */
    PyObject_GenericSetAttr,      /* tp_setattro */
    &PyRingbuffer_as_buffer,      /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT,           /* tp_flags */
    "Ringbuffer object",          /* tp_doc */
    0,                            /* tp_traverse */
    0,                            /* tp_clear */
    0,                            /* tp_richcompare */
    0,                            /* tp_weaklistoffset */
    0,                            /* tp_iter */
    0,                            /* tp_iternext */
    Ringbuffer_methods,          /* tp_methods */
    0,                            /* tp_members */
    Ringbuffer_getset,           /* tp_getset */
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
