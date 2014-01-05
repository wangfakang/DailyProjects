#include "pch.h"

#include <time.h>
#include <string.h>
#include <assert.h>

#include <vector>
#include <map>

#ifdef __GNUC__
#include <sys/mman.h>
#endif

using namespace std;

#define BENCHMARK(ops) { clock_t start = clock(); ops; printf("%s: %f\n", #ops, float(clock() - start) / CLOCKS_PER_SEC); }

#ifdef _MSC_VER
#define FORCE_INLINE  __forceinline
#elif __GNUC__
//#define FORCE_INLINE  __attribute__((always_inline))
#define FORCE_INLINE  inline
#else
#define FORCE_INLINE inline
#endif

// ------------------------------ code emission
enum OpCode {
    OC_Add, OC_Sub, OC_Mul, OC_Div,
    OC_EQ, OC_NE,
    OC_PushLocal, OC_PopLocal,
    OC_PushInt,
    OC_Jmp, OC_TrueJmp,
    OC_EOF,
    OC_NOP,
};

#define CodeSize 4
template<int size>
struct CodeTypeSelector {
    typedef char type;
};
template<>
struct CodeTypeSelector<4> {
    typedef int type;
};
typedef CodeTypeSelector<CodeSize>::type CodeType; 

typedef CodeType LocalIdxType;
static const int LocalIdxSize = CodeSize;

static void emit(vector<char> &codes, OpCode code) {
    int pos = (int)codes.size();
    codes.resize(pos + CodeSize);
    memcpy(&codes[pos], &code, CodeSize);
}
template<typename VType>
static void emit(vector<char> &codes, OpCode code, VType value) {
    int pos = (int)codes.size();
    codes.resize(pos + CodeSize + sizeof(value));
    memcpy(&codes[pos], &code, CodeSize);
    memcpy(&codes[pos + CodeSize], &value, sizeof(value));
}
static void fixupJmpTarget(vector<char>& codes) {
    vector<int> codeOffs;
    for (int i = 0; i < (int)codes.size();) {
        codeOffs.push_back(i);
        switch ((CodeType&)codes[i]) {
            case OC_PushLocal: case OC_PopLocal:
                i += CodeSize + LocalIdxSize;
                break;
            case OC_PushInt: case OC_Jmp: case OC_TrueJmp:
                i += CodeSize + sizeof(int);
                break;
            default:
                i += CodeSize;
        }
    }
    codeOffs.push_back(codes.size());

    for (int i = 0; i < (int)codes.size();) {
        switch ((CodeType&)codes[i]) {
            case OC_PushLocal: case OC_PopLocal:
                i += CodeSize + LocalIdxSize;
                break;
            case OC_PushInt:
                i += CodeSize + sizeof(int);
                break;
            case OC_Jmp: case OC_TrueJmp: {
                int *p = (int*)(&codes[i] + CodeSize);
                *p = codeOffs[*p] - i;
                i += CodeSize;
              }
                break;
            default:
                i += CodeSize;
        }
    }
}

// ------------------------------ call-threading interpreter
FORCE_INLINE static void _op_add(vector<int> &stack, vector<int> &locals, const char *&ip) {
    stack[stack.size() - 2] += stack[stack.size() - 1]; stack.pop_back();
    ip += CodeSize;
}
FORCE_INLINE static void _op_sub(vector<int> &stack, vector<int> &locals, const char *&ip) {
    stack[stack.size() - 2] -= stack[stack.size() - 1]; stack.pop_back();
    ip += CodeSize;
}
FORCE_INLINE static void _op_mul(vector<int> &stack, vector<int> &locals, const char *&ip) {
    stack[stack.size() - 2] *= stack[stack.size() - 1]; stack.pop_back();
    ip += CodeSize;
}
FORCE_INLINE static void _op_div(vector<int> &stack, vector<int> &locals, const char *&ip) {
    stack[stack.size() - 2] /= stack[stack.size() - 1]; stack.pop_back();
    ip += CodeSize;
}
FORCE_INLINE static void _op_eq(vector<int> &stack, vector<int> &locals, const char *&ip) {
    stack[stack.size() - 2] = stack[stack.size() - 2] == stack[stack.size() - 1]; stack.pop_back();
    ip += CodeSize;
}
FORCE_INLINE static void _op_ne(vector<int> &stack, vector<int> &locals, const char *&ip) {
    stack[stack.size() - 2] = stack[stack.size() - 2] != stack[stack.size() - 1]; stack.pop_back();
    ip += CodeSize;
}
FORCE_INLINE static void _op_pushLocal(vector<int> &stack, vector<int> &locals, const char *&ip) {
    stack.push_back(locals[*(LocalIdxType*)(ip + CodeSize)]);
    ip += CodeSize + LocalIdxSize;
}
FORCE_INLINE static void _op_popLocal(vector<int> &stack, vector<int> &locals, const char *&ip) {
    locals[*(LocalIdxType*)(ip + CodeSize)] = stack.back();
    stack.pop_back();
    ip += CodeSize + LocalIdxSize;
}
FORCE_INLINE static void _op_pushInt(vector<int> &stack, vector<int> &locals, const char *&ip) {
    stack.push_back(*(int*)(ip + CodeSize));
    ip += CodeSize + sizeof(int);
}
FORCE_INLINE static void _op_jmp(vector<int> &stack, vector<int> &locals, const char *&ip) {
    ip += *(int*)(ip + CodeSize);
}
FORCE_INLINE static void _op_trueJmp(vector<int> &stack, vector<int> &locals, const char *&ip) {
    int v = stack.back();
    stack.pop_back();
    if (v) {
        ip += *(int*)(ip + CodeSize);
    } else {
        ip += CodeSize + sizeof(int);
    }
}
FORCE_INLINE static void _op_nop(vector<int> &stack, vector<int> &locals, const char *&ip) {
    ip += CodeSize;
}
FORCE_INLINE static int call_threading_interpreter(const vector<char> &codes) {
    assert(!codes.empty());
    const char *ip = &codes[0];
    vector<int> stack;
    vector<int> locals(32, 1);
    void(*funcs[])(vector<int>&,vector<int>&, const char*&) = { _op_add, _op_sub, _op_mul, _op_div, _op_eq, _op_ne, _op_pushLocal, _op_popLocal, _op_pushInt, _op_jmp, _op_trueJmp, NULL, _op_nop, };
    while ((CodeType&)*ip != OC_EOF) {
        funcs[(CodeType&)*ip](stack, locals, ip);
    }
    return locals[0];
}

// ------------------------------ switch-threading interpreter with stl
static int switch_threading_stl_interpreter(const vector<char> &codes) {
    assert(!codes.empty());
    const char *ip = &codes[0];
    vector<int> stack;
    vector<int> locals(32, 1);
    for (;;) {
        switch ((CodeType&)*ip) {
            case OC_Add: _op_add(stack, locals, ip); break;
            case OC_Sub: _op_sub(stack, locals, ip); break;
            case OC_Mul: _op_mul(stack, locals, ip); break;
            case OC_Div: _op_div(stack, locals, ip); break;
            case OC_EQ: _op_eq(stack, locals, ip); break;
            case OC_NE: _op_ne(stack, locals, ip); break;
            case OC_PushLocal: _op_pushLocal(stack, locals, ip); break;
            case OC_PopLocal: _op_popLocal(stack, locals, ip); break;
            case OC_PushInt: _op_pushInt(stack, locals, ip); break;
            case OC_Jmp: _op_jmp(stack, locals, ip); break;
            case OC_TrueJmp: _op_trueJmp(stack, locals, ip); break;
            case OC_NOP: default: _op_nop(stack, locals, ip); break;
            case OC_EOF: return locals[0];
        }
    }
    return locals[0];
}
//------------------------------ switch-threading interpreter
static int switch_threading_interpreter(const vector<char> &codes) {
    assert(!codes.empty());
    const char *ip = &codes[0];
    int stackSpace[32];
    int *stack = stackSpace;
    int locals[32] = {1};
    for (;;) {
        switch ((CodeType&)*ip) {
            case OC_Add: 
                stack[-2] += stack[-1]; --stack;
                ip += CodeSize;
                 break;
            case OC_Sub: 
                stack[-2] -= stack[-1]; --stack;
                ip += CodeSize;
                 break;
            case OC_Mul: 
                stack[-2] *= stack[-1]; --stack;
                ip += CodeSize;
                 break;
            case OC_Div: 
                stack[-2] /= stack[-1]; --stack;
                ip += CodeSize;
                 break;
            case OC_EQ: 
                stack[-2] = stack[-2] == stack[-1]; --stack;
                ip += CodeSize;
                 break;
            case OC_NE: 
                stack[-2] = stack[-2] != stack[-1]; --stack;
                ip += CodeSize;
                 break;
            case OC_PushLocal: 
                 *stack++ = locals[*(LocalIdxType*)(ip + CodeSize)];
                ip += CodeSize + LocalIdxSize;
                 break;
            case OC_PopLocal: 
                 locals[*(LocalIdxType*)(ip + CodeSize)] = *--stack;
                ip += CodeSize + LocalIdxSize;
                 break;
            case OC_PushInt:
                 *stack++ = *(int*)(ip + CodeSize);
                 ip += CodeSize + sizeof(int);
                 break;
            case OC_Jmp:
                 ip += *(int*)(ip + CodeSize);
                 break;
            case OC_TrueJmp: {
                     int v = *--stack;
                     if (v) {
                         ip += *(int*)(ip + CodeSize);
                     } else {
                         ip += CodeSize + sizeof(int);
                     }
                 }
                 break;
            case OC_EOF:
                 return locals[0];
            case OC_NOP:
            default:
                 ip += CodeSize;
                 break;
        }
    }
    return locals[0];
}

// ------------------------------ replicate-switch-threading interpreter
static int replicate_switch_threading_interpreter(const vector<char>& codes) {
#define NEXT() switch ((CodeType&)*ip) {\
    case OC_Add: goto label_Add;\
    case OC_Sub: goto label_Sub;\
    case OC_Mul: goto label_Mul;\
    case OC_Div: goto label_Div;\
    case OC_EQ: goto label_EQ;\
    case OC_NE: goto label_NE;\
    case OC_PushLocal: goto label_PushLocal;\
    case OC_PopLocal: goto label_PopLocal;\
    case OC_PushInt: goto label_PushInt;\
    case OC_Jmp: goto label_Jmp;\
    case OC_TrueJmp: goto label_TrueJmp;\
    case OC_EOF: goto label_EOF;\
    case OC_NOP: goto label_NOP;\
    default: goto label_NOP;\
}

    assert(!codes.empty());
    const char *ip = &codes[0];
    int stackSpace[32];
    int *stack = stackSpace;
    int locals[32] = {1};

    NEXT();
    label_Add: 
        stack[-2] += stack[-1]; --stack;
        ip += CodeSize;
         NEXT();
    label_Sub: 
        stack[-2] -= stack[-1]; --stack;
        ip += CodeSize;
         NEXT();
    label_Mul: 
        stack[-2] *= stack[-1]; --stack;
        ip += CodeSize;
         NEXT();
    label_Div: 
        stack[-2] /= stack[-1]; --stack;
        ip += CodeSize;
         NEXT();
    label_EQ: 
        stack[-2] = stack[-2] == stack[-1]; --stack;
        ip += CodeSize;
         NEXT();
    label_NE: 
        stack[-2] = stack[-2] != stack[-1]; --stack;
        ip += CodeSize;
         NEXT();
    label_PushLocal: 
         *stack++ = locals[*(LocalIdxType*)(ip + CodeSize)];
        ip += CodeSize + LocalIdxSize;
         NEXT();
    label_PopLocal: 
         locals[*(LocalIdxType*)(ip + CodeSize)] = *--stack;
        ip += CodeSize + LocalIdxSize;
         NEXT();
    label_PushInt:
         *stack++ = *(int*)(ip + CodeSize);
         ip += CodeSize + sizeof(int);
         NEXT();
    label_Jmp:
         ip += *(int*)(ip + CodeSize);
         NEXT();
    label_TrueJmp: {
             int v = *--stack;
             if (v) {
                 ip += *(int*)(ip + CodeSize);
             } else {
                 ip += CodeSize + sizeof(int);
             }
         }
         NEXT();
    label_EOF:
         return locals[0];
    label_NOP:
         ip += CodeSize;
             NEXT();
#undef NEXT
}

// ------------------------------ token-threading interpreter
static int token_threading_interpreter(const vector<char> &codes) {
#ifndef __GNUC__
    return 0;
#else

#define NEXT() goto *labels[(CodeType&)*ip]

    assert(!codes.empty());
    const char *ip = &codes[0];
    int stackSpace[32];
    int *stack = stackSpace;
    int locals[32] = {1};

    void* labels[] = { &&label_Add, &&label_Sub, &&label_Mul, &&label_Div, &&label_EQ, &&label_NE, &&label_PushLocal, &&label_PopLocal, &&label_PushInt, &&label_Jmp, &&label_TrueJmp, &&label_EOF, &&label_NOP, };

    NEXT();
    label_Add: 
        stack[-2] += stack[-1]; --stack;
        ip += CodeSize;
         NEXT();
    label_Sub: 
        stack[-2] -= stack[-1]; --stack;
        ip += CodeSize;
         NEXT();
    label_Mul: 
        stack[-2] *= stack[-1]; --stack;
        ip += CodeSize;
         NEXT();
    label_Div: 
        stack[-2] /= stack[-1]; --stack;
        ip += CodeSize;
         NEXT();
    label_EQ: 
        stack[-2] = stack[-2] == stack[-1]; --stack;
        ip += CodeSize;
         NEXT();
    label_NE: 
        stack[-2] = stack[-2] != stack[-1]; --stack;
        ip += CodeSize;
         NEXT();
    label_PushLocal: 
         *stack++ = locals[*(LocalIdxType*)(ip + CodeSize)];
        ip += CodeSize + LocalIdxSize;
         NEXT();
    label_PopLocal: 
         locals[*(LocalIdxType*)(ip + CodeSize)] = *--stack;
        ip += CodeSize + LocalIdxSize;
         NEXT();
    label_PushInt:
         *stack++ = *(int*)(ip + CodeSize);
         ip += CodeSize + sizeof(int);
         NEXT();
    label_Jmp:
         ip += *(int*)(ip + CodeSize);
         NEXT();
    label_TrueJmp: {
             int v = *--stack;
             if (v) {
                 ip += *(int*)(ip + CodeSize);
             } else {
                 ip += CodeSize + sizeof(int);
             }
         }
         NEXT();
    label_EOF:
         return locals[0];
    label_NOP:
         ip += CodeSize;
             NEXT();
#undef NEXT

#endif
}
//------------------------------ direct-threading interpreter
static int direct_threading_interpreter(const vector<char> &_codes) {

#if defined(__GNUC__) && !defined(__x86_64__) && (CodeSize == 4)

#define NEXT() goto **(void**)ip

    void* labels[] = { &&label_Add, &&label_Sub, &&label_Mul, &&label_Div, &&label_EQ, &&label_NE, &&label_PushLocal, &&label_PopLocal, &&label_PushInt, &&label_Jmp, &&label_TrueJmp, &&label_EOF, &&label_NOP, };
    vector<char> codes(_codes);
    for (int i = 0; i < (int)codes.size(); ) {
        CodeType code = (CodeType&)codes[i];
        switch (code) {
            case OC_PushLocal: case OC_PopLocal:
                (void*&)codes[i] = labels[code];
                i += CodeSize + LocalIdxSize;
                break;
            case OC_PushInt: case OC_Jmp: case OC_TrueJmp:
                (void*&)codes[i] = labels[code];
                i += CodeSize + sizeof(int);
                break;
            default:
                (void*&)codes[i] = labels[code];
                i += CodeSize;
        }
    }

    assert(!codes.empty());
    const char *ip = &codes[0];
    int stackSpace[32];
    int *stack = stackSpace;
    int locals[32] = {1};

    NEXT();
    label_Add: 
        stack[-2] += stack[-1]; --stack;
        ip += CodeSize;
         NEXT();
    label_Sub: 
        stack[-2] -= stack[-1]; --stack;
        ip += CodeSize;
         NEXT();
    label_Mul: 
        stack[-2] *= stack[-1]; --stack;
        ip += CodeSize;
         NEXT();
    label_Div: 
        stack[-2] /= stack[-1]; --stack;
        ip += CodeSize;
         NEXT();
    label_EQ: 
        stack[-2] = stack[-2] == stack[-1]; --stack;
        ip += CodeSize;
         NEXT();
    label_NE: 
        stack[-2] = stack[-2] != stack[-1]; --stack;
        ip += CodeSize;
         NEXT();
    label_PushLocal: 
         *stack++ = locals[*(LocalIdxType*)(ip + CodeSize)];
        ip += CodeSize + LocalIdxSize;
         NEXT();
    label_PopLocal: 
         locals[*(LocalIdxType*)(ip + CodeSize)] = *--stack;
        ip += CodeSize + LocalIdxSize;
         NEXT();
    label_PushInt:
         *stack++ = *(int*)(ip + CodeSize);
         ip += CodeSize + sizeof(int);
         NEXT();
    label_Jmp:
         ip += *(int*)(ip + CodeSize);
         NEXT();
    label_TrueJmp: {
             int v = *--stack;
             if (v) {
                 ip += *(int*)(ip + CodeSize);
             } else {
                 ip += CodeSize + sizeof(int);
             }
         }
         NEXT();
    label_EOF:
         return locals[0];
    label_NOP:
         ip += CodeSize;
             NEXT();
#undef NEXT

#else
    return 0;
#endif
}

//------------------------------ jit interpreter
static int jit_interpreter(const vector<char> &codes) {
    int MEM_SIZE = 4 * 1024;
    char *p = (char*)mmap(NULL, MEM_SIZE, PROT_EXEC | PROT_WRITE | PROT_READ, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (p == MAP_FAILED) puts(strerror(errno)), exit(1);

#define EMIT_NATIVE(...)  { unsigned char codes[] = {__VA_ARGS__}; memcpy(bytes, codes, sizeof(codes)); bytes += sizeof(codes);}
#define EMIT_NATIVE_INT(i) { int v = i; memcpy(bytes, &v, sizeof(v)); bytes += sizeof(v); }

    char *bytes = p;
    EMIT_NATIVE(0x55); // push %ebp
    EMIT_NATIVE(0x53); // push %ebx
    EMIT_NATIVE(0x51); // push %ecx
    EMIT_NATIVE(0x52); // push %edx
    EMIT_NATIVE(0x89, 0xe5); // mov %esp, %ebp
    EMIT_NATIVE(0x83, 0xec, 0x40); // sub $0x40, %esp

    map<int, int> bcOff2NativeOff;
    vector<int*> jmpTargets;

    const char *ip = &codes[0];
    for (;;) {
        bcOff2NativeOff[ip - &codes[0]] = bytes - p;
        switch ((CodeType&)*ip) {
            case OC_Add: 
                EMIT_NATIVE(0x8b, 0x04, 0x24); // mov (%esp), %eax
                EMIT_NATIVE(0x01, 0x44, 0x24, 0x04); // add    %eax,0x4(%esp)
                EMIT_NATIVE(0x83, 0xc4, 0x04); //  add    $0x4,%esp
                ip += CodeSize;
                 break;
            case OC_Sub: 
                EMIT_NATIVE(0x8b, 0x04, 0x24); // mov (%esp), %eax
                EMIT_NATIVE(0x29, 0x44, 0x24, 0x04); // sub    %eax,0x4(%esp)
                EMIT_NATIVE(0x83, 0xc4, 0x04); //  add    $0x4,%esp
                ip += CodeSize;
                 break;
            case OC_Mul: 
                EMIT_NATIVE(0x8b, 0x04, 0x24); // mov    (%esp),%eax
                EMIT_NATIVE(0xf7, 0x64, 0x24, 0x04); // mull   0x4(%esp)
                EMIT_NATIVE(0x89, 0x44, 0x24, 0x04); // mov    %eax,0x4(%esp)
                EMIT_NATIVE(0x83, 0xc4, 0x04); //  add    $0x4,%esp
                ip += CodeSize;
                 break;
            case OC_Div: 
                EMIT_NATIVE(0x31, 0xd2); // xor    %edx,%edx
                EMIT_NATIVE(0x8b, 0x44, 0x24, 0x04); // mov    0x4(%esp),%eax
                EMIT_NATIVE(0xf7, 0x34, 0x24); // divl   (%esp)
                EMIT_NATIVE(0x89, 0x44, 0x24, 0x04); // mov    %eax,0x4(%esp)
                EMIT_NATIVE(0x83, 0xc4, 0x04); //  add    $0x4,%esp
                ip += CodeSize;
                 break;
            case OC_EQ: 
                EMIT_NATIVE(0x8b, 0x04, 0x24); // mov    (%esp),%eax
                EMIT_NATIVE(0x3b, 0x44, 0x24, 0x04); // cmp    0x4(%esp),%eax
                EMIT_NATIVE(0x0f, 0x94, 0xc0); // sete   %al
                EMIT_NATIVE(0x0f, 0xb6, 0xc0); // movzbl %al,%eax
                EMIT_NATIVE(0x89, 0x44, 0x24, 0x04); // mov    %eax,0x4(%esp)
                EMIT_NATIVE(0x83, 0xc4, 0x04); //  add    $0x4,%esp
                ip += CodeSize;
                 break;
            case OC_NE: 
                EMIT_NATIVE(0x8b, 0x04, 0x24); // mov    (%esp),%eax
                EMIT_NATIVE(0x3b, 0x44, 0x24, 0x04); // cmp    0x4(%esp),%eax
                EMIT_NATIVE(0x0f, 0x95, 0xc0); // setne  %al
                EMIT_NATIVE(0x0f, 0xb6, 0xc0); // movzbl %al,%eax
                EMIT_NATIVE(0x89, 0x44, 0x24, 0x04); // mov    %eax,0x4(%esp)
                EMIT_NATIVE(0x83, 0xc4, 0x04); //  add    $0x4,%esp
                ip += CodeSize;
                 break;
            case OC_PushLocal: 
                 EMIT_NATIVE(0xff, 0xb5); // pushl  -4(%ebp)
                 EMIT_NATIVE_INT((*(LocalIdxType*)(ip + CodeSize) + 1) * -4);
                ip += CodeSize + LocalIdxSize;
                 break;
            case OC_PopLocal: 
                 EMIT_NATIVE(0x8f, 0x85); // popl   -4(%ebp)
                 EMIT_NATIVE_INT((*(LocalIdxType*)(ip + CodeSize) + 1) * -4);
                ip += CodeSize + LocalIdxSize;
                 break;
            case OC_PushInt:
                 EMIT_NATIVE(0x68); // pushl  0x12d686
                 EMIT_NATIVE_INT(*(int*)(ip + CodeSize));
                 ip += CodeSize + sizeof(int);
                 break;
            case OC_Jmp:
                 EMIT_NATIVE(0xe9);
                 jmpTargets.push_back((int*)bytes);
                 EMIT_NATIVE_INT(ip + *(int*)(ip + CodeSize) - &codes[0]);
                 ip += CodeSize + sizeof(int);
                 break;
            case OC_TrueJmp: {
                     EMIT_NATIVE(0x8b, 0x04, 0x24);// mov    (%esp),%eax
                     EMIT_NATIVE(0x83, 0xc4, 0x04); //  add    $0x4,%esp
                     EMIT_NATIVE(0x85, 0xc0);// test   %eax,%eax
                     EMIT_NATIVE(0x0f, 0x85);// je
                     jmpTargets.push_back((int*)bytes);
                     EMIT_NATIVE_INT(ip + *(int*)(ip + CodeSize) - &codes[0]);
                     ip += CodeSize + sizeof(int);
                 }
                 break;
            case OC_EOF:
                 goto label_end_loop;
            case OC_NOP:
            default:
                 ip += CodeSize;
                 break;
        }
    }
label_end_loop:

    for (int i = 0; i < (int)jmpTargets.size(); ++i) {
        *jmpTargets[i] = p + bcOff2NativeOff[*jmpTargets[i]] - ((char*)jmpTargets[i] + 4);
    }

    EMIT_NATIVE(0x8b, 0x45, 0xfc); // mov -4(%ebp), %eax
    EMIT_NATIVE(0x89, 0xec); // mov %ebp, %esp
    EMIT_NATIVE(0x5a); // pop %edx
    EMIT_NATIVE(0x59); // pop %ecx
    EMIT_NATIVE(0x5b); // pop %ebx
    EMIT_NATIVE(0x5d); // pop %ebp
    EMIT_NATIVE(0xc3); // ret

    int r = ((int(*)())p)();

    munmap(p, MEM_SIZE);

#undef EMIT_NATIVE_INT
#undef EMIT_NATIVE

    return r;
}

int main(int argc, char *argv[]) {
    int LOOP = 1000;
    if (argc > 1) LOOP = atoi(argv[1]);

    int expectedValue = 0;
    for (int i = 0; i < LOOP; ++i) {
        for (int j = 0; j < i + 1; ++j) {
            expectedValue += i * j;
        }
    }

    /*
        pushInt 0
        popLocal 10
        pushInt 0
        popLocal 0
label_loop1:
        pushLocal 0
        pushInt LOOP
        eq
        trueJmp label_end1

        pushInt 0
        popLocal 1
label_loop2:
        pushLocal 1
        pushLocal 0
        pushInt 1
        add
        eq
        trueJmp label_end2

        pushLocal 0
        pushLocal 1
        mul
        pushLocal 10
        add
        popLocal 10

        pushLocal 1
        pushInt 1
        add
        popLocal 1
        jmp label_loop2
label_end2:

        pushLocal 0
        pushInt 1
        add
        popLocal 0
        jmp label_loop1
label_end1:

     * */
    vector<char> codes;
    emit(codes, OC_PushInt, 0); emit(codes, OC_PopLocal, (LocalIdxType)0);
    emit(codes, OC_PushInt, 0); emit(codes, OC_PopLocal, (LocalIdxType)1);
    // label_loop1
    emit(codes, OC_PushLocal, (LocalIdxType)1);
    emit(codes, OC_PushInt, LOOP);
    emit(codes, OC_EQ);
    emit(codes, OC_TrueJmp, 32);

    emit(codes, OC_PushInt, 0); emit(codes, OC_PopLocal, (LocalIdxType)2);
    // label_loop2
    emit(codes, OC_PushLocal, (LocalIdxType)2);
    emit(codes, OC_PushLocal, (LocalIdxType)1);
    emit(codes, OC_PushInt, 1);
    emit(codes, OC_Add);
    emit(codes, OC_EQ);
    emit(codes, OC_TrueJmp, 27);

    emit(codes, OC_PushLocal, (LocalIdxType)1);
    emit(codes, OC_PushLocal, (LocalIdxType)2);
    emit(codes, OC_Mul);
    emit(codes, OC_PushLocal, (LocalIdxType)0);
    emit(codes, OC_Add);
    emit(codes, OC_PopLocal, (LocalIdxType)0);

    emit(codes, OC_PushLocal, (LocalIdxType)2);
    emit(codes, OC_PushInt, 1);
    emit(codes, OC_Add);
    emit(codes, OC_PopLocal, (LocalIdxType)2);
    emit(codes, OC_Jmp, 10);
    // label_end2

    emit(codes, OC_PushLocal, (LocalIdxType)1);
    emit(codes, OC_PushInt, 1);
    emit(codes, OC_Add);
    emit(codes, OC_PopLocal, (LocalIdxType)1);
    emit(codes, OC_Jmp, 4);
    // label_end1
    emit(codes, OC_EOF);

    fixupJmpTarget(codes);

    BENCHMARK(int v = call_threading_interpreter(codes); assert(v == expectedValue || !v););
    BENCHMARK(int v = switch_threading_stl_interpreter(codes); assert(v == expectedValue || !v););
    BENCHMARK(int v = switch_threading_interpreter(codes); assert(v == expectedValue || !v););
    BENCHMARK(int v = replicate_switch_threading_interpreter(codes); assert(v == expectedValue || !v););
    BENCHMARK(int v = token_threading_interpreter(codes); assert(v == expectedValue || !v););
    BENCHMARK(int v = direct_threading_interpreter(codes); assert(v == expectedValue || !v););
    BENCHMARK(int v = jit_interpreter(codes); assert(v == expectedValue || !v););
}
