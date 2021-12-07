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

#define BIG_INTEGER 1000000000

#include <Python.h>
#include <opcode.h>
#include <unordered_map>
#include "cowvector.h"
#include "types.h"
#include "base.h"
#include "objects/unboxedrangeobject.h"

#ifdef WINDOWS
typedef SIZE_T size_t;
typedef SSIZE_T ssize_t;
#endif

class AbstractValue;
struct AbstractValueWithSources;
class LocalSource;
struct AbstractSources;
class AbstractSource;

enum AbstractValueKind {
    AVK_Any = 0,
    AVK_Undefined = 1,
    AVK_Integer = 2,
    AVK_Float = 3,
    AVK_Bool = 4,
    AVK_List = 5,
    AVK_Dict = 6,
    AVK_Tuple = 7,
    AVK_Set = 8,
    AVK_FrozenSet = 9,
    AVK_String = 10,
    AVK_Bytes = 11,
    AVK_Bytearray = 12,
    AVK_None = 13,
    AVK_Function = 14,
    AVK_Slice = 15,
    AVK_Complex = 16,
    AVK_Iterable = 17,
    AVK_Code = 18,
    AVK_Enumerate = 19,
    AVK_Type = 20,
    AVK_Module = 21,
    AVK_Method = 22,
    AVK_BigInteger = 23,
    AVK_Range = 24,
    AVK_RangeIterator = 25,
    AVK_MemoryView = 26,
    AVK_Classmethod = 27,
    AVK_Filter = 28,
    AVK_Property = 29,
    AVK_Map = 30,
    AVK_Baseobject = 31,
    AVK_Reversed = 32,
    AVK_Staticmethod = 33,
    AVK_Super = 34,
    AVK_Zip = 35,
    AVK_UnboxedRangeIterator = 36,
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

class AbstractSource : public PyjionBase {
    vector<pair<py_opindex, size_t>> _consumers;
    py_opindex _producer;

public:
    shared_ptr<AbstractSources> Sources;

    explicit AbstractSource(py_opindex producer);

    virtual bool hasConstValue() { return false; }

    virtual bool isBuiltin() {
        return false;
    }
    virtual bool isIntermediate() {
        return false;
    }
    virtual const char* describe() {
        return "unknown source";
    }

    void addConsumer(py_opindex index, size_t position) {
        _consumers.emplace_back(index, position);
    }

    ssize_t isConsumedBy(py_opindex idx) {
        for (auto& _consumer : _consumers) {
            if (_consumer.first == idx)
                return _consumer.second;
        };
        return -1;
    }

    py_opindex producer() const {
        return _producer;
    }

    void setProducer(py_opindex i) {
        _producer = i;
    }

    static AbstractSource* combine(AbstractSource* one, AbstractSource* two);
};

struct AbstractSources {
    unordered_set<AbstractSource*> Sources;

    AbstractSources() {
        Sources = unordered_set<AbstractSource*>();
    }
};

class ConstSource : public AbstractSource {
    PyObject* value;
    Py_hash_t hash;
    bool hasHashValueSet = false;
    bool hasNumericValueSet = false;
    Py_ssize_t numericValue = -1;

public:
    explicit ConstSource(PyObject* value, py_opindex producer) : AbstractSource(producer) {
        this->value = value;
        this->hash = PyObject_Hash(value);
        if (PyErr_Occurred()) {
            PyErr_Clear();
        } else {
            hasHashValueSet = true;
        }
        if (PyLong_CheckExact(value)) {
            numericValue = PyLong_AsSsize_t(value);
            if (PyErr_Occurred() && PyErr_ExceptionMatches(PyExc_OverflowError)) {
                hasNumericValueSet = false;
                PyErr_Clear();
            } else {
                hasNumericValueSet = true;
            }
        }
    }

    PyObject* getValue() { return value; }

    bool hasConstValue() override { return true; }

    bool hasHashValue() const { return hasHashValueSet; }

    bool hasNumericValue() const { return hasNumericValueSet; }

    Py_ssize_t getNumericValue() const { return numericValue; }

    Py_hash_t getHash() const {
        return this->hash;
    }

    const char* describe() override {
        return "Const";
    }
};

class GlobalSource : public AbstractSource {
    const char* _name;
    PyObject* _value;

public:
    explicit GlobalSource(const char* name, PyObject* value, py_opindex producer) : AbstractSource(producer) {
        _name = name;
        _value = value;
    }

    const char* describe() override {
        return "Global";
    }

    const char* getName() {
        return _name;
    }
};

class BuiltinSource : public AbstractSource {
    const char* _name;
    PyObject* _value;

public:
    explicit BuiltinSource(const char* name, PyObject* value, py_opindex producer) : AbstractSource(producer) {
        _name = name;
        _value = value;
    };

    const char* describe() override {
        return "Builtin";
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
    explicit LocalSource(py_opindex producer) : AbstractSource(producer){};

    const char* describe() override {
        return "Local";
    }
};

class IntermediateSource : public AbstractSource {
public:
    explicit IntermediateSource(py_opindex producer) : AbstractSource(producer){};

    const char* describe() override {
        return "Intermediate";
    }

    bool isIntermediate() override {
        return true;
    }
};

class IteratorSource : public AbstractSource {
    AbstractValueKind _kind;

public:
    IteratorSource(AbstractValueKind iterableKind, py_opindex producer) : AbstractSource(producer) {
        _kind = iterableKind;
    }

    const char* describe() override {
        return "Iterator";
    }

    AbstractValueKind kind() { return _kind; }
};

class MethodSource : public AbstractSource {
    const char* _name = "";

public:
    explicit MethodSource(const char* name, py_opindex producer) : AbstractSource(producer) {
        _name = name;
    }

    const char* describe() override {
        return "Method";
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
    virtual AbstractValue* iter(AbstractSource* selfSources);
    virtual AbstractValue* next(AbstractSource* selfSources);
    virtual AbstractValue* item(AbstractSource* selfSources);
    virtual AbstractValueKind itemKind();

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

    virtual AbstractValue* mergeWith(AbstractValue* other);
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

    AbstractValueWithSources(AbstractValue* type = nullptr) {// NOLINT(google-explicit-constructor)
        Value = type;
        Sources = nullptr;
    }

    AbstractValueWithSources(AbstractValue* type, AbstractSource* source) {
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
        return {
                Value->mergeWith(other.Value),
                AbstractSource::combine(Sources, other.Sources)};
    }

    bool operator==(AbstractValueWithSources const& other) const {
        if (Value != other.Value) {
            return false;
        }

        if (Sources == nullptr) {
            return other.Sources == nullptr;
        } else if (other.Sources == nullptr) {
            return false;
        }

        return Sources->Sources.get() == other.Sources->Sources.get();
    }

    bool operator!=(AbstractValueWithSources const& other) const {
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
    AbstractValue* mergeWith(AbstractValue* other) override {
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
    AbstractValue* binary(AbstractSource* selfSources, int op, AbstractValueWithSources& other) override;
    AbstractValue* unary(AbstractSource* selfSources, int op) override;
    const char* describe() override;
    AbstractValueKind resolveMethod(const char* name) override;

public:
    static AbstractValue* binary(int op, AbstractValueWithSources& other);
    static bool isBig(PyObject* val) {
        if (val == nullptr)
            return false;
        return (Py_ABS(Py_SIZE(val)) > 2);
    }
};

class InternIntegerValue : public IntegerValue {
    bool isIntern() override {
        return true;
    }

public:
    InternIntegerValue() = default;
};

class BigIntegerValue : public IntegerValue {
public:
    BigIntegerValue() = default;
    AbstractValueKind kind() override {
        return AVK_BigInteger;
    }
    AbstractValue* binary(AbstractSource* selfSources, int op, AbstractValueWithSources& other) override;
    const char* describe() override {
        return "big int";
    }
    static AbstractValue* binary(int op, AbstractValueWithSources& other);
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
    AbstractValueKind resolveMethod(const char* name) override;

public:
    static AbstractValue* binary(int op, AbstractValueWithSources& other);
};

class TupleValue : public AbstractValue {
    AbstractValueKind kind() override;
    AbstractValue* binary(AbstractSource* selfSources, int op, AbstractValueWithSources& other) override;
    AbstractValue* unary(AbstractSource* selfSources, int op) override;
    const char* describe() override;
};

template<AbstractValueKind Kind>
class TupleOfValue : public TupleValue {
public:
    AbstractValueKind itemKind() override {
        return Kind;
    }
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
    AbstractValue* binary(AbstractSource* selfSources, int op, AbstractValueWithSources& other) override;
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

class ModuleValue : public AbstractValue {
    AbstractValueKind kind() override;
    AbstractValue* unary(AbstractSource* selfSources, int op) override;
    const char* describe() override;
};

class TypeValue : public AbstractValue {
    AbstractValueKind kind() override;
    AbstractValue* unary(AbstractSource* selfSources, int op) override;
    const char* describe() override;
};

class ByteArrayValue : public AbstractValue {
    AbstractValueKind kind() override;
    AbstractValue* binary(AbstractSource* selfSources, int op, AbstractValueWithSources& other) override;
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

class RangeValue : public AbstractValue {
    AbstractValueKind kind() override {
        return AVK_Range;
    }
    const char* describe() override {
        return "range";
    }
    AbstractValue* iter(AbstractSource* selfSources) override;
};

class RangeIteratorValue : public AbstractValue {
    AbstractValueKind kind() override {
        return AVK_RangeIterator;
    }
    const char* describe() override {
        return "range iterator";
    }
    AbstractValue* next(AbstractSource* selfSources) override;
};

class UnboxedRangeIteratorValue : public AbstractValue {
    AbstractValueKind kind() override {
        return AVK_UnboxedRangeIterator;
    }
    const char* describe() override {
        return "unboxed range iterator";
    }
    AbstractValue* next(AbstractSource* selfSources) override;
};

class MemoryViewValue : public AbstractValue {
    AbstractValueKind kind() override {
        return AVK_MemoryView;
    }
    const char* describe() override {
        return "memoryview";
    }
};

class ClassMethodValue : public AbstractValue {
    AbstractValueKind kind() override {
        return AVK_Classmethod;
    }
    const char* describe() override {
        return "classmethod";
    }
};

class FilterValue : public AbstractValue {
    AbstractValueKind kind() override {
        return AVK_Filter;
    }
    const char* describe() override {
        return "filter";
    }
};

class PropertyValue : public AbstractValue {
    AbstractValueKind kind() override {
        return AVK_Property;
    }
    const char* describe() override {
        return "property";
    }
};

class MapValue : public AbstractValue {
    AbstractValueKind kind() override {
        return AVK_Map;
    }
    const char* describe() override {
        return "map";
    }
};

class BaseObjectValue : public AbstractValue {
    AbstractValueKind kind() override {
        return AVK_Baseobject;
    }
    const char* describe() override {
        return "baseobject";
    }
};

class ReversedValue : public AbstractValue {
    AbstractValueKind kind() override {
        return AVK_Reversed;
    }
    const char* describe() override {
        return "reversed";
    }
};

class StaticMethodValue : public AbstractValue {
    AbstractValueKind kind() override {
        return AVK_Staticmethod;
    }
    const char* describe() override {
        return "staticmethod";
    }
};

class SuperValue : public AbstractValue {
    AbstractValueKind kind() override {
        return AVK_Super;
    }
    const char* describe() override {
        return "super";
    }
};

class ZipValue : public AbstractValue {
    AbstractValueKind kind() override {
        return AVK_Zip;
    }
    const char* describe() override {
        return "zip";
    }
};

class VolatileValue : public AbstractValue {
    PyTypeObject* _type;
    PyObject* _object;

protected:
    AbstractValueKind _kind;

public:
    VolatileValue(PyTypeObject* type, PyObject* object, AbstractValueKind kind) {
        _type = type;
        _object = object;
        _kind = kind;
    }

    AbstractValueKind kind() override;

    PyTypeObject* pythonType() override;

    bool known() override {
        return true;
    }
    PyObject* lastValue() {
        if (_PyObject_IsFreed(_object) || _object == (PyObject*) 0xFFFFFFFFFFFFFFFF)
            return nullptr;
        return _object;
    }

    const char* describe() override {
        if (_type == nullptr)
            return "volatile";
        return _PyType_Name(_type);
    }

    bool needsGuard() override {
        return true;
    }
    AbstractValue* binary(AbstractSource* selfSources, int op, AbstractValueWithSources& other) override;
};

class PgcValue : public VolatileValue {
public:
    PgcValue(PyTypeObject* type, AbstractValueKind kind) : VolatileValue(type, nullptr, kind) {}
    AbstractValueKind kind() override;
};

class ArgumentValue : public VolatileValue {
public:
    ArgumentValue(PyTypeObject* type, PyObject* object, AbstractValueKind kind) : VolatileValue(type, object, kind) {}
};

class GlobalValue : public VolatileValue {
public:
    GlobalValue(PyTypeObject* type, PyObject* object, AbstractValueKind kind) : VolatileValue(type, object, kind) {}
};

AbstractValueKind knownFunctionReturnType(AbstractValueWithSources source);

extern UndefinedValue Undefined;
extern AnyValue Any;
extern BoolValue Bool;
extern IntegerValue Integer;
extern BigIntegerValue BigInteger;
extern FloatValue Float;
extern ListValue List;
extern SetValue Set;
extern FrozenSetValue FrozenSet;
extern StringValue String;
extern BytesValue Bytes;
extern DictValue Dict;
extern NoneValue None;
extern FunctionValue Function;
extern SliceValue Slice;
extern ComplexValue Complex;
extern IterableValue Iterable;
extern ModuleValue Module;
extern TypeValue Type;
extern ByteArrayValue ByteArray;
extern MethodValue Method;
extern CodeObjectValue CodeObject;
extern EnumeratorValue Enumerator;
extern RangeIteratorValue RangeIterator;
extern UnboxedRangeIteratorValue UnboxedRangeIterator;
extern RangeValue Range;
extern MemoryViewValue MemoryView;
extern ClassMethodValue ClassMethod;
extern FilterValue Filter;
extern PropertyValue Property;
extern MapValue Map;
extern BaseObjectValue BaseObject;
extern ReversedValue Reversed;
extern StaticMethodValue StaticMethod;
extern SuperValue Super;
extern ZipValue Zip;

extern TupleOfValue<AVK_Any> Tuple;
extern TupleOfValue<AVK_Integer> TupleOfInteger;
extern TupleOfValue<AVK_BigInteger> TupleOfBigInteger;
extern TupleOfValue<AVK_Float> TupleOfFloat;
extern TupleOfValue<AVK_String> TupleOfString;

AbstractValue* avkToAbstractValue(AbstractValueKind);
AbstractValueKind GetAbstractType(PyTypeObject* type, PyObject* value = nullptr);
PyTypeObject* GetPyType(AbstractValueKind type);
#endif
