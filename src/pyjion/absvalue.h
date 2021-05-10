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

    AbstractSources() {
        Sources = unordered_set<AbstractSource*>();
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
        return "Source: Const";
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
        return "Source: Global";
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
        return "Source: Builtin";
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
        return "Source: Local";
    }
};

class IntermediateSource : public AbstractSource {
public:
    const char* describe() override {
        return "Source: Intermediate";
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

    bool hasValue() const {
        return Value != nullptr;
    }

    bool hasSource() const {
        return Sources != nullptr;
    }

    AbstractValueWithSources mergeWith(AbstractValueWithSources other) const {
        // TODO: Is defining a new source at the merge point better?
        return {
                Value->mergeWith(other.Value),
            AbstractSource::combine(Sources, other.Sources)
            };
    }

    bool operator== (AbstractValueWithSources const & other) const {
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

    bool operator!= (AbstractValueWithSources const & other) const {
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
        if (_PyObject_IsFreed(_object) || _object == (PyObject*)0xFFFFFFFFFFFFFFFF)
            return nullptr;
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
        if (_PyObject_IsFreed(_value) || _value == (PyObject*)0xFFFFFFFFFFFFFFFF)
            return nullptr;
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

// The abstract interpreter implementation.  The abstract interpreter performs
// static analysis of the Python byte code to determine what types are known.
// Ultimately this information will feedback into code generation allowing
// more efficient code to be produced.
//
// The abstract interpreter ultimately produces a set of states for each opcode
// before it has been executed.  It also produces an abstract value for the type
// that the function returns.
//
// The abstract interpreter walks the byte code updating the stack of the stack
// and locals based upon the opcode being performed and the existing state of the
// stack.  When it encounters a branch it will merge the current state in with the
// state for where we're branching to.  If the merge results in a new starting state
// that we haven't analyzed it will then queue the target opcode as the next starting
// point to be analyzed.
//
// If the branch is unconditional, or definitively taken based upon analysis, then
// we'll go onto the next starting opcode to be analyzed.
//
// Once we've processed all of the blocks of code in this manner the analysis
// is complete.

// Tracks the state of a local variable at each location in the function.
// Each local has a known type associated with it as well as whether or not
// the value is potentially undefined.  When a variable is definitely assigned
// IsMaybeUndefined is false.
//
// Initially all locals start out as being marked as IsMaybeUndefined and a special
// typeof Undefined.  The special type is really just for convenience to avoid
// having null types.  Merging with the undefined type will produce the other type.
// Assigning to a variable will cause the undefined marker to be removed, and the
// new type to be specified.
//
// When we merge locals if the undefined flag is specified from either side we will
// propagate it to the new state.  This could result in:
//
// State 1: Type != Undefined, IsMaybeUndefined = false
//      The value is definitely assigned and we have valid type information
//
// State 2: Type != Undefined, IsMaybeUndefined = true
//      The value is assigned in one code path, but not in another.
//
// State 3: Type == Undefined, IsMaybeUndefined = true
//      The value is definitely unassigned.
//
// State 4: Type == Undefined, IsMaybeUndefined = false
//      This should never happen as it means the Undefined
//      type has leaked out in an odd way
struct AbstractLocalInfo {
    AbstractLocalInfo() = default;

    AbstractValueWithSources ValueInfo;
    bool IsMaybeUndefined;

    AbstractLocalInfo(AbstractValueWithSources valueInfo, bool isUndefined = false) : ValueInfo(valueInfo) {
        IsMaybeUndefined = true;
        assert(valueInfo.Value != nullptr);
        assert(!(valueInfo.Value == &Undefined && !isUndefined));
        IsMaybeUndefined = isUndefined;
    }

    AbstractLocalInfo mergeWith(AbstractLocalInfo other) const {
        return {
                ValueInfo.mergeWith(other.ValueInfo),
                IsMaybeUndefined || other.IsMaybeUndefined
        };
    }

    bool operator== (AbstractLocalInfo other) {
        return other.ValueInfo == ValueInfo &&
               other.IsMaybeUndefined == IsMaybeUndefined;
    }
    bool operator!= (AbstractLocalInfo other) {
        return other.ValueInfo != ValueInfo ||
               other.IsMaybeUndefined != IsMaybeUndefined;
    }
};
#endif

