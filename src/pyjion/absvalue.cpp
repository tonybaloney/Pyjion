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

#include "absvalue.h"
#include "knownmethods.h"

AnyValue Any;
UndefinedValue Undefined;
IntegerValue Integer;
InternIntegerValue InternInteger;
FloatValue Float;
BoolValue Bool;
ListValue List;
TupleValue Tuple;
SetValue Set;
FrozenSetValue FrozenSet;
StringValue String;
BytesValue Bytes;
DictValue Dict;
NoneValue None;
FunctionValue Function;
SliceValue Slice;
ComplexValue Complex;
IterableValue Iterable;
BuiltinValue Builtin;
ModuleValue Module;
TypeValue Type;
ByteArrayValue ByteArray;
MethodValue Method;
CodeObjectValue CodeObject;
EnumeratorValue Enumerator;
FileValue File;


AbstractSource::AbstractSource() {
    Sources = shared_ptr<AbstractSources>(new AbstractSources());
    Sources->Sources.insert(this);
}

AbstractValue* AbstractValue::binary(AbstractSource* selfSources, int op, AbstractValueWithSources& other) {
    if (selfSources != nullptr) {
        selfSources->escapes();
    }
    other.escapes();

    return &Any;
}

AbstractValue* AbstractValue::unary(AbstractSource* selfSources, int op) {
    if (selfSources != nullptr) {
        selfSources->escapes();
    }
    return &Any;
}

AbstractValue* AbstractValue::compare(AbstractSource* selfSources, int op, AbstractValueWithSources& other) {
    if (isKnownType(kind()) && isKnownType(other.Value->kind()) && kind() == other.Value->kind()) {
        // We know all of the known types don't have fancy rich comparison
        // operations and will return true/false.  This is in contrast to
        // user defined types which can override the rich comparison methods
        // and return values which are not bools.
        return &Bool;
    }

    if (selfSources != nullptr) {
        selfSources->escapes();
    }
    other.escapes();
    return &Any;
}

void AbstractValue::truth(AbstractSource* selfSources) {
    if (selfSources != nullptr) {
        selfSources->escapes();
    }
}

AbstractValue* AbstractValue::mergeWith(AbstractValue*other) {
    if (this == other) {
        return this;
    }
    return &Any;
}

PyTypeObject *AbstractValue::pythonType() {
    return GetPyType(this->kind());
}

bool AbstractValue::known() {
    return isKnownType(this->kind());
}

void AbstractSource::escapes() const {
    if (Sources) {
        Sources->m_escapes = true;
    }
}

bool AbstractSource::needsBoxing() const {
    if (Sources) {
        return Sources->m_escapes;
    }
    return true;
}

AbstractSource* AbstractSource::combine(AbstractSource* one, AbstractSource* two) {
    if (one == two) {
        return one;
    }
    if (one != nullptr) {
        if (two != nullptr) {
            if (one->Sources.get() == two->Sources.get()) {
                return one;
            }

            // link the sources...
            if (one->Sources->Sources.size() > two->Sources->Sources.size()) {
                for (auto source : two->Sources->Sources) {
                    one->Sources->Sources.insert(source);
                    if (source != two) {
                        source->Sources = one->Sources;
                    }
                }
                if (two->needsBoxing() && !one->needsBoxing()) {
                    one->escapes();
                }
                two->Sources = one->Sources;
                return one;
            }
            else {
                for (auto source : one->Sources->Sources) {
                    two->Sources->Sources.insert(source);
                    if (source != one) {
                        source->Sources = two->Sources;
                    }
                }
                if (one->needsBoxing() && !two->needsBoxing()) {
                    two->escapes();
                }
                one->Sources = two->Sources;
                return two;
            }
        }
        else {
            // merging with an unknown source...
            one->escapes();
            return one;
        }
    }
    else if (two != nullptr) {
        // merging with an unknown source...
        two->escapes();
        return two;
    }
    return nullptr;
}

/*
void TupleSource::escapes() {
    for (auto cur = m_sources.begin(); cur != m_sources.end(); cur++) {
        (*cur).escapes();
    }
    m_escapes = true;
}*/

// BoolValue methods
AbstractValueKind BoolValue::kind() {
    return AVK_Bool;
}

void BoolValue::truth(AbstractSource* selfSources) {
    // bools aren't boxed, and don't escape on truth checks
}

AbstractValue* BoolValue::binary(AbstractSource* selfSources, int op, AbstractValueWithSources& other) {
    auto other_kind = other.Value->kind();
    if (other_kind == AVK_Bool) {
        switch (op) {
            case BINARY_AND:
            case BINARY_OR:
            case BINARY_XOR:
            case INPLACE_AND:
            case INPLACE_OR:
            case INPLACE_XOR:
                return this;
            case BINARY_TRUE_DIVIDE:
            case INPLACE_TRUE_DIVIDE:
            {
                if (selfSources != nullptr) {
                    selfSources->escapes();
                }
                other.escapes();
                return &Float;
            }
            case BINARY_ADD:
            case BINARY_FLOOR_DIVIDE:
            case BINARY_LSHIFT:
            case BINARY_MODULO:
            case BINARY_MULTIPLY:
            case BINARY_POWER:
            case BINARY_RSHIFT:
            case BINARY_SUBTRACT:
            case INPLACE_ADD:
            case INPLACE_FLOOR_DIVIDE:
            case INPLACE_LSHIFT:
            case INPLACE_MODULO:
            case INPLACE_MULTIPLY:
            case INPLACE_POWER:
            case INPLACE_RSHIFT:
            case INPLACE_SUBTRACT:
                return &Integer;
        }
    }
    else if (other_kind == AVK_Bytes) {
        switch (op) {
            case BINARY_MULTIPLY:
            case INPLACE_MULTIPLY:
                return &Bytes;
        }
    }
    else if (other_kind == AVK_Complex) {
        switch (op) {
            case BINARY_ADD:
            case BINARY_MULTIPLY:
            case BINARY_POWER:
            case BINARY_SUBTRACT:
            case BINARY_TRUE_DIVIDE:
            case INPLACE_ADD:
            case INPLACE_MULTIPLY:
            case INPLACE_POWER:
            case INPLACE_SUBTRACT:
            case INPLACE_TRUE_DIVIDE:
                return &Complex;
        }
    }
    else if (other_kind == AVK_Float) {
        switch (op) {
            case BINARY_ADD:
            case BINARY_FLOOR_DIVIDE:
            case BINARY_MODULO:
            case BINARY_MULTIPLY:
            case BINARY_POWER:
            case BINARY_SUBTRACT:
            case BINARY_TRUE_DIVIDE:
            case INPLACE_ADD:
            case INPLACE_FLOOR_DIVIDE:
            case INPLACE_MODULO:
            case INPLACE_MULTIPLY:
            case INPLACE_POWER:
            case INPLACE_SUBTRACT:
            case INPLACE_TRUE_DIVIDE:
            {
                if (selfSources != nullptr) {
                    selfSources->escapes();
                }
                other.escapes();
                return &Float;
            }
        }
    }
    else if (other_kind == AVK_Integer) {
        switch (op) {
            case BINARY_MODULO:
            case INPLACE_MODULO:
                return this;
            case BINARY_TRUE_DIVIDE:
            case INPLACE_TRUE_DIVIDE:
            {
                if (selfSources != nullptr) {
                    selfSources->escapes();
                }
                other.escapes();
                return &Float;
            }
            case BINARY_ADD:
            case BINARY_AND:
            case BINARY_FLOOR_DIVIDE:
            case BINARY_LSHIFT:
            case BINARY_MULTIPLY:
            case BINARY_OR:
            case BINARY_POWER:
            case BINARY_RSHIFT:
            case BINARY_SUBTRACT:
            case BINARY_XOR:
            case INPLACE_ADD:
            case INPLACE_AND:
            case INPLACE_FLOOR_DIVIDE:
            case INPLACE_LSHIFT:
            case INPLACE_MULTIPLY:
            case INPLACE_OR:
            case INPLACE_POWER:
            case INPLACE_RSHIFT:
            case INPLACE_SUBTRACT:
            case INPLACE_XOR:
                return &Integer;
        }
    }
    else if (other_kind == AVK_List) {
        switch (op) {
            case BINARY_MULTIPLY:
            case INPLACE_MULTIPLY:
                return &List;
        }
    }
    else if (other_kind == AVK_String) {
        switch (op) {
            case BINARY_MULTIPLY:
            case INPLACE_MULTIPLY:
                return &String;
        }
    }
    else if (other_kind == AVK_Tuple) {
        switch (op) {
            case BINARY_MULTIPLY:
            case INPLACE_MULTIPLY:
                return &Tuple;
        }
    }
    return AbstractValue::binary(selfSources, op, other);
}

AbstractValue* BoolValue::unary(AbstractSource* selfSources, int op) {
    switch (op) {
        case UNARY_NOT:
            return this;
        case UNARY_INVERT:
        case UNARY_NEGATIVE:
        case UNARY_POSITIVE:
            return &Integer;
    }
    return AbstractValue::unary(selfSources, op);
}

const char* BoolValue::describe() {
    return "bool";
}

// BytesValue methods
AbstractValueKind BytesValue::kind() {
    return AVK_Bytes;
}

AbstractValue* BytesValue::binary(AbstractSource* selfSources, int op, AbstractValueWithSources& other) {
    auto other_kind = other.Value->kind();
    if (other_kind == AVK_Bool) {
        switch (op) {
            case BINARY_MULTIPLY:
            case INPLACE_MULTIPLY:
                other.escapes();
                return this;
        }
    }
    else if (other_kind == AVK_Bytes) {
        switch (op) {
            case BINARY_ADD:
            case BINARY_MODULO:
            case INPLACE_ADD:
            case INPLACE_MODULO:
                return this;
        }
    }
    else if (other_kind == AVK_Dict) {
        switch (op) {
            case BINARY_MODULO:
            case INPLACE_MODULO:
                return this;
        }
    }
    else if (other_kind == AVK_Integer) {
        switch (op) {
            case BINARY_MULTIPLY:
            case INPLACE_MULTIPLY:
                other.escapes();
                return this;
            case BINARY_SUBSCR:
                other.escapes();
                return &Integer;
        }
    }
    else if (other_kind == AVK_List) {
        switch (op) {
            case BINARY_MODULO:
            case INPLACE_MODULO:
                return this;
        }
    }
    else if (other_kind == AVK_Slice) {
        switch (op) {
            case BINARY_SUBSCR:
                return this;
        }
    }
    else if (other_kind == AVK_Tuple) {
        switch (op) {
            case BINARY_MODULO:
            case INPLACE_MODULO:
                return this;
        }
    }
    return AbstractValue::binary(selfSources, op, other);
}

AbstractValue* BytesValue::unary(AbstractSource* selfSources, int op) {
    switch (op) {
        case UNARY_NOT:
            return &Bool;
    }
    return AbstractValue::unary(selfSources, op);
}

const char* BytesValue::describe() {
    return "bytes";
}

AbstractValueKind BytesValue::resolveMethod(const char *name) {
    for (auto const &b: bytesMethodReturnTypes){
        if (strcmp(name, b.first) == 0)
            return b.second;
    }
    return AVK_Any;
}

// ComplexValue methods
AbstractValueKind ComplexValue::kind() {
    return AVK_Complex;
}

AbstractValue* ComplexValue::binary(AbstractSource* selfSources, int op, AbstractValueWithSources& other) {
    auto other_kind = other.Value->kind();
    if (other_kind == AVK_Bool) {
        switch (op) {
            case BINARY_ADD:
            case BINARY_MULTIPLY:
            case BINARY_POWER:
            case BINARY_SUBTRACT:
            case BINARY_TRUE_DIVIDE:
            case INPLACE_ADD:
            case INPLACE_MULTIPLY:
            case INPLACE_POWER:
            case INPLACE_SUBTRACT:
            case INPLACE_TRUE_DIVIDE:
                return this;
        }
    }
    else if (other_kind == AVK_Complex) {
        switch (op) {
            case BINARY_ADD:
            case BINARY_MULTIPLY:
            case BINARY_POWER:
            case BINARY_SUBTRACT:
            case BINARY_TRUE_DIVIDE:
            case INPLACE_ADD:
            case INPLACE_MULTIPLY:
            case INPLACE_POWER:
            case INPLACE_SUBTRACT:
            case INPLACE_TRUE_DIVIDE:
                return this;
        }
    }
    else if (other_kind == AVK_Float) {
        switch (op) {
            case BINARY_ADD:
            case BINARY_MULTIPLY:
            case BINARY_POWER:
            case BINARY_SUBTRACT:
            case BINARY_TRUE_DIVIDE:
            case INPLACE_ADD:
            case INPLACE_MULTIPLY:
            case INPLACE_POWER:
            case INPLACE_SUBTRACT:
            case INPLACE_TRUE_DIVIDE:
                return this;
        }
    }
    else if (other_kind == AVK_Integer) {
        switch (op) {
            case BINARY_ADD:
            case BINARY_MULTIPLY:
            case BINARY_POWER:
            case BINARY_SUBTRACT:
            case BINARY_TRUE_DIVIDE:
            case INPLACE_ADD:
            case INPLACE_MULTIPLY:
            case INPLACE_POWER:
            case INPLACE_SUBTRACT:
            case INPLACE_TRUE_DIVIDE:
                return this;
        }
    }
    return AbstractValue::binary(selfSources, op, other);
}

AbstractValue* ComplexValue::unary(AbstractSource* selfSources, int op) {
    switch (op) {
        case UNARY_NOT:
            return &Bool;
        case UNARY_NEGATIVE:
        case UNARY_POSITIVE:
            return this;
    }
    return AbstractValue::unary(selfSources, op);
}

const char* ComplexValue::describe() {
    return "complex";
}

// IntegerValue methods
AbstractValueKind IntegerValue::kind() {
    return AVK_Integer;
}

AbstractValue* IntegerValue::binary(AbstractSource* selfSources, int op, AbstractValueWithSources& other) {
    auto other_kind = other.Value->kind();
    if (other_kind == AVK_Bool) {
        switch (op) {
            case BINARY_TRUE_DIVIDE:
            case INPLACE_TRUE_DIVIDE:
            {
                if (selfSources != nullptr) {
                    selfSources->escapes();
                }
                other.escapes();
                return &Float;
            }
            case BINARY_ADD:
            case BINARY_AND:
            case BINARY_FLOOR_DIVIDE:
            case BINARY_LSHIFT:
            case BINARY_MODULO:
            case BINARY_MULTIPLY:
            case BINARY_OR:
            case BINARY_POWER:
            case BINARY_RSHIFT:
            case BINARY_SUBTRACT:
            case BINARY_XOR:
            case INPLACE_ADD:
            case INPLACE_AND:
            case INPLACE_FLOOR_DIVIDE:
            case INPLACE_LSHIFT:
            case INPLACE_MODULO:
            case INPLACE_MULTIPLY:
            case INPLACE_OR:
            case INPLACE_POWER:
            case INPLACE_RSHIFT:
            case INPLACE_SUBTRACT:
            case INPLACE_XOR:
				if (selfSources != nullptr) {
					selfSources->escapes();
				}
				other.escapes();
				return this;
        }
    }
    else if (other_kind == AVK_Bytes) {
        switch (op) {
            case BINARY_MULTIPLY:
            case INPLACE_MULTIPLY:
                return &Bytes;
        }
    }
    else if (other_kind == AVK_Complex) {
        switch (op) {
            case BINARY_ADD:
            case BINARY_MULTIPLY:
            case BINARY_POWER:
            case BINARY_SUBTRACT:
            case BINARY_TRUE_DIVIDE:
            case INPLACE_ADD:
            case INPLACE_MULTIPLY:
            case INPLACE_POWER:
            case INPLACE_SUBTRACT:
            case INPLACE_TRUE_DIVIDE:
                return &Complex;
        }
    }
    else if (other_kind == AVK_Float) {
        switch (op) {
            case BINARY_ADD:
            case BINARY_FLOOR_DIVIDE:
            case BINARY_MODULO:
            case BINARY_MULTIPLY:
            case BINARY_POWER:
            case BINARY_SUBTRACT:
            case BINARY_TRUE_DIVIDE:
            case INPLACE_ADD:
            case INPLACE_FLOOR_DIVIDE:
            case INPLACE_MODULO:
            case INPLACE_MULTIPLY:
            case INPLACE_POWER:
            case INPLACE_SUBTRACT:
            case INPLACE_TRUE_DIVIDE:
            {
                if (selfSources != nullptr) {
                    selfSources->escapes();
                }
                other.escapes();
                return &Float;
            }
        }
    }
    else if (other_kind == AVK_Integer) {
        switch (op) {
            case BINARY_TRUE_DIVIDE:
            case INPLACE_TRUE_DIVIDE:
            {
                if (selfSources != nullptr) {
                    selfSources->escapes();
                }
                other.escapes();
                return &Float;
            }
            case BINARY_ADD:
            case BINARY_AND:
            case BINARY_FLOOR_DIVIDE:
            case BINARY_LSHIFT:
            case BINARY_MODULO:
            case BINARY_MULTIPLY:
            case BINARY_OR:
            case BINARY_POWER:
            case BINARY_RSHIFT:
            case BINARY_SUBTRACT:
            case BINARY_XOR:
            case INPLACE_ADD:
            case INPLACE_AND:
            case INPLACE_FLOOR_DIVIDE:
            case INPLACE_LSHIFT:
            case INPLACE_MODULO:
            case INPLACE_MULTIPLY:
            case INPLACE_OR:
            case INPLACE_POWER:
            case INPLACE_RSHIFT:
            case INPLACE_SUBTRACT:
            case INPLACE_XOR:
                return this;
        }
    }
    else if (other_kind == AVK_List) {
        switch (op) {
            case BINARY_MULTIPLY:
            case INPLACE_MULTIPLY:
                return &List;
        }
    }
    else if (other_kind == AVK_String) {
        switch (op) {
            case BINARY_MULTIPLY:
            case INPLACE_MULTIPLY:
                return &String;
        }
    }
    else if (other_kind == AVK_Tuple) {
        switch (op) {
            case BINARY_MULTIPLY:
            case INPLACE_MULTIPLY:
                return &Tuple;
        }
    }
    return AbstractValue::binary(selfSources, op, other);
}

AbstractValue* IntegerValue::unary(AbstractSource* selfSources, int op) {
    switch (op) {
        case UNARY_NOT:
            return &Bool;
        case UNARY_INVERT:
        case UNARY_NEGATIVE:
        case UNARY_POSITIVE:
            return this;
    }
    return AbstractValue::unary(selfSources, op);
}

void IntegerValue::truth(AbstractSource* selfSources) {
    // ints don't escape on truth checks...
}

const char* IntegerValue::describe() {
    return "int";
}

AbstractValueKind IntegerValue::resolveMethod(const char *name) {
    for (auto const &b: intMethodReturnTypes){
        if (strcmp(name, b.first) == 0)
            return b.second;
    }
    return AVK_Any;
}

// StringValue methods
AbstractValueKind StringValue::kind() {
    return AVK_String;
}

AbstractValue* StringValue::binary(AbstractSource* selfSources, int op, AbstractValueWithSources& other) {
    // String interpolation always returns a `str` (when successful).
    if (op == BINARY_MODULO || op == INPLACE_MODULO) {
		other.escapes();
        return this;
    }

    auto other_kind = other.Value->kind();
    if (other_kind == AVK_Bool) {
        switch (op) {
            case BINARY_MULTIPLY:
            case INPLACE_MULTIPLY:
                other.escapes();
                return this;
        }
    }
    else if (other_kind == AVK_Integer) {
        switch (op) {
            case BINARY_MULTIPLY:
            case BINARY_SUBSCR:
            case INPLACE_MULTIPLY:
                other.escapes();
                return this;
        }
    }
    else if (other_kind == AVK_Slice) {
        switch (op) {
            case BINARY_SUBSCR:
                other.escapes();
                return this;
        }
    }
    else if (other_kind == AVK_String) {
        switch (op) {
            case BINARY_ADD:
            case INPLACE_ADD:
                other.escapes();
                return this;
        }
    }
    return AbstractValue::binary(selfSources, op, other);
}

AbstractValue* StringValue::unary(AbstractSource* selfSources, int op) {
    switch (op) {
        case UNARY_NOT:
            return &Bool;
    }
    return AbstractValue::unary(selfSources, op);
}

const char* StringValue::describe() {
    return "str";
}

AbstractValueKind StringValue::resolveMethod(const char *name) {
    for (auto const &b: stringMethodReturnTypes){
        if (strcmp(name, b.first) == 0)
            return b.second;
    }
    return AVK_Any;
}

// FloatValue methods
AbstractValueKind FloatValue::kind() {
    return AVK_Float;
}

void FloatValue::truth(AbstractSource* selfSources) {
    // floats don't escape on truth checks...
}

AbstractValue* FloatValue::binary(AbstractSource* selfSources, int op, AbstractValueWithSources& other) {
    auto other_kind = other.Value->kind();
    if (other_kind == AVK_Bool) {
        switch (op) {
            case BINARY_ADD:
            case BINARY_FLOOR_DIVIDE:
            case BINARY_MODULO:
            case BINARY_MULTIPLY:
            case BINARY_POWER:
            case BINARY_SUBTRACT:
            case BINARY_TRUE_DIVIDE:
            case INPLACE_ADD:
            case INPLACE_FLOOR_DIVIDE:
            case INPLACE_MODULO:
            case INPLACE_MULTIPLY:
            case INPLACE_POWER:
            case INPLACE_SUBTRACT:
            case INPLACE_TRUE_DIVIDE:
                if (selfSources != nullptr) {
                    selfSources->escapes();
                }
                other.escapes();
                return this;
        }
    }
    else if (other_kind == AVK_Complex) {
        switch (op) {
            case BINARY_ADD:
            case BINARY_MULTIPLY:
            case BINARY_POWER:
            case BINARY_SUBTRACT:
            case BINARY_TRUE_DIVIDE:
            case INPLACE_ADD:
            case INPLACE_MULTIPLY:
            case INPLACE_POWER:
            case INPLACE_SUBTRACT:
            case INPLACE_TRUE_DIVIDE:
                if (selfSources != nullptr) {
                    selfSources->escapes();
                }
                other.escapes();
                return &Complex;
        }
    }
    else if (other_kind == AVK_Float) {
        switch (op) {
            case BINARY_ADD:
            case BINARY_FLOOR_DIVIDE:
            case BINARY_MODULO:
            case BINARY_MULTIPLY:
            case BINARY_POWER:
            case BINARY_SUBTRACT:
            case BINARY_TRUE_DIVIDE:
            case INPLACE_ADD:
            case INPLACE_FLOOR_DIVIDE:
            case INPLACE_MODULO:
            case INPLACE_MULTIPLY:
            case INPLACE_POWER:
            case INPLACE_SUBTRACT:
            case INPLACE_TRUE_DIVIDE:
                return this;
        }
    }
    else if (other_kind == AVK_Integer) {
        switch (op) {
            case BINARY_ADD:
            case BINARY_FLOOR_DIVIDE:
            case BINARY_MODULO:
            case BINARY_MULTIPLY:
            case BINARY_POWER:
            case BINARY_SUBTRACT:
            case BINARY_TRUE_DIVIDE:
            case INPLACE_ADD:
            case INPLACE_FLOOR_DIVIDE:
            case INPLACE_MODULO:
            case INPLACE_MULTIPLY:
            case INPLACE_POWER:
            case INPLACE_SUBTRACT:
            case INPLACE_TRUE_DIVIDE:
                return this;
        }
    }
    return AbstractValue::binary(selfSources, op, other);
}

AbstractValue* FloatValue::unary(AbstractSource* selfSources, int op) {
    switch (op) {
        case UNARY_NOT:
            return &Bool;
        case UNARY_NEGATIVE:
        case UNARY_POSITIVE:
            return this;
    }
    return AbstractValue::unary(selfSources, op);
}

const char* FloatValue::describe() {
    return "float";
}

// TupleValue methods
AbstractValueKind TupleValue::kind() {
    return AVK_Tuple;
}

AbstractValue* TupleValue::binary(AbstractSource* selfSources, int op, AbstractValueWithSources& other) {
    auto other_kind = other.Value->kind();
    if (other_kind == AVK_Bool) {
        switch (op) {
            case BINARY_MULTIPLY:
            case INPLACE_MULTIPLY:
                return this;
        }
    }
    else if (other_kind == AVK_Integer) {
        switch (op) {
            case BINARY_MULTIPLY:
            case INPLACE_MULTIPLY:
                return this;
        }
    }
    else if (other_kind == AVK_Slice) {
        switch (op) {
            case BINARY_SUBSCR:
                return this;
        }
    }
    else if (other_kind == AVK_Tuple) {
        switch (op) {
            case BINARY_ADD:
            case INPLACE_ADD:
                return this;
        }
    }
    return AbstractValue::binary(selfSources, op, other);
}

AbstractValue* TupleValue::unary(AbstractSource* selfSources, int op) {
    switch (op) {
        case UNARY_NOT:
            return &Bool;
    }
    return AbstractValue::unary(selfSources, op);
}

const char* TupleValue::describe() {
    return "tuple";
}

// ListValue methods
AbstractValueKind ListValue::kind() {
    return AVK_List;
}

AbstractValue* ListValue::binary(AbstractSource* selfSources, int op, AbstractValueWithSources& other) {
    auto other_kind = other.Value->kind();
    if (other_kind == AVK_Bool) {
        switch (op) {
            case BINARY_MULTIPLY:
            case INPLACE_MULTIPLY:
                return this;
        }
    }
    else if (other_kind == AVK_Bytes) {
        switch (op) {
            case INPLACE_ADD:
                return this;
        }
    }
    else if (other_kind == AVK_Dict) {
        switch (op) {
            case INPLACE_ADD:
                return this;
        }
    }
    else if (other_kind == AVK_Integer) {
        switch (op) {
            case BINARY_MULTIPLY:
            case INPLACE_MULTIPLY:
                return this;
        }
    }
    else if (other_kind == AVK_List) {
        switch (op) {
            case BINARY_ADD:
            case INPLACE_ADD:
                return this;
        }
    }
    else if (other_kind == AVK_Set) {
        switch (op) {
            case INPLACE_ADD:
                return this;
        }
    }
    else if (other_kind == AVK_Slice) {
        switch (op) {
            case BINARY_SUBSCR:
                return this;
        }
    }
    else if (other_kind == AVK_String) {
        switch (op) {
            case INPLACE_ADD:
                return this;
        }
    }
    else if (other_kind == AVK_Tuple) {
        switch (op) {
            case INPLACE_ADD:
                return this;
        }
    }
    return AbstractValue::binary(selfSources, op, other);
}

AbstractValue* ListValue::unary(AbstractSource* selfSources, int op) {
    switch (op) {
        case UNARY_NOT:
            return &Bool;
    }
    return AbstractValue::unary(selfSources, op);
}

const char* ListValue::describe() {
    return "list";
}


AbstractValueKind ListValue::resolveMethod(const char *name) {
    for (auto const &b: listMethodReturnTypes){
        if (strcmp(name, b.first) == 0)
            return b.second;
    }
    return AVK_Any;
}

// DictValue methods
AbstractValueKind DictValue::kind() {
    return AVK_Dict;
}

AbstractValue* DictValue::unary(AbstractSource* selfSources, int op) {
    switch (op) {
        case UNARY_NOT:
            return &Bool;
    }
    return AbstractValue::unary(selfSources, op);
}

const char* DictValue::describe() {
    return "dict";
}

AbstractValueKind DictValue::resolveMethod(const char *name) {
    for (auto const &b: dictMethodReturnTypes){
        if (strcmp(name, b.first) == 0)
            return b.second;
    }
    return AVK_Any;
}


// SetValue methods
AbstractValueKind SetValue::kind() {
    return AVK_Set;
}

AbstractValue* SetValue::binary(AbstractSource* selfSources, int op, AbstractValueWithSources& other) {
    auto other_kind = other.Value->kind();
    if (other_kind == AVK_Set) {
        switch (op) {
            case BINARY_AND:
            case BINARY_OR:
            case BINARY_SUBTRACT:
            case BINARY_XOR:
            case INPLACE_AND:
            case INPLACE_OR:
            case INPLACE_SUBTRACT:
            case INPLACE_XOR:
                return this;
        }
    }
    return AbstractValue::binary(selfSources, op, other);
}

AbstractValue* SetValue::unary(AbstractSource* selfSources, int op) {
    switch (op) {
        case UNARY_NOT:
            return &Bool;
    }
    return AbstractValue::unary(selfSources, op);
}

const char* SetValue::describe() {
    return "set";
}

// FrozenSetValue methods
AbstractValueKind FrozenSetValue::kind() {
    return AVK_FrozenSet;
}

AbstractValue* FrozenSetValue::binary(AbstractSource* selfSources, int op, AbstractValueWithSources& other) {
    auto other_kind = other.Value->kind();
    if (other_kind == AVK_Set || other_kind == AVK_FrozenSet) {
        switch (op) {
            case BINARY_AND:
            case BINARY_OR:
            case BINARY_SUBTRACT:
            case BINARY_XOR:
            case INPLACE_AND:
            case INPLACE_OR:
            case INPLACE_SUBTRACT:
            case INPLACE_XOR:
                return this;
        }
    }
    return AbstractValue::binary(selfSources, op, other);
}

AbstractValue* FrozenSetValue::unary(AbstractSource* selfSources, int op) {
    switch (op) {
        case UNARY_NOT:
            return &Bool;
    }
    return AbstractValue::unary(selfSources, op);
}

const char* FrozenSetValue::describe() {
    return "frozenset";
}

// NoneValue methods
AbstractValueKind NoneValue::kind() {
    return AVK_None;
}

AbstractValue* NoneValue::unary(AbstractSource* selfSources, int op) {
    switch (op) {
        case UNARY_NOT:
            return &Bool;
    }
    return AbstractValue::unary(selfSources, op);
}

const char* NoneValue::describe() {
    return "None";
}

// FunctionValue methods
AbstractValueKind FunctionValue::kind() {
    return AVK_Function;
}

AbstractValue* FunctionValue::unary(AbstractSource* selfSources, int op) {
    switch (op) {
        case UNARY_NOT:
            return &Bool;
    }
    return AbstractValue::unary(selfSources, op);
}

const char* FunctionValue::describe() {
    return "function";
}

// SliceValue methods
AbstractValueKind SliceValue::kind() {
    return AVK_Slice;
}
AbstractValue* SliceValue::unary(AbstractSource* selfSources, int op) {
    switch (op) {
        case UNARY_NOT:
            return &Bool;
    }
    return AbstractValue::unary(selfSources, op);
}
const char* SliceValue::describe() {
    return "slice";
}

// Iterable methods
AbstractValueKind IterableValue::kind() {
    return AVK_Iterable;
}
AbstractValue* IterableValue::unary(AbstractSource* selfSources, int op) {
    return AbstractValue::unary(selfSources, op);
}
const char* IterableValue::describe() {
    return "iterable";
}

// Builtin methods
AbstractValueKind BuiltinValue::kind() {
    return AVK_Function;
}
AbstractValue* BuiltinValue::unary(AbstractSource* selfSources, int op) {
    return AbstractValue::unary(selfSources, op);
}
const char* BuiltinValue::describe() {
    return "builtin";
}

// Builtin methods
AbstractValueKind ModuleValue::kind(){
    return AVK_Module;
}
AbstractValue *ModuleValue::unary(AbstractSource *selfSources, int op){
    return AbstractValue::unary(selfSources, op);
}
const char *ModuleValue::describe(){
    return "module";
}


// Type methods
AbstractValueKind TypeValue::kind() {
    return AVK_Type;
}

AbstractValue *TypeValue::unary(AbstractSource *selfSources, int op) {
    return AbstractValue::unary(selfSources, op);
}

const char *TypeValue::describe() {
    return "type";
}

// ByteArray methods
AbstractValueKind ByteArrayValue::kind() {
    return AVK_Bytearray;
}

AbstractValue *ByteArrayValue::unary(AbstractSource *selfSources, int op) {
    if (op == UNARY_NOT)
        return &Bool;
    return AbstractValue::unary(selfSources, op);
}

const char *ByteArrayValue::describe() {
    return "bytearray";
}

AbstractValueKind ByteArrayValue::resolveMethod(const char *name) {
    for (auto const &b: bytearrayMethodReturnTypes){
        if (strcmp(name, b.first) == 0)
            return b.second;
    }
    return AVK_Any;
}

// Method methods
AbstractValueKind MethodValue::kind() {
    return AVK_Method;
}

AbstractValue *MethodValue::unary(AbstractSource *selfSources, int op) {
    return AbstractValue::unary(selfSources, op);
}

const char *MethodValue::describe() {
    return "method";
}

/* Enumerator Value */
AbstractValueKind EnumeratorValue::kind() {
    return AVK_Enumerate;
}

AbstractValue *EnumeratorValue::unary(AbstractSource *selfSources, int op) {
    return AbstractValue::unary(selfSources, op);
}

const char *EnumeratorValue::describe() {
    return "enumerator";
}

/* File Value */
AbstractValueKind FileValue::kind() {
    return AVK_File;
}

AbstractValue *FileValue::unary(AbstractSource *selfSources, int op) {
    return AbstractValue::unary(selfSources, op);
}

const char *FileValue::describe() {
    return "file";
}

/* Code Object Value */
AbstractValueKind CodeObjectValue::kind() {
    return AVK_Code;
}

AbstractValue *CodeObjectValue::unary(AbstractSource *selfSources, int op) {
    return AbstractValue::unary(selfSources, op);
}

const char *CodeObjectValue::describe() {
    return "codeobject";
}

AbstractValueKind knownFunctionReturnType(AbstractValueWithSources source){
    // IS this a builtin?
    if (source.hasSource() && source.Sources->isBuiltin())
    {
        auto globalSource = dynamic_cast<BuiltinSource*>(source.Sources);
        for (auto const &b: builtinReturnTypes){
            if (strcmp(globalSource->getName(), b.first) == 0)
                return b.second;
        }
    }
    return AVK_Any;
}

AbstractValue* avkToAbstractValue(AbstractValueKind kind){
    switch (kind) {
        case AVK_Any:
            return &Any;
        case AVK_Undefined:
            return &Any;
        case AVK_Integer:
            return &Integer;
        case AVK_Float:
            return &Float;
        case AVK_Bool:
            return &Bool;
        case AVK_List:
            return &List;
        case AVK_Dict:
            return &Dict;
        case AVK_Tuple:
            return &Tuple;
        case AVK_Set:
            return &Set;
        case AVK_FrozenSet:
            return &FrozenSet;
        case AVK_String:
            return &String;
        case AVK_Bytes:
            return &Bytes;
        case AVK_Bytearray:
            return &ByteArray;
        case AVK_None:
            return &None;
        case AVK_Function:
            return &Function;
        case AVK_Slice:
            return &Slice;
        case AVK_Complex:
            return &Complex;
        case AVK_Iterable:
            return &Iterable;
        case AVK_Code:
            return &CodeObject;
        case AVK_Enumerate:
            return &Enumerator;
        case AVK_File:
            return &File;
        case AVK_Type:
            return &Type;
        case AVK_Module:
            return &Module;

        default:
            return &Any;
    }
}

AbstractValueKind GetAbstractType(PyTypeObject* type) {
    if (type == nullptr) {
        return AVK_Any;
    } else if (type == &PyLong_Type) {
        return AVK_Integer;
    }
    else if (type == &PyFloat_Type) {
        return AVK_Float;
    }
    else if (type == &PyDict_Type) {
        return AVK_Dict;
    }
    else if (type == &PyTuple_Type) {
        return AVK_Tuple;
    }
    else if (type == &PyList_Type) {
        return AVK_List;
    }
    else if (type == &PyBool_Type) {
        return AVK_Bool;
    }
    else if (type == &PyUnicode_Type) {
        return AVK_String;
    }
    else if (type == &PyBytes_Type) {
        return AVK_Bytes;
    }
    else if (type == &PySet_Type) {
        return AVK_Set;
    }
    else if (type == &PyFrozenSet_Type) {
        return AVK_FrozenSet;
    }
    else if (type == &_PyNone_Type) {
        return AVK_None;
    }
    else if (type == &PyFunction_Type || type == &PyCFunction_Type) {
        return AVK_Function;
    }
    else if (type == &PySlice_Type) {
        return AVK_Slice;
    }
    else if (type == &PyComplex_Type) {
        return AVK_Complex;
    }
    else if (type == &PyType_Type) {
        return AVK_Type;
    }
    else if (type == &PyEnum_Type) {
        return AVK_Enumerate;
    }
    else if (type == &PyCode_Type) {
        return AVK_Code;
    }
    return AVK_Any;
}

PyTypeObject* GetPyType(AbstractValueKind type) {
    switch (type) {
        case AVK_Any: return &PyType_Type;
        case AVK_Integer: return &PyLong_Type;
        case AVK_Float: return &PyFloat_Type;
        case AVK_Dict: return &PyDict_Type;
        case AVK_Tuple: return &PyTuple_Type;
        case AVK_List: return &PyList_Type;
        case AVK_Bool: return &PyBool_Type;
        case AVK_String: return &PyUnicode_Type;
        case AVK_Bytes: return &PyBytes_Type;
        case AVK_Set: return &PySet_Type;
        case AVK_FrozenSet: return &PyFrozenSet_Type;
        case AVK_None: return &_PyNone_Type;
        case AVK_Function: return &PyFunction_Type;
        case AVK_Slice: return &PySlice_Type;
        case AVK_Complex: return &PyComplex_Type;
        case AVK_Type: return &PyType_Type;
        case AVK_Enumerate: return &PyEnum_Type;
        case AVK_Code: return &PyCode_Type;
        case AVK_Bytearray: return &PyByteArray_Type;
        default:
            return nullptr;
    }
}

PyTypeObject* PgcValue::pythonType() {
    return this->_type;
}

AbstractValueKind PgcValue::kind() {
    return GetAbstractType(this->_type);
}

PyTypeObject* ArgumentValue::pythonType() {
    return this->_type;
}

AbstractValueKind ArgumentValue::kind() {
    return GetAbstractType(this->_type);
}