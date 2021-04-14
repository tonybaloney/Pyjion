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

#ifndef PYJION_PYCOMP_H
#define PYJION_PYCOMP_H


#include <windows.h>

#include <share.h>
#include <intrin.h>
#include <climits>
#include <cfloat>

#include "ipycomp.h"
#include "jitinfo.h"
#include "codemodel.h"
#include "ilgen.h"
#include "intrins.h"
#include "absint.h"
#include "disasm.h"

// binary operator helpers
#define METHOD_ADD_TOKEN                         0x00000000
#define METHOD_MULTIPLY_TOKEN                    0x00000001
#define METHOD_SUBTRACT_TOKEN                    0x00000002
#define METHOD_DIVIDE_TOKEN                      0x00000003
#define METHOD_FLOORDIVIDE_TOKEN                 0x00000004
#define METHOD_POWER_TOKEN                       0x00000005
#define METHOD_MODULO_TOKEN                      0x00000006
#define METHOD_SUBSCR_TOKEN                      0x00000007
#define METHOD_STOREMAP_TOKEN                    0x00000008
#define METHOD_RICHCMP_TOKEN                     0x00000009
#define METHOD_CONTAINS_TOKEN                    0x0000000A
#define METHOD_NOTCONTAINS_TOKEN                 0x0000000B
#define METHOD_STORESUBSCR_TOKEN                 0x0000000C
#define METHOD_DELETESUBSCR_TOKEN                0x0000000D
#define METHOD_NEWFUNCTION_TOKEN                 0x0000000E
#define METHOD_GETITER_TOKEN                     0x0000000F
#define METHOD_DECREF_TOKEN                      0x00000010
#define METHOD_GETBUILDCLASS_TOKEN               0x00000011
#define METHOD_LOADNAME_TOKEN                    0x00000012
#define METHOD_STORENAME_TOKEN                   0x00000013
#define METHOD_SEQUENCE_AS_LIST                  0x00000014
#define METHOD_LIST_ITEM_FROM_BACK               0x00000015
#define METHOD_DELETENAME_TOKEN                  0x00000016
#define METHOD_PYCELL_SET_TOKEN                  0x00000017
#define METHOD_SET_CLOSURE                       0x00000018
#define METHOD_BUILD_SLICE                       0x00000019
#define METHOD_UNARY_POSITIVE                    0x0000001A
#define METHOD_UNARY_NEGATIVE                    0x0000001B
#define METHOD_UNARY_NOT                         0x0000001C
#define METHOD_UNARY_INVERT                      0x0000001D
#define METHOD_MATRIX_MULTIPLY_TOKEN             0x0000001E
#define METHOD_BINARY_LSHIFT_TOKEN               0x0000001F
#define METHOD_BINARY_RSHIFT_TOKEN               0x00000020
#define METHOD_BINARY_AND_TOKEN                  0x00000021
#define METHOD_BINARY_XOR_TOKEN                  0x00000022
#define METHOD_BINARY_OR_TOKEN                   0x00000023
#define METHOD_LIST_APPEND_TOKEN                 0x00000024
#define METHOD_SET_ADD_TOKEN                     0x00000025
#define METHOD_INPLACE_POWER_TOKEN               0x00000026
#define METHOD_INPLACE_MULTIPLY_TOKEN            0x00000027
#define METHOD_INPLACE_MATRIX_MULTIPLY_TOKEN     0x00000028
#define METHOD_INPLACE_TRUE_DIVIDE_TOKEN         0x00000029
#define METHOD_INPLACE_FLOOR_DIVIDE_TOKEN        0x0000002A
#define METHOD_INPLACE_MODULO_TOKEN              0x0000002B
#define METHOD_INPLACE_ADD_TOKEN                 0x0000002C
#define METHOD_INPLACE_SUBTRACT_TOKEN            0x0000002D
#define METHOD_INPLACE_LSHIFT_TOKEN              0x0000002E
#define METHOD_INPLACE_RSHIFT_TOKEN              0x0000002F
#define METHOD_INPLACE_AND_TOKEN                 0x00000030
#define METHOD_INPLACE_XOR_TOKEN                 0x00000031
#define METHOD_INPLACE_OR_TOKEN                  0x00000032
#define METHOD_MAP_ADD_TOKEN                     0x00000033
#define METHOD_PRINT_EXPR_TOKEN                  0x00000034
#define METHOD_LOAD_CLASSDEREF_TOKEN             0x00000035
#define METHOD_PREPARE_EXCEPTION                 0x00000036
#define METHOD_DO_RAISE                          0x00000037
#define METHOD_EH_TRACE                          0x00000038
#define METHOD_COMPARE_EXCEPTIONS                0x00000039
#define METHOD_UNBOUND_LOCAL                     0x0000003A
#define METHOD_DEBUG_TRACE                       0x0000003B
#define METHOD_DEBUG_PTR                         0x0000003C
#define METHOD_DEBUG_TYPE                        0x0000003D
#define METHOD_DEBUG_PYOBJECT                    0x0000003E
#define METHOD_UNWIND_EH                         0x0000003F
#define METHOD_PY_PUSHFRAME                      0x00000041
#define METHOD_PY_POPFRAME                       0x00000042
#define METHOD_PY_IMPORTNAME                     0x00000043

#define METHOD_PY_IMPORTFROM                     0x00000045
#define METHOD_PY_IMPORTSTAR                     0x00000046
#define METHOD_IS                                0x00000049
#define METHOD_ISNOT                             0x0000004A
#define METHOD_IS_BOOL                           0x0000004B
#define METHOD_ISNOT_BOOL                        0x0000004C

#define METHOD_UNARY_NOT_INT                     0x00000051
#define METHOD_FLOAT_FROM_DOUBLE                 0x00000053
#define METHOD_BOOL_FROM_LONG                    0x00000054
#define METHOD_PYERR_SETSTRING                   0x00000055
#define METHOD_NUMBER_AS_SSIZET                  0x00000056

#define METHOD_EQUALS_INT_TOKEN                  0x00000065
#define METHOD_LESS_THAN_INT_TOKEN               0x00000066
#define METHOD_LESS_THAN_EQUALS_INT_TOKEN        0x00000067
#define METHOD_NOT_EQUALS_INT_TOKEN              0x00000068
#define METHOD_GREATER_THAN_INT_TOKEN            0x00000069
#define METHOD_GREATER_THAN_EQUALS_INT_TOKEN     0x0000006A

#define METHOD_EXTENDLIST_TOKEN                  0x0000006C
#define METHOD_LISTTOTUPLE_TOKEN                 0x0000006D
#define METHOD_SETUPDATE_TOKEN                   0x0000006E
#define METHOD_DICTUPDATE_TOKEN                  0x0000006F

#define METHOD_INT_TO_FLOAT                      0x00000072

#define METHOD_STOREMAP_NO_DECREF_TOKEN          0x00000073
#define METHOD_FORMAT_VALUE                      0x00000074
#define METHOD_FORMAT_OBJECT                     0x00000075
#define METHOD_BUILD_DICT_FROM_TUPLES            0x00000076
#define METHOD_DICT_MERGE                        0x00000077
#define METHOD_SETUP_ANNOTATIONS                 0x00000078
#define METHOD_DEALLOC_OBJECT                    0x00000079
#define METHOD_LOAD_CLOSURE                      0x0000007A
#define METHOD_TRIPLE_BINARY_OP                  0x0000007B
#define METHOD_XXXXXXXXXXXXXX                    0x0000007C
#define METHOD_LOADNAME_HASH                     0x0000007D
#define METHOD_LOADGLOBAL_HASH                   0x0000007E
#define METHOD_PENDING_CALLS                     0x0000007F

// call helpers
#define METHOD_CALL_0_TOKEN        0x00010000
#define METHOD_CALL_1_TOKEN        0x00010001
#define METHOD_CALL_2_TOKEN        0x00010002
#define METHOD_CALL_3_TOKEN        0x00010003
#define METHOD_CALL_4_TOKEN        0x00010004
#define METHOD_CALL_5_TOKEN        0x00010005
#define METHOD_CALL_6_TOKEN        0x00010006
#define METHOD_CALL_7_TOKEN        0x00010007
#define METHOD_CALL_8_TOKEN        0x00010008
#define METHOD_CALL_9_TOKEN        0x00010009
#define METHOD_CALL_10_TOKEN       0x0001000A
#define METHOD_CALLN_TOKEN         0x000100FF
#define METHOD_VECTORCALL          0x00010100
#define METHOD_OBJECTCALL          0x00010101

#define METHOD_METHCALL_0_TOKEN    0x00011000
#define METHOD_METHCALL_1_TOKEN    0x00011001
#define METHOD_METHCALL_2_TOKEN    0x00011002
#define METHOD_METHCALL_3_TOKEN    0x00011003
#define METHOD_METHCALL_4_TOKEN    0x00011004
#define METHOD_METHCALL_5_TOKEN    0x00011005
#define METHOD_METHCALL_6_TOKEN    0x00011006
#define METHOD_METHCALL_7_TOKEN    0x00011007
#define METHOD_METHCALL_8_TOKEN    0x00011008
#define METHOD_METHCALL_9_TOKEN    0x00011009
#define METHOD_METHCALL_10_TOKEN   0x0001100A
#define METHOD_METHCALLN_TOKEN     0x000110FF

#define METHOD_CALL_ARGS           0x00012001
#define METHOD_CALL_KWARGS         0x00012002
#define METHOD_KWCALLN_TOKEN       0x00012003

#define METHOD_LOAD_METHOD           0x00013000

#define METHOD_GIL_ENSURE            0x00014000
#define METHOD_GIL_RELEASE           0x00014001

// Py* helpers
#define METHOD_PYTUPLE_NEW           0x00020000
#define METHOD_PYLIST_NEW            0x00020001
#define METHOD_PYDICT_NEWPRESIZED    0x00020002
#define METHOD_PYSET_NEW             0x00020003
#define METHOD_PYSET_ADD             0x00020004
#define METHOD_PYOBJECT_ISTRUE       0x00020005
#define METHOD_PYITER_NEXT           0x00020006
#define METHOD_PYCELL_GET            0x00020007
#define METHOD_PYERR_RESTORE         0x00020008
#define METHOD_PYOBJECT_STR          0x00020009
#define METHOD_PYOBJECT_REPR         0x0002000A
#define METHOD_PYOBJECT_ASCII        0x0002000B
#define METHOD_PYUNICODE_JOINARRAY   0x0002000C

// Loading attributes and globals
#define METHOD_LOADGLOBAL_TOKEN      0x00030000
#define METHOD_LOADATTR_TOKEN        0x00030001
#define METHOD_STOREATTR_TOKEN       0x00030002
#define METHOD_DELETEATTR_TOKEN      0x00030003
#define METHOD_STOREGLOBAL_TOKEN     0x00030004
#define METHOD_DELETEGLOBAL_TOKEN    0x00030005
#define METHOD_LOAD_ASSERTION_ERROR  0x00030006
#define METHOD_GENERIC_GETATTR       0x00030007
#define METHOD_LOADATTR_HASH         0x00030008

/* Tracing methods */
#define METHOD_TRACE_LINE            0x00030010
#define METHOD_TRACE_FRAME_ENTRY     0x00030011
#define METHOD_TRACE_FRAME_EXIT      0x00030012
#define METHOD_TRACE_EXCEPTION       0x00030013
#define METHOD_PROFILE_FRAME_ENTRY   0x00030014
#define METHOD_PROFILE_FRAME_EXIT    0x00030015
#define METHOD_PGC_PROBE             0x00030016

#define METHOD_ITERNEXT_TOKEN        0x00040000

#define METHOD_FLOAT_POWER_TOKEN    0x00050000
#define METHOD_FLOAT_FLOOR_TOKEN    0x00050001
#define METHOD_FLOAT_MODULUS_TOKEN  0x00050002

#define METHOD_STORE_SUBSCR_OBJ       0x00060000
#define METHOD_STORE_SUBSCR_OBJ_I     0x00060001
#define METHOD_STORE_SUBSCR_OBJ_I_HASH     0x00060002
#define METHOD_STORE_SUBSCR_DICT      0x00060003
#define METHOD_STORE_SUBSCR_DICT_HASH 0x00060004
#define METHOD_STORE_SUBSCR_LIST      0x00060005
#define METHOD_STORE_SUBSCR_LIST_I    0x00060006

#define METHOD_SUBSCR_OBJ           0x00070000
#define METHOD_SUBSCR_OBJ_I         0x00070001
#define METHOD_SUBSCR_OBJ_I_HASH    0x00070002
#define METHOD_SUBSCR_DICT          0x00070003
#define METHOD_SUBSCR_DICT_HASH     0x00070004
#define METHOD_SUBSCR_LIST          0x00070005
#define METHOD_SUBSCR_LIST_I        0x00070006
#define METHOD_SUBSCR_TUPLE         0x00070007
#define METHOD_SUBSCR_TUPLE_I       0x00070008
#define METHOD_SUBSCR_LIST_SLICE    0x00070009
#define METHOD_SUBSCR_LIST_SLICE_STEPPED 0x0007000A
#define METHOD_SUBSCR_LIST_SLICE_REVERSED 0x0007000B



#define LD_FIELDA(type, field) m_il.ld_i(offsetof(type, field)); m_il.add();
#define LD_FIELD(type, field) m_il.ld_i(offsetof(type, field)); m_il.add(); m_il.ld_ind_i();
#define LD_FIELDI(type, field) m_il.ld_i(offsetof(type, field)); m_il.mul(); m_il.ld_ind_i();

extern ICorJitCompiler* g_jit;
class PythonCompiler : public IPythonCompiler {
    PyCodeObject *m_code;
    // pre-calculate some information...
    ILGenerator m_il;
    UserModule* m_module;
    Local m_lasti;
    Local m_instrCount;
    unordered_map<int, Local> m_frameLocals;

public:
    explicit PythonCompiler(PyCodeObject *code);

    int il_length() override {
        return static_cast<int>(m_il.m_il.size());
    };

    void emit_rot_two(LocalKind kind) override;

    void emit_rot_three(LocalKind kind) override;

    void emit_rot_four(LocalKind kind) override;

    void emit_pop_top() override;

    void emit_dup_top() override;

    void emit_dup_top_two() override;

    void emit_load_name(PyObject* name) override;
    void emit_load_name_hashed(PyObject* name, ssize_t name_hash) override;

    void emit_is_true() override;

    void emit_push_frame() override;
    void emit_pop_frame() override;
    void emit_eh_trace() override;

    void emit_lasti_init() override;
    void emit_lasti_update(int index) override;

    void emit_ret(int size) override;

    void emit_store_name(PyObject* name) override;
    void emit_delete_name(PyObject* name) override;
    void emit_store_attr(PyObject* name) override;
    void emit_delete_attr(PyObject* name) override;
    void emit_load_attr(PyObject* name) override;
    void emit_load_attr(PyObject* name, AbstractValueWithSources obj) override;
    void emit_store_global(PyObject* name) override;
    void emit_delete_global(PyObject* name) override;
    void emit_load_global(PyObject* name) override;
    void emit_load_global_hashed(PyObject* name, ssize_t name_hash) override;
    void emit_delete_fast(int index) override;

    void emit_new_tuple(size_t size) override;
    void emit_tuple_store(size_t size) override;
    void emit_tuple_load(size_t index) override;
    void emit_list_load(size_t index) override;
    void emit_tuple_length() override;
    void emit_list_length() override;

    void emit_new_list(size_t argCnt) override;
    void emit_list_store(size_t argCnt) override;
    void emit_list_extend() override;
    void emit_list_to_tuple() override;

    void emit_new_set() override;
    void emit_set_extend() override;
    void emit_set_update() override;
    void emit_dict_store() override;
    void emit_dict_store_no_decref() override;
    void emit_dict_update() override;
    void emit_dict_build_from_map() override;

    void emit_unicode_joinarray() override;
    void emit_format_value() override;
    void emit_pyobject_str() override;
    void emit_pyobject_repr() override;
    void emit_pyobject_ascii() override;
    void emit_pyobject_format() override;

    void emit_new_dict(size_t size) override;
    void emit_map_extend() override;

    void emit_build_slice() override;

    void emit_store_subscr() override;
    void emit_store_subscr(AbstractValueWithSources value, AbstractValueWithSources container, AbstractValueWithSources key) override;
    void emit_delete_subscr() override;

    void emit_unary_positive() override;
    void emit_unary_negative() override;
    void emit_unary_negative_float() override;

    void emit_unary_not() override;

    void emit_unary_not_push_int() override;
    void emit_unary_invert() override;

    void emit_import_name(void* name) override;
    void emit_import_from(void* name) override;
    void emit_import_star() override;

    void emit_load_build_class() override;

    void emit_unpack_sequence(size_t size, AbstractValueWithSources iterable) override;
    void emit_unpack_tuple(size_t size, AbstractValueWithSources iterable) override;
    void emit_unpack_list(size_t size, AbstractValueWithSources iterable) override;
    void emit_unpack_generic(size_t size, AbstractValueWithSources iterable) override;
    void emit_unpack_sequence_ex(size_t leftSize, size_t rightSize, AbstractValueWithSources iterable) override;
    void emit_list_shrink(size_t by) override;
    void emit_builtin_method(PyObject* name, AbstractValue* typeValue) override;
    void emit_call_function_inline(size_t n_args, AbstractValueWithSources func) override;
    bool emit_call_function(size_t argCnt) override;
    void emit_call_with_tuple() override;

    void emit_kwcall_with_tuple() override;

    void emit_call_args() override;
    void emit_call_kwargs() override;
    
    void emit_new_function() override;
    void emit_set_closure() override;
    void emit_set_annotations() override;
    void emit_set_kw_defaults() override;
    void emit_set_defaults() override;

    void emit_load_deref(int index) override;
    void emit_store_deref(int index) override;
    void emit_delete_deref(int index) override;
    void emit_load_closure(int index) override;

    Local emit_spill() override;
    void emit_store_local(Local local) override;

    void emit_load_local(Local local) override;
    void emit_load_local_addr(Local local) override;
    void emit_load_and_free_local(Local local) override;
    Local emit_define_local(bool cache) override;
    Local emit_define_local(LocalKind kind) override;
    void emit_free_local(Local local) override;

    void emit_set_add() override;
    void emit_map_add() override;
    void emit_list_append() override;

    void emit_raise_varargs() override;

    void emit_null() override;

    void emit_print_expr() override;
    void emit_load_classderef(int index) override;
    void emit_getiter() override;
    void emit_for_next() override;
    void emit_for_next(AbstractValueWithSources) override;

    void emit_binary_float(int opcode) override;
    void emit_binary_object(int opcode) override;
    void emit_binary_object(int opcode, AbstractValueWithSources left, AbstractValueWithSources right) override;
    void emit_binary_subscr(int opcode, AbstractValueWithSources left, AbstractValueWithSources right) override;
    bool emit_binary_subscr_slice(AbstractValueWithSources container, AbstractValueWithSources start, AbstractValueWithSources stop) override;
    bool emit_binary_subscr_slice(AbstractValueWithSources container, AbstractValueWithSources start, AbstractValueWithSources stop, AbstractValueWithSources step) override;
    void emit_tagged_int_to_float() override;

    void emit_in() override;
    void emit_not_in() override;

    void emit_is(bool isNot) override;

    void emit_compare_object(int compareType) override;
    void emit_compare_float(int compareType) override;
    void emit_compare_known_object(int compareType, AbstractValueWithSources lhs, AbstractValueWithSources rhs) override;
    void emit_compare_tagged_int(int compareType) override;

    void emit_store_fast(int local) override;

    void emit_unbound_local_check() override;
    void emit_load_fast(int local) override;

    Label emit_define_label() override;
    void emit_mark_label(Label label) override;
    void emit_branch(BranchType branchType, Label label) override;

    void emit_int(int value) override;
    void emit_long_long(long long value) override;
    void emit_float(double value) override;
    void emit_ptr(void *value) override;
    void emit_bool(bool value) override;

    void emit_unwind_eh(Local prevExc, Local prevExcVal, Local prevTraceback) override;
    void emit_prepare_exception(Local prevExc, Local prevExcVal, Local prevTraceback) override;
    void emit_reraise() override;
    void emit_restore_err() override;
    void emit_pyerr_setstring(void* exception, const char*msg) override;

    void emit_compare_exceptions() override;

    // Pops a value off the stack, performing no operations related to reference counting
    void emit_pop() override;
    // Dups the current value on the stack, performing no operations related to reference counting
    void emit_dup() override;

    void emit_incref() override;

    void emit_debug_msg(const char* msg) override;
    void emit_debug_pyobject() override;

    void emit_load_method(void* name) override;
    bool emit_method_call(size_t argCnt) override;
    void emit_method_call_n() override;

    void emit_dict_merge() override;

    void emit_load_assertion_error() override;

    void emit_pending_calls() override;
    void emit_init_instr_counter() override;

    void emit_setup_annotations() override;

    void emit_breakpoint() override;

    void emit_inc_local(Local local, int value) override;
    void emit_dec_local(Local local, int value) override;

    void emit_trace_line(Local lowerBound, Local upperBound, Local lastInstr) override;
    void emit_trace_frame_entry() override;
    void emit_trace_frame_exit() override;
    void emit_trace_exception() override;
    void emit_profile_frame_entry() override;
    void emit_profile_frame_exit() override;
    void emit_pgc_probe(size_t curByte, size_t stackSize) override;

    void emit_load_frame_locals() override;
    void emit_triple_binary_op(int firstOp, int secondOp) override;
    JittedCode* emit_compile() override;
    void lift_n_to_top(int pos) override;
    void lift_n_to_second(int pos) override;
    void lift_n_to_third(int pos) override;
    void sink_top_to_n(int pos) override;


private:
    void load_frame();
    void load_tstate();
    void load_local(int oparg);
    void decref(bool noopt = false);
    CorInfoType to_clr_type(LocalKind kind);
    void pop_top() override;
    void emit_binary_subscr(AbstractValueWithSources container, AbstractValueWithSources index);
    void emit_varobject_iter_next(int seq_offset, int index_offset, int ob_item_offset );

    void emit_known_binary_op(AbstractValueWithSources &left, AbstractValueWithSources &right, Local leftLocal, Local rightLocal, int nb_slot,
                              int sq_slot, int fallback_token);
    void emit_known_binary_op_power(AbstractValueWithSources &left, AbstractValueWithSources &right, Local leftLocal, Local rightLocal, int nb_slot,
                              int sq_slot, int fallback_token);
    void emit_known_binary_op_add(AbstractValueWithSources &left, AbstractValueWithSources &right, Local leftLocal, Local rightLocal, int nb_slot,
                              int sq_slot, int fallback_token);
    void emit_known_binary_op_multiply(AbstractValueWithSources &left, AbstractValueWithSources &right, Local leftLocal, Local rightLocal, int nb_slot,
                              int sq_slot, int fallback_token);
    void fill_local_vector(vector<Local> & vec, size_t len);
};

// Copies of internal CPython structures

typedef struct {
    PyObject_HEAD
    Py_ssize_t it_index;
    PyListObject *it_seq; /* Set to NULL when iterator is exhausted */
} _listiterobject;

typedef struct {
    PyObject_HEAD
    Py_ssize_t it_index;
    PyTupleObject *it_seq; /* Set to NULL when iterator is exhausted */
} _tupleiterobject;

#endif