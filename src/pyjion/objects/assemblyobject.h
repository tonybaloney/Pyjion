//
// Created by Anthony Shaw on 27/10/21.
//

#include <Python.h>
#include <structmember.h>

#include "../peloader.h"
#include "../corehost.h"

#ifndef SRC_PYJION_ASSEMBLYOBJECT_H
#define SRC_PYJION_ASSEMBLYOBJECT_H

typedef struct {
    PyObject_HEAD
            /* Type-specific fields go here. */
            PyObject* name;
            PyObject* namespace_;
            PyObject* assembly;
            PyObject* qualifiedName;
            uint16_t flags; // .NET Metadata flags for TypeDef
} PyjionAssemblyTypeObject;

typedef struct {
    PyObject_HEAD
            /* Type-specific fields go here. */
            PyObject* module_name;
    PyObject* version;
    PyObject* assembly;
    PyObject* types; // Public types dictionary.
    PEDecoder* decoder;
} PyjionAssemblyObject;

typedef struct {
    PyObject_HEAD
    /* Type-specific fields go here. */
    PyObject* method_name;
    PyjionAssemblyTypeObject * assembly_type;
    component_entry_point_fn methodPtr;
} PyjionAssemblyMethodObject;

PyObject * PyJitAssembly_new(PyTypeObject *type, PyObject *args, PyObject *kwds);
int PyJitAssembly_init(PyjionAssemblyObject *self, PyObject *args, PyObject *kwargs);
void PyJitAssembly_dealloc(PyjionAssemblyObject * self);
PyObject * PyJitAssembly_repr(PyjionAssemblyObject *self);
PyObject * PyJitAssemblyType_new(PyjionAssemblyObject* assembly, TypeDefRow typeDef);

static PyMemberDef PyJitAssembly_members[] = {
        {"name", T_OBJECT_EX, offsetof(PyjionAssemblyObject, module_name), 0,"Module Name"},
        {"version", T_OBJECT_EX, offsetof(PyjionAssemblyObject, version), 0,"Assembly Version"},
        {"types", T_OBJECT_EX, offsetof(PyjionAssemblyObject, types), 0,"Assembly Types"},

        {nullptr}  /* Sentinel */
};

static PyMemberDef PyJitAssemblyType_members[] = {
        {"name", T_OBJECT_EX, offsetof(PyjionAssemblyTypeObject, name), 0,"Type Name"},
        {"namespace", T_OBJECT_EX, offsetof(PyjionAssemblyTypeObject, namespace_), 0,"Type Namespace"},
        {"assembly", T_OBJECT_EX, offsetof(PyjionAssemblyTypeObject, assembly), 0,"Source assembly"},
        {"qualifiedName", T_OBJECT_EX, offsetof(PyjionAssemblyTypeObject, qualifiedName), 0,"Qualified Name"},

        {nullptr}  /* Sentinel */
};

static PyMethodDef PyJitAssembly_methods[] = {
        {nullptr}  /* Sentinel */
};


PyAPI_DATA(PyTypeObject) PyjionAssembly_Type;
PyAPI_DATA(PyTypeObject) PyjionAssemblyType_Type;
PyAPI_DATA(PyTypeObject) PyjionAssemblyMethod_Type;
#endif//SRC_PYJION_ASSEMBLYOBJECT_H
