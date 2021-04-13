/*
* The MIT License (MIT)
*
* Copyright (c) Microsoft Corporation
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included
* in all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
* OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
* OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
* ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
* OTHER DEALINGS IN THE SOFTWARE.
*
*/

#ifndef PYJION_ABSVALUE_H
#define PYJION_ABSVALUE_H

#include <Python.h>
#include <opcode.h>
#include <unordered_map>
#include "cowvector.h"

class AbstractValue;
struct AbstractValueWithSources;
class LocalSource;
struct AbstractSources;
class AbstractSource;

enum AbstractValueKind {
    AVK_Any,
    AVK_Undefined,
    AVK_Integer,
    AVK_Float,
    AVK_Bool,
    AVK_List,
    AVK_Dict,
    AVK_Tuple,
    AVK_Set,
    AVK_FrozenSet,
    AVK_String,
    AVK_Bytes,
    AVK_Bytearray,
    AVK_None,
    AVK_Function,
    AVK_Slice,
    AVK_Complex,
    AVK_Iterable,
    AVK_Code,
    AVK_Enumerate,
    AVK_File,
    AVK_Type,
    AVK_Module,
    AVK_Method
};

static bool isKnownType(AbstractValueKind kind) {
    switch (kind) {
        case AVK_Any:
        case AVK_Undefined:
        case AVK_Type:
            return false;
    }
    return true;
}

class AbstractSource {
public:
    shared_ptr<AbstractSources> Sources;

    AbstractSource();

    void escapes() const;

    bool needsBoxing() const;

    virtual bool hasConstValue() { return false; }

    virtual bool isBuiltin() {
        return false;
    }

    virtual const char* describe() {
        return "unknown source";
    }

    static AbstractSource* combine(AbstractSource* one, AbstractSource*two);
};

struct AbstractSources {
    unordered_set<AbstractSource*> Sources;
    bool m_escapes;

    AbstractSources() {
        Sources = unordered_set<AbstractSource*>();
        m_escapes = false;
    }
    void escapes() {
        m_escapes = true;
    }
};

class ConstSource : public AbstractSource {
    Py_hash_t hash;
    bool hasHashValueSet = false;
    bool hasNumericValueSet = false;
    Py_ssize_t numericValue = -1;
public:
    explicit ConstSource(PyObject* value) {
        this->hash = PyObject_Hash(value);
        if (PyErr_Occurred()){
            PyErr_Clear();
        } else {
            hasHashValueSet = true;
        }
        if (PyLong_CheckExact(value)){
            numericValue = PyLong_AsSsize_t(value);
            if (PyErr_Occurred() && PyErr_ExceptionMatches(PyExc_OverflowError)) {
                hasNumericValueSet = false;
                PyErr_Clear();
            } else {
                hasNumericValueSet = true;
            }
        }
    }

    bool hasConstValue() override { return true; }

    bool hasHashValue() const { return hasHashValueSet; }

    bool hasNumericValue() const { return hasNumericValueSet; }

    Py_ssize_t getNumericValue() const { return numericValue; }

    Py_hash_t getHash () const{
        return this->hash;
    }

    const char* describe() override {
        if (needsBoxing()) {
            return "Source: Const (escapes)";
        }
        else {
            return "Source: Const";
        }
    }
};

class GlobalSource : public AbstractSource {
    const char* _name;
    PyObject* _value;
public:
    explicit GlobalSource(const char* name, PyObject* value) {
        _name = name;
        _value = value;
    }

    const char* describe() override {
        if (needsBoxing()) {
            return "Source: Global (escapes)";
        }
        else {
            return "Source: Global";
        }
    }

    const char* getName() {
        return _name;
    }
};

class BuiltinSource : public AbstractSource {
    const char* _name;
    PyObject* _value;
public:
    explicit BuiltinSource(const char* name, PyObject* value)  {
        _name = name;
        _value = value;
    };

    const char* describe() override {
        if (needsBoxing()) {
            return "Source: Builtin (escapes)";
        }
        else {
            return "Source: Builtin";
        }
    }

    bool isBuiltin() override {
        return true;
    }

    const char* getName() {
        return _name;
    }

    PyObject* getValue() {
        return _value;
    }
};

class LocalSource : public AbstractSource {
public:
    const char* describe() override {
        if (needsBoxing()) {
            return "Source: Local (escapes)";
        }
        else {
            return "Source: Local";
        }
    }
};

class IntermediateSource : public AbstractSource {
public:
    const char* describe() override {
        if (needsBoxing()) {
            return "Source: Intermediate (escapes)";
        }
        else {
            return "Source: Intermediate";
        }
    }
};

class PgcSource : public AbstractSource {
public:
    const char* describe() override {
        return "Source: PGC";
    }
};

class IteratorSource : public AbstractSource {
    AbstractValueKind _kind;
public:
    explicit IteratorSource(AbstractValueKind iterableKind){
        _kind = iterableKind;
    }

    const char* describe() override {
        return "Source: Iterator";
    }

    AbstractValueKind kind() { return _kind; }
};

class MethodSource : public AbstractSource {
    const char* _name = "";
public:
    explicit MethodSource(const char* name){
        _name = name;
    }

    const char* describe() override {
        return "Source: Method";
    }

    const char* name() {
        return _name;
    }
};

class AbstractValue {
public:
    virtual AbstractValue* unary(AbstractSource* selfSources, int op);
    virtual AbstractValue* binary(AbstractSource* selfSources, int op, AbstractValueWithSources& other);
    virtual AbstractValue* compare(AbstractSource* selfSources, int op, AbstractValueWithSources& other);
    virtual void truth(AbstractSource* selfSources);

    virtual bool isAlwaysTrue() {
        return false;
    }
    virtual bool isAlwaysFalse() {
        return false;
    }
    virtual bool isIntern() {
        return false;
    }
    virtual bool needsGuard() {
        return false;
    }

    virtual AbstractValue* mergeWith(AbstractValue*other);
    virtual AbstractValueKind kind() = 0;
    virtual const char* describe() {
        return "";
    }

    virtual AbstractValueKind resolveMethod(const char* name) {
        return AVK_Any;
    }

    virtual PyTypeObject* pythonType();

    virtual bool known();
};

struct AbstractValueWithSources {
    AbstractValue* Value;
    AbstractSource* Sources;

    AbstractValueWithSources(AbstractValue *type = nullptr) { // NOLINT(google-explicit-constructor)
        Value = type;
        Sources = nullptr;
    }

    AbstractValueWithSources(AbstractValue *type, AbstractSource* source) {
        Value = type;
        Sources = source;
    }

    void escapes() const {
        if (Sources != nullptr) {
            Sources->escapes();
        }
    }

    bool needsBoxing() const {
        if (Sources != nullptr) {
            return Sources->needsBoxing();
        }
        return true;
    }

    bool hasValue() const {
        return Value != nullptr;
    }

    bool hasSource() const {
        return Sources != nullptr;
    }

    AbstractValueWithSources mergeWith(AbstractValueWithSources other) const {
        // TODO: Is defining a new source at the merge point better?

        auto newValue = Value->mergeWith(other.Value);
        if ((newValue->kind() != Value->kind() && Value->kind() != AVK_Undefined) ||
            (newValue->kind() != other.Value->kind() && other.Value->kind() != AVK_Undefined)) {
            if (Sources != nullptr) {
                Sources->escapes();
            }
            if (other.Sources != nullptr) {
                other.Sources->escapes();
            }
        }
        return {
                Value->mergeWith(other.Value),
            AbstractSource::combine(Sources, other.Sources)
            };
    }

    bool operator== (AbstractValueWithSources& other) const {
        if (Value != other.Value) {
            return false;
        }

        if (Sources == nullptr) {
            return other.Sources == nullptr;
        }
        else if (other.Sources == nullptr) {
            return false;
        }

        return Sources->Sources.get() == other.Sources->Sources.get();
    }

    bool operator!= (AbstractValueWithSources& other) const {
        return !(*this == other);
    }
};

class AnyValue : public AbstractValue {
    AbstractValueKind kind() override {
        return AVK_Any;
    }
    const char* describe() override {
        return "Any";
    }
};

class UndefinedValue : public AbstractValue {
    AbstractValue* mergeWith(AbstractValue*other) override {
        return other;
    }
    AbstractValueKind kind() override {
        return AVK_Undefined;
    }
    const char* describe() override {
        return "Undefined";
    }
};

class BoolValue : public AbstractValue {
    AbstractValueKind kind() override;
    AbstractValue* binary(AbstractSource* selfSources, int op, AbstractValueWithSources& other) override;
    AbstractValue* unary(AbstractSource* selfSources, int op) override;
    const char* describe() override;
    void truth(AbstractSource* selfSources) override;
};

class BytesValue : public AbstractValue {
    AbstractValueKind kind() override;
    AbstractValue* binary(AbstractSource* selfSources, int op, AbstractValueWithSources& other) override;
    AbstractValue* unary(AbstractSource* selfSources, int op) override;
    const char* describe() override;
    AbstractValueKind resolveMethod(const char* name) override;

};

class ComplexValue : public AbstractValue {
    AbstractValueKind kind() override;
    AbstractValue* binary(AbstractSource* selfSources, int op, AbstractValueWithSources& other) override;
    AbstractValue* unary(AbstractSource* selfSources, int op) override;
    const char* describe() override;
};

class IntegerValue : public AbstractValue {
    AbstractValueKind kind() override;
    AbstractValue* binary(AbstractSource*selfSources, int op, AbstractValueWithSources& other) override;
    AbstractValue* unary(AbstractSource* selfSources, int op) override;
    const char* describe() override;
    void truth(AbstractSource* sources) override;
    AbstractValueKind resolveMethod(const char* name) override;

};

class InternIntegerValue : public IntegerValue {
    bool isIntern() override {
        return true;
    }

public:
    InternIntegerValue() = default;
};

class StringValue : public AbstractValue {
    AbstractValueKind kind() override;
    AbstractValue* binary(AbstractSource* selfSources, int op, AbstractValueWithSources& other) override;
    AbstractValue* unary(AbstractSource* selfSources, int op) override;
    const char* describe() override;
    AbstractValueKind resolveMethod(const char* name) override;
};

class FloatValue : public AbstractValue {
    AbstractValueKind kind() override;
    AbstractValue* binary(AbstractSource* selfSources, int op, AbstractValueWithSources& other) override;
    AbstractValue* unary(AbstractSource* selfSources, int op) override;
    void truth(AbstractSource* selfSources) override;
    const char* describe() override;
};

class TupleValue : public AbstractValue {
    AbstractValueKind kind() override;
    AbstractValue* binary(AbstractSource* selfSources, int op, AbstractValueWithSources& other) override;
    AbstractValue* unary(AbstractSource* selfSources, int op) override;
    const char* describe() override;
};

class ListValue : public AbstractValue {
    AbstractValueKind kind() override;
    AbstractValue* binary(AbstractSource* selfSources, int op, AbstractValueWithSources& other) override;
    AbstractValue* unary(AbstractSource* selfSources, int op) override;
    const char* describe() override;
    AbstractValueKind resolveMethod(const char* name) override;
};

class DictValue : public AbstractValue {
    AbstractValueKind kind() override;
    AbstractValue* unary(AbstractSource* selfSources, int op) override;
    const char* describe() override;
    AbstractValueKind resolveMethod(const char* name) override;
};

class SetValue : public AbstractValue {
    AbstractValueKind kind() override;
    AbstractValue* binary(AbstractSource* selfSources, int op, AbstractValueWithSources& other) override;
    AbstractValue* unary(AbstractSource* selfSources, int op) override;
    const char* describe() override;
};

class FrozenSetValue : public AbstractValue {
    AbstractValueKind kind() override;
    AbstractValue* binary(AbstractSource* selfSources, int op, AbstractValueWithSources& other) override;
    AbstractValue* unary(AbstractSource* selfSources, int op) override;
    const char* describe() override;
};

class NoneValue : public AbstractValue {
    AbstractValueKind kind() override;
    AbstractValue* unary(AbstractSource* selfSources, int op) override;
    const char* describe() override;
};

class FunctionValue : public AbstractValue {
    AbstractValueKind kind() override;
    AbstractValue* unary(AbstractSource* selfSources, int op) override;
    const char* describe() override;
};

class SliceValue : public AbstractValue {
    AbstractValueKind kind() override;
    AbstractValue* unary(AbstractSource* selfSources, int op) override;
    const char* describe() override;
};

class IterableValue : public AbstractValue {
    AbstractValueKind kind() override;
    AbstractValue* unary(AbstractSource* selfSources, int op) override;
    const char* describe() override;
};

class BuiltinValue : public AbstractValue {
    AbstractValueKind kind() override;
    AbstractValue* unary(AbstractSource* selfSources, int op) override;
    const char* describe() override;
};

class ModuleValue : public AbstractValue
{
    AbstractValueKind kind() override;
    AbstractValue *unary(AbstractSource *selfSources, int op) override;
    const char *describe() override;
};

class TypeValue : public AbstractValue {
    AbstractValueKind kind() override;
    AbstractValue* unary(AbstractSource* selfSources, int op) override;
    const char* describe() override;
};

class ByteArrayValue : public AbstractValue {
    AbstractValueKind kind() override;
    AbstractValue* unary(AbstractSource* selfSources, int op) override;
    const char* describe() override;
    AbstractValueKind resolveMethod(const char* name) override;
};

class MethodValue : public AbstractValue {
    AbstractValueKind kind() override;
    AbstractValue* unary(AbstractSource* selfSources, int op) override;
    const char* describe() override;
};

class CodeObjectValue : public AbstractValue {
    AbstractValueKind kind() override;
    AbstractValue* unary(AbstractSource* selfSources, int op) override;
    const char* describe() override;
};

class EnumeratorValue : public AbstractValue {
    AbstractValueKind kind() override;
    AbstractValue* unary(AbstractSource* selfSources, int op) override;
    const char* describe() override;
};

class FileValue : public AbstractValue {
    AbstractValueKind kind() override;
    AbstractValue* unary(AbstractSource* selfSources, int op) override;
    const char* describe() override;
};

class VolatileValue: public AbstractValue{
public:
    bool needsGuard() override {
        return true;
    }
    virtual PyObject* lastValue() {
        Py_RETURN_NONE;
    }
};

class PgcValue : public VolatileValue {
    PyTypeObject* _type;
    PyObject* _object;
public:
    explicit PgcValue(PyTypeObject* type, PyObject* object){
        _type = type;
        _object = object;
    }
    AbstractValueKind kind() override;
    PyTypeObject* pythonType() override;
    bool known() override {
        return true;
    }
    PyObject* lastValue() override {
        return _object;
    }
};

class ArgumentValue: public VolatileValue {
    PyTypeObject* _type;
    PyObject* _value;
public:
    explicit ArgumentValue(PyTypeObject* type, PyObject* value){
        _type = type;
        _value = value;
    }
    AbstractValueKind kind() override;
    PyTypeObject* pythonType() override;
    bool known() override {
        return true;
    }
    PyObject* lastValue() override {
        return _value;
    }
};

AbstractValueKind knownFunctionReturnType(AbstractValueWithSources source);

extern UndefinedValue Undefined;
extern AnyValue Any;
extern BoolValue Bool;
extern IntegerValue Integer;
extern InternIntegerValue InternInteger;
extern FloatValue Float;
extern ListValue List;
extern TupleValue Tuple;
extern SetValue Set;
extern FrozenSetValue FrozenSet;
extern StringValue String;
extern BytesValue Bytes;
extern DictValue Dict;
extern NoneValue None;
extern FunctionValue Function;
extern SliceValue Slice;
extern ComplexValue Complex;
extern BuiltinValue Builtin;
extern IterableValue Iterable;
extern ModuleValue Module;
extern TypeValue Type;
extern ByteArrayValue ByteArray;
extern MethodValue Method;
extern CodeObjectValue CodeObject;
extern EnumeratorValue Enumerator;
extern FileValue File;

AbstractValue* avkToAbstractValue(AbstractValueKind);
AbstractValueKind GetAbstractType(PyTypeObject* type);
PyTypeObject* GetPyType(AbstractValueKind type);
#endif

