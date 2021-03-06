/**
 * +-----------------------------------------------------------------+
 * |  myjvm writing a Java virtual machine step by step (C version)  |
 * +-----------------------------------------------------------------+
 * |  Author: springlchy <sisbeau@126.com>  All Rights Reserved      |
 * +-----------------------------------------------------------------+
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "parse_class.c"

void displayStaticFields(Class *pclass);

/**
 * @brief runMethod instruction execute loop, simulate the instruction execute model of CPU
 * @param env
 */
void runMethod(OPENV *env)
{
    uchar op;
    Instruction instruction;
    do {
        op = *(env->pc);
        instruction = jvm_instructions[op];
        printf("#%d: %s ", env->pc-env->pc_start, instruction.code_name);
        printCurrentClassMethod(env, instruction);

        env->pc=env->pc+1;
        instruction.action(env);
        printf("\n");
    } while(1);
}

/**
 * @brief internalRunClinitMethod specially executes the instructions in the <clinit> method
 * @param env
 */
void internalRunClinitMethod(OPENV *env)
{
    uchar op;
    Instruction instruction;
    do {
        op = *(env->pc);
        instruction = jvm_instructions[op];
        printf("#%d: %s ", env->pc-env->pc_start, instruction.code_name);
        printCurrentClassMethod(env, instruction);

        env->pc=env->pc+1;
        instruction.action(env);
        displayStaticFields(env->current_class);
        printf("\n");
    } while(env->pc <= env->pc_end && env->pc!=NULL);
}

method_info* findMainMethod(Class *pclass)
{
    ushort index=0;
    while(index < pclass->methods_count) {
        if(IS_MAIN_METHOD(pclass, pclass->methods[index])){
            break;
        }
        index++;
    }

    if (index == pclass->methods_count) {
        return NULL;
    }
    return pclass->methods[index];
}

method_info* findClinitMethod(Class *pclass)
{
    ushort index=0;
    while(index < pclass->methods_count) {
        if(NULL != pclass->methods[index] && IS_CLINIT_METHOD(pclass, pclass->methods[index])){
            break;
        }
        index++;
    }

    if (index == pclass->methods_count) {
        return NULL;
    }

    return pclass->methods[index];
}

/**
 * @brief runClinitMethod run <clinit> method
 * @param current_env
 * @param clinit_class
 * @param method
 */
void runClinitMethod(OPENV *current_env, Class *clinit_class, method_info* method)
{
    OPENV clinitEnv;
    StackFrame* stf;
    Code_attribute* code_attr;

    if (clinit_class->clinit_runned) {
        return;
    }

    debug("before call, current_class=%s", get_class_name(current_env->current_class->constant_pool, current_env->current_class->this_class));

    // 1. create new stack frame
    code_attr = (Code_attribute*)(method->code_attribute_addr);
    stf = newStackFrame(NULL, code_attr);

    // 4. set new environment
    clinitEnv.pc = clinitEnv.pc_start = code_attr->code;
    clinitEnv.pc_end = code_attr->code + code_attr->code_length;
    clinitEnv.current_stack = stf;
    clinitEnv.current_class = clinit_class;
    clinitEnv.method = method;
    clinitEnv.is_clinit = 1;
    clinitEnv.call_depth = 0;
    stf->method = method;

    #ifdef DEBUG
        clinitEnv.dbg = newDebugType(code_attr->max_locals, STACK_FRAME_SIZE);
    #endif
    debug("real class name = %s", get_class_name(current_env->current_class->constant_pool, current_env->current_class->this_class));
    internalRunClinitMethod(&clinitEnv);
    clinit_class->clinit_runned = 1;
    displayStaticFields(clinit_class);
}

/**
 * @brief runMainMethod run the `main` method
 * @param pclass
 */
void runMainMethod(Class *pclass)
{
    CONSTANT_Class_info *class_info;
    CONSTANT_Utf8_info *class_utf8_info;

    Class *parent_class;
    StackFrame* mainStack;
    OPENV mainEnv;
    method_info *mainMethod, *clinitMethod;
    Code_attribute* mainCode_attr;

    mainMethod = findMainMethod(pclass);
    if (NULL == mainMethod) {
        printf("Error: cannot find main method!\n");
        exit(1);
    }

    mainCode_attr = GET_CODE_FROM_METHOD(mainMethod);
    mainStack = newStackFrame(NULL, mainCode_attr);

    mainEnv.current_class = pclass;
    mainEnv.current_stack = mainStack;
    mainEnv.pc = mainCode_attr->code;
    mainEnv.pc_end = mainCode_attr->code + mainCode_attr->code_length;
    mainEnv.pc_start = mainCode_attr->code;
    mainEnv.method = mainMethod;
    mainEnv.call_depth = 0;
    mainEnv.is_clinit = 0;

    mainStack->method = mainMethod;

#ifdef DEBUG
    mainEnv.dbg = newDebugType(mainCode_attr->max_locals, STACK_FRAME_SIZE);
#endif

    debug("stack=%p", mainStack);

    printf("class name=%s", get_this_class_name(pclass));
    // 1. run super class's clinit method
    if (pclass->super_class > 0) {
        printf("**********run super class's clinit method\n");
        class_info = (CONSTANT_Class_info*)(pclass->constant_pool[pclass->super_class]);
        class_utf8_info = (CONSTANT_Utf8_info*)(pclass->constant_pool[class_info->name_index]);
        parent_class = loadClassFromDiskRecursive(&mainEnv, class_utf8_info->bytes);
        class_info->pclass = parent_class;
        pclass->parent_class = parent_class;
    }

    // 2. run this class's clinit method

    if (clinitMethod = findClinitMethod(pclass)) {
        printf("*********run this class's clinit method\n");
        runClinitMethod(&mainEnv, pclass, clinitMethod);
    }

    printf("class name=%s", get_this_class_name(pclass));

    // 3. run main method
    printf("******run main method\n");
    runMethod(&mainEnv);
}


void callResolvedClassVirtualMethod(OPENV* current_env, CONSTANT_Methodref_info* method_ref, MethodEntry *mentry)
{
    Object *obj;
    StackFrame* stf, *last_stack;
    CONSTANT_Class_info* class_info;
    method_info* method;
    Code_attribute* code_attr;
    int real_args_len =0;

    debug("before call, current_class=%s", get_class_name(current_env->current_class->constant_pool, current_env->current_class->this_class));
    if (current_env->current_class->super_class) {
        debug("super_class=%s", get_class_name(current_env->current_class->constant_pool, current_env->current_class->super_class));
    }
    printMethodrefInfo(current_env->current_class, method_ref);


    last_stack= current_env->current_stack;
    // 1. create new stack frame
    method = mentry->method; //(method_info*)(method_ref->ref_addr);
    code_attr = (Code_attribute*)(method->code_attribute_addr);
    debug("code_attr=%p", code_attr);
    stf = newStackFrame(last_stack, code_attr);
    debug("End create new stack frame, max_locals = %d", code_attr->max_locals);

    // 2. copy args
    real_args_len = method->args_len + SZ_REF;
    last_stack->sp -= real_args_len;
    memcpy(stf->localvars, last_stack->sp, real_args_len);
    obj = *(Object**)(stf->localvars);
    current_env->current_obj = obj;
    debug("args_len=%d", real_args_len);
    debug("last_stack=%p, localvar[0]=%p", PICK_STACKC(last_stack, Reference), obj);

    // 3. save current environment
    stf->last_pc = current_env->pc;
    stf->last_pc_end = current_env->pc_end;
    stf->last_pc_start = current_env->pc_start;
    stf->last_class = current_env->current_class;
    stf->method = method;
    printf("End save current environment\n");

    // 4. set new environment
    //class_info = (CONSTANT_Class_info*)(current_env->current_class->constant_pool[method_ref->class_index]);
    current_env->pc = current_env->pc_start = code_attr->code;
    current_env->pc_end = code_attr->code + code_attr->code_length;
    current_env->current_class = mentry->pclass; //method_ref->pclass;
    current_env->current_stack = stf;
    current_env->call_depth++;

    debug("real call: class_name=%s", get_this_class_name(mentry->pclass));
    //debug("real class name = %s", get_class_name(current_env->current_class->constant_pool, current_env->current_class->this_class));
}

MethodEntry* resolveClassVirtualMethod(Class* caller_class, CONSTANT_Methodref_info **pmethod_ref, CONSTANT_Utf8_info *method_name_utf8, CONSTANT_Utf8_info *method_descriptor_utf8)
{
    MethodEntry *mentry = NULL;
    Class* callee_class;
    cp_info callee_cp, caller_cp;
    CONSTANT_Methodref_info* method_ref = *pmethod_ref;
    CONSTANT_NameAndType_info* method_nt_info;
    CONSTANT_Utf8_info  *tmp_method_name_utf8, *tmp_method_descriptor_utf8;
    CONSTANT_Class_info *method_ref_class_info;
    method_info *method;
    int i, found =0;

    caller_cp = caller_class->constant_pool;
    callee_class = caller_class;
    if (NULL == callee_class) {
        printf("resolveClassVirtualMehod: null class\n");exit(1);
     }

    debug("method_name=%s, type=%s", method_name_utf8->bytes, method_descriptor_utf8->bytes);
    do {
        debug("callee_class=%p", callee_class);
        callee_cp = callee_class->constant_pool;
        debug("callee_class=%s, methods_count=%d", get_class_name(callee_cp, callee_class->this_class), callee_class->methods_count);
        for (i = 0; i < callee_class->methods_count; i++) {
            method = (method_info*)(callee_class->methods[i]);
            tmp_method_name_utf8 = (CONSTANT_Utf8_info*)(callee_cp[method->name_index]);
            tmp_method_descriptor_utf8 = (CONSTANT_Utf8_info*)(callee_cp[method->descriptor_index]);
            //debug("method name = %s, type=%s", tmp_method_name_utf8->bytes, tmp_method_descriptor_utf8->bytes);
            if (method_name_utf8->length == tmp_method_name_utf8->length &&
                strcmp(method_name_utf8->bytes, tmp_method_name_utf8->bytes) == 0) {
                if (method_descriptor_utf8->length == tmp_method_descriptor_utf8->length &&
                    strcmp(method_descriptor_utf8->bytes, tmp_method_descriptor_utf8->bytes) == 0) {
                    debug("resolve method success, class=%s", get_class_name(callee_cp, callee_class->this_class));

                    mentry = newMethodEntry(callee_class, method);
                    debug("mentry: pclass=%p, method=%p", mentry->pclass, mentry->method);

                    addMethodEntry(method_ref->mtable, mentry);

                    found = 1;

                    break;
                }
            }
        }

        if (found) {
            break;
        }
        callee_class = callee_class->parent_class;
    } while (callee_class != NULL);

    if (!found) {
        printf("Error! cannot resolve method: %s.%s\n", method_name_utf8->bytes, method_descriptor_utf8->bytes);
        exit(1);
    }

    return mentry;
}

void callClassVirtualMethod(OPENV *current_env, int mindex)
{
    MethodEntry *mentry = NULL;
    Object *caller_obj;
    Class* current_class = current_env->current_class;
    cp_info cp = current_class->constant_pool;
    CONSTANT_Methodref_info* method_ref = (CONSTANT_Methodref_info*)(current_class->constant_pool[mindex]);
    CONSTANT_NameAndType_info *nt_info = (CONSTANT_NameAndType_info*)(current_class->constant_pool[method_ref->name_and_type_index]);

    if (NULL == method_ref->mtable) {
        method_ref->args_len = getMethodrefArgsLen(current_class, nt_info->descriptor_index);
        method_ref->mtable = newMethodTable();
    }

    caller_obj = *(Reference*)(current_env->current_stack->sp - ((method_ref->args_len+4)));
    printf("%p\n", caller_obj);
    debug("caller_obj=%p, class=%s", caller_obj, get_this_class_name(caller_obj->pclass));
    debug("current_class=%s", get_utf8(current_class->constant_pool[((CONSTANT_Class_info*)(current_class->constant_pool[current_class->this_class]))->name_index]));
    debug("method class index=%d", method_ref->class_index);

    if(NULL == (mentry = findMethodEntry(method_ref->mtable, caller_obj->pclass))) {
        debug("method_ref=%p, mtable=%p", method_ref, method_ref->mtable);
        debug("name_index=%d,%p, desc_index=%d\n", nt_info->name_index, (CONSTANT_Utf8_info*)(cp[nt_info->name_index]), nt_info->descriptor_index);
        mentry = resolveClassVirtualMethod(caller_obj->pclass, &method_ref, (CONSTANT_Utf8_info*)(cp[nt_info->name_index]), (CONSTANT_Utf8_info*)(cp[nt_info->descriptor_index]));
    }
    printf("caller_obj=%p, caller_obj_class=%s\n", caller_obj, get_this_class_name(mentry->pclass));

    callResolvedClassVirtualMethod(current_env, method_ref, mentry);
}

void resolveClassStaticField(Class* caller_class, CONSTANT_Fieldref_info **pfield_ref)
{
    Class* callee_class;
    cp_info callee_cp, caller_cp;
    CONSTANT_Fieldref_info* field_ref = *pfield_ref;
    CONSTANT_NameAndType_info* field_nt_info;
    CONSTANT_Utf8_info* field_name_utf8, *field_descriptor_utf8, *tmp_field_name_utf8, *tmp_field_descriptor_utf8;
    CONSTANT_Class_info *field_ref_class_info;
    field_info *field;
    int i, found =0, fields_count;

    caller_cp = caller_class->constant_pool;
    field_ref_class_info = (CONSTANT_Class_info*)(caller_cp[field_ref->class_index]);
    callee_class = field_ref_class_info->pclass;
    if (NULL == callee_class) {
        printf("NULL class");exit(1);
    }

    field_nt_info = (CONSTANT_NameAndType_info*)(caller_cp[field_ref->name_and_type_index]);
    field_name_utf8 = (CONSTANT_Utf8_info*)(caller_cp[field_nt_info->name_index]);
    field_descriptor_utf8 = (CONSTANT_Utf8_info*)(caller_cp[field_nt_info->descriptor_index]);

    do {
        callee_cp = callee_class->constant_pool;
        fields_count = callee_class->fields_count;
        for (i = 0; i < fields_count; i++) {
            field = (field_info*)(callee_class->fields[i]);
            tmp_field_name_utf8 = (CONSTANT_Utf8_info*)(callee_cp[field->name_index]);
            //debug("field name=%s access_flags=%x, %x", tmp_field_name_utf8->bytes, field->access_flags, field->access_flags & ACC_STATIC);
            if (IS_ACC_STATIC(field->access_flags) &&
                field_name_utf8->length == tmp_field_name_utf8->length &&
                strcmp(field_name_utf8->bytes, tmp_field_name_utf8->bytes) == 0) {
                tmp_field_descriptor_utf8 = (CONSTANT_Utf8_info*)(callee_cp[field->descriptor_index]);
                if (field_descriptor_utf8->length == tmp_field_descriptor_utf8->length &&
                    strcmp(field_descriptor_utf8->bytes, tmp_field_descriptor_utf8->bytes) == 0) {
                    field_ref->ftype = field->ftype;
                    field_ref->findex = field->findex;
                    found = 1;

                    debug("field resolve success, class=%s", get_class_name(callee_cp, callee_class->this_class));
                    debug("field index=%d", field->findex);
                    break;
                }
            }
        }

        if (found) {
            break;
        }
        callee_class = callee_class->parent_class;
    } while(callee_class != NULL);

    if (!found) {
        printf("Error! cannot resolve field: %s.%s", field_name_utf8->bytes, field_descriptor_utf8->bytes);
        exit(1);
    }
}
void do_arraycopy(OPENV *env)
{
    int i;
    int length, destPos, srcPos;
    CArray_char *arr1, *arr2;
    GET_STACK(env->current_stack, length, int);
    GET_STACK(env->current_stack, destPos, int);
    GET_STACK(env->current_stack, arr2, CArray_char*);
    GET_STACK(env->current_stack, srcPos, int);
    GET_STACK(env->current_stack, arr1, CArray_char*);

    memcpy(arr2->elements+destPos, arr1->elements+srcPos, length);
}

int callNativeMethod(const char* method_name, OPENV *env)
{
    Object *obj;
    CArray_char* arr_ref;
    int i=0;
    if (strcmp(method_name, "writeString") == 0) {
        obj = GET_STACKR(env->current_stack, obj, Reference);
        debug("obj=%p", obj);
        if (NULL != obj) {
            arr_ref = GET_FIELD(obj, 0, CArray_char*);
            printf("type=%d, length=%d\n", arr_ref->atype, arr_ref->length);
            printf("[OUT]: ");
            while(i<arr_ref->length) {
                putchar(arr_ref->elements[i]);
                i++;
            }
            printf("\n");
        }
    }
    return 0;
}

int resolveStaticClassMethod(Class* caller_class, CONSTANT_Methodref_info **pmethod_ref, OPENV *env)
{
    Class* callee_class;
    cp_info callee_cp, caller_cp;
    CONSTANT_Methodref_info* method_ref = *pmethod_ref;
    CONSTANT_NameAndType_info* method_nt_info;
    CONSTANT_Utf8_info* method_name_utf8, *method_descriptor_utf8, *tmp_method_name_utf8, *tmp_method_descriptor_utf8;
    CONSTANT_Class_info *method_ref_class_info;
    method_info *method;
    int i, found =0;

    caller_cp = caller_class->constant_pool;

    method_ref_class_info = (CONSTANT_Class_info*)(caller_cp[method_ref->class_index]);
    callee_class = method_ref_class_info->pclass;

    if (NULL == callee_class) {
        callee_class = method_ref_class_info->pclass = systemLoadClassRecursive(env, (CONSTANT_Utf8_info*)(caller_cp[method_ref_class_info->name_index]));
    }

    method_nt_info = (CONSTANT_NameAndType_info*)(caller_cp[method_ref->name_and_type_index]);
    method_name_utf8 = (CONSTANT_Utf8_info*)(caller_cp[method_nt_info->name_index]);
    method_descriptor_utf8 = (CONSTANT_Utf8_info*)(caller_cp[method_nt_info->descriptor_index]);

    printMethodrefInfo(caller_class, method_ref);
    do {
        callee_cp = callee_class->constant_pool;
        debug("callee_class=%s, methods_count=%d", get_class_name(callee_cp, callee_class->this_class), callee_class->methods_count);
        for (i = 0; i < callee_class->methods_count; i++) {
            method = (method_info*)(callee_class->methods[i]);
            tmp_method_name_utf8 = (CONSTANT_Utf8_info*)(callee_cp[method->name_index]);

            if (IS_ACC_STATIC(method->access_flags) &&
                method_name_utf8->length == tmp_method_name_utf8->length &&
                strcmp(method_name_utf8->bytes, tmp_method_name_utf8->bytes) == 0) {
                tmp_method_descriptor_utf8 = (CONSTANT_Utf8_info*)(callee_cp[method->descriptor_index]);
                if (method_descriptor_utf8->length == tmp_method_descriptor_utf8->length &&
                    strcmp(method_descriptor_utf8->bytes, tmp_method_descriptor_utf8->bytes) == 0) {
                    if (IS_ACC_NATIVE(method->access_flags)) {
                        if (strcmp(method_name_utf8->bytes, "arraycopy") == 0) {
                            debug("arraycopy: found %s method", method_name_utf8->bytes);
                            do_arraycopy(env);
                            return 0;
                        } else if (strcmp(get_this_class_name(callee_class), "test/IOUtil") == 0) {
                            printf("find self defined native class");
                            return callNativeMethod(method_name_utf8->bytes, env);

                            exit(1);
                        } else {
                            // "registerNatives"
                            debug("found unimplemented %s method, skip it", method_name_utf8->bytes);
                            system("pause");
                            return 0;
                        }
                    }

                    method_ref->ref_addr = method;
                    method_ref->args_len = method->args_len;
                    found = 1;

                    debug("resolve method success, class=%s", get_class_name(callee_cp, callee_class->this_class));

                    break;
                }
            }
        }

        if (found) {
            break;
        }
        callee_class = callee_class->parent_class;
    } while (callee_class != NULL);

    if (!found) {
        printf("Error! cannot resolve method: %s.%s\n", method_name_utf8->bytes, method_descriptor_utf8->bytes);
        exit(1);
    }

    return 1;
}

void callResolvedStaticClassMethod(OPENV* current_env, CONSTANT_Methodref_info* method_ref)
{
    StackFrame* stf, *last_stack;
    CONSTANT_Class_info* class_info;
    method_info* method;
    Code_attribute* code_attr;
    int real_args_len =0;

    debug("before call, current_class=%s", get_class_name(current_env->current_class->constant_pool, current_env->current_class->this_class));
    if (current_env->current_class->super_class) {
        debug("super_class=%s", get_class_name(current_env->current_class->constant_pool, current_env->current_class->super_class));
    }
    printMethodrefInfo(current_env->current_class, method_ref);


    last_stack= current_env->current_stack;
    // 1. create new stack frame
    method = (method_info*)(method_ref->ref_addr);
    code_attr = (Code_attribute*)(method->code_attribute_addr);
    stf = newStackFrame(last_stack, code_attr);
    debug("End create new stack frame, max_locals = %d", code_attr->max_locals);

    // 2. copy args
    real_args_len = method->args_len;
    if (real_args_len > 0) {
        last_stack->sp -= real_args_len;
        memcpy(stf->localvars, last_stack->sp, real_args_len);
        debug("args_len=%d", real_args_len);
    }

    // 3. save current environment
    stf->last_pc = current_env->pc;
    stf->last_pc_end = current_env->pc_end;
    stf->last_pc_start = current_env->pc_start;
    stf->last_class = current_env->current_class;
    stf->method = method;

    printf("End save current environment\n");

    // 4. set new environment
    class_info = (CONSTANT_Class_info*)(current_env->current_class->constant_pool[method_ref->class_index]);
    current_env->pc = current_env->pc_start = code_attr->code;
    current_env->pc_end = code_attr->code + code_attr->code_length;
    current_env->current_class = class_info->pclass;
    current_env->current_stack = stf;
    current_env->call_depth++;

    debug("real call: class_index=%d", method_ref->class_index);
    debug("real class name = %s", get_class_name(current_env->current_class->constant_pool, current_env->current_class->this_class));
}

void callStaticClassMethod(OPENV* current_env, int mindex)
{
    char *method_name;
    CONSTANT_NameAndType_info *nt_info;
    Class* current_class = current_env->current_class;
    CONSTANT_Methodref_info* method_ref = (CONSTANT_Methodref_info*)(current_class->constant_pool[mindex]);
    char *current_class_name = get_utf8(current_class->constant_pool[((CONSTANT_Class_info*)(current_class->constant_pool[current_class->this_class]))->name_index]);
    method_name = get_utf8(current_class->constant_pool[((CONSTANT_NameAndType_info*)(current_class->constant_pool[method_ref->name_and_type_index]))->name_index]);
    if (strncmp(current_class_name, "java", 4) == 0) {
        debug("skip static method of java lib class: %s", current_class_name);
        debug("method name=%s", method_name);
    }

    if (strcmp(method_name, "getPrimitiveClass") == 0) {
        debug("skip method: %s", method_name);

        return;
    }

    nt_info = (CONSTANT_NameAndType_info*)(current_class->constant_pool[method_ref->name_and_type_index]);

    debug("call static method, ref_addr=%p", method_ref->ref_addr);
    debug("current_class=%s", get_utf8(current_class->constant_pool[((CONSTANT_Class_info*)(current_class->constant_pool[current_class->this_class]))->name_index]));
    debug("method class index=%d", method_ref->class_index);

    if (NULL == method_ref->ref_addr) {
        if (0 == resolveStaticClassMethod(current_class, &method_ref, current_env)) {
            return;
        }
    }

    callResolvedStaticClassMethod(current_env, method_ref);
}

void resolveClassInstanceField(Class* caller_class, CONSTANT_Fieldref_info **pfield_ref)
{
    Class* callee_class;
    cp_info callee_cp, caller_cp;
    CONSTANT_Fieldref_info* field_ref = *pfield_ref;
    CONSTANT_NameAndType_info* field_nt_info;
    CONSTANT_Utf8_info* field_name_utf8, *tmp_field_name_utf8, *tmp_field_descriptor_utf8, *field_descriptor_utf8;
    CONSTANT_Class_info *field_ref_class_info;
    field_info *field;
    int i, found =0, fields_count;

    caller_cp = caller_class->constant_pool;
    field_ref_class_info = (CONSTANT_Class_info*)(caller_cp[field_ref->class_index]);
    callee_class = field_ref_class_info->pclass;
    if (NULL == callee_class) {
        printf("NULL class");exit(1);
    }

    field_nt_info = (CONSTANT_NameAndType_info*)(caller_cp[field_ref->name_and_type_index]);
    field_name_utf8 = (CONSTANT_Utf8_info*)(caller_cp[field_nt_info->name_index]);
    field_descriptor_utf8 = (CONSTANT_Utf8_info*)(caller_cp[field_nt_info->descriptor_index]);

    do {
        callee_cp = callee_class->constant_pool;
        fields_count = callee_class->fields_count;
        for (i = 0; i < fields_count; i++) {
            field = (field_info*)(callee_class->fields[i]);
            tmp_field_name_utf8 = (CONSTANT_Utf8_info*)(callee_cp[field->name_index]);
            if (NOT_ACC_STATIC(field->access_flags) &&
                field_name_utf8->length == tmp_field_name_utf8->length &&
                strcmp(field_name_utf8->bytes, tmp_field_name_utf8->bytes) == 0) {
                tmp_field_descriptor_utf8 = (CONSTANT_Utf8_info*)(callee_cp[field->descriptor_index]);
                if (field_descriptor_utf8->length == tmp_field_descriptor_utf8->length &&
                    strcmp(field_descriptor_utf8->bytes, tmp_field_descriptor_utf8->bytes) == 0) {
                    field_ref->ftype = field->ftype;
                    field_ref->findex = field->findex;
                    found = 1;

                    debug("field resolve success, class=%s", get_class_name(callee_cp, callee_class->this_class));
                    debug("field index=%d", field->findex);
                    break;
                }
            }
        }

        if (found) {
            break;
        }
        callee_class = callee_class->parent_class;
    } while(callee_class != NULL);

    if (!found) {
        field_name_utf8 = (CONSTANT_Utf8_info*)(caller_cp[field_nt_info->name_index]);
        printf("Error! cannot resolve field: %s%s", field_name_utf8->bytes);
        exit(1);
    }
}

void resolveClassSpecialMethod(Class* caller_class, CONSTANT_Methodref_info **pmethod_ref)
{
    Class* callee_class;
    cp_info callee_cp, caller_cp;
    CONSTANT_Methodref_info* method_ref = *pmethod_ref;
    CONSTANT_NameAndType_info* method_nt_info;
    CONSTANT_Utf8_info *method_name_utf8, *method_descriptor_utf8, *tmp_method_name_utf8,*tmp_method_descriptor_utf8;
    CONSTANT_Class_info *method_ref_class_info;
    method_info *method;
    int i, found =0;

    caller_cp = caller_class->constant_pool;
    method_ref_class_info = (CONSTANT_Class_info*)(caller_cp[method_ref->class_index]);
    callee_class = method_ref_class_info->pclass;
    if (NULL == callee_class) {
        callee_class = method_ref_class_info->pclass = systemLoadClass((CONSTANT_Utf8_info*)(caller_cp[method_ref_class_info->name_index]));
    }

    method_nt_info = (CONSTANT_NameAndType_info*)(caller_cp[method_ref->name_and_type_index]);
    method_name_utf8 = (CONSTANT_Utf8_info*)(caller_cp[method_nt_info->name_index]);
    method_descriptor_utf8 = (CONSTANT_Utf8_info*)(caller_cp[method_nt_info->descriptor_index]);

    printf("Begin resolve method:\n");
    printMethodrefInfo(caller_class, method_ref);
    do {
        callee_cp = callee_class->constant_pool;
        debug("callee_class=%s, methods_count=%d", get_class_name(callee_cp, callee_class->this_class), callee_class->methods_count);
        for (i = 0; i < callee_class->methods_count; i++) {
            method = (method_info*)(callee_class->methods[i]);
            tmp_method_name_utf8 = (CONSTANT_Utf8_info*)(callee_cp[method->name_index]);
            if (method_name_utf8->length == tmp_method_name_utf8->length &&
                strcmp(method_name_utf8->bytes, tmp_method_name_utf8->bytes) == 0) {
                tmp_method_descriptor_utf8 = (CONSTANT_Utf8_info*)(callee_cp[method->descriptor_index]);
                if (method_descriptor_utf8->length == tmp_method_descriptor_utf8->length &&
                    strcmp(method_descriptor_utf8->bytes, tmp_method_descriptor_utf8->bytes) == 0) {
                    method_ref->ref_addr = method;
                    method_ref->args_len = method->args_len;

                    found = 1;

                    debug("resolve method success, class=%s", get_class_name(callee_cp, callee_class->this_class));

                    break;
                }
            }
        }

        if (found) {
            break;
        }
        callee_class = callee_class->parent_class;
    } while (callee_class != NULL);

    if (!found) {
        method_name_utf8 = (CONSTANT_Utf8_info*)(caller_cp[method_nt_info->name_index]);
        method_descriptor_utf8 = (CONSTANT_Utf8_info*)(caller_cp[method_nt_info->descriptor_index]);
        printf("Error! cannot resolve method: %s.%s\n", method_name_utf8->bytes, method_descriptor_utf8->bytes);
        exit(1);
    }
}

void callResolvedClassSpecialMethod(OPENV* current_env, CONSTANT_Methodref_info* method_ref)
{
    Object *obj;
    StackFrame* stf, *last_stack;
    CONSTANT_Class_info* class_info;
    method_info* method;
    Code_attribute* code_attr;
    int real_args_len =0;

    debug("before call, current_class=%s", get_class_name(current_env->current_class->constant_pool, current_env->current_class->this_class));
    if (current_env->current_class->super_class) {
        debug("super_class=%s", get_class_name(current_env->current_class->constant_pool, current_env->current_class->super_class));
    }
    printMethodrefInfo(current_env->current_class, method_ref);


    last_stack= current_env->current_stack;
    // 1. create new stack frame
    method = (method_info*)(method_ref->ref_addr);
    code_attr = (Code_attribute*)(method->code_attribute_addr);
    stf = newStackFrame(last_stack, code_attr);
    debug("End create new stack frame, max_locals = %d", code_attr->max_locals);

    // 2. copy args
    real_args_len = method->args_len + SZ_REF;
    last_stack->sp -= real_args_len;
    memcpy(stf->localvars, last_stack->sp, real_args_len);
    obj = *(Object**)(stf->localvars);
    current_env->current_obj = obj;

    // 3. save current environment
    stf->last_pc = current_env->pc;
    stf->last_pc_end = current_env->pc_end;
    stf->last_pc_start = current_env->pc_start;
    stf->last_class = current_env->current_class;
    stf->method = method;

    // 4. set new environment
    class_info = (CONSTANT_Class_info*)(current_env->current_class->constant_pool[method_ref->class_index]);
    current_env->pc = current_env->pc_start = code_attr->code;
    current_env->pc_end = code_attr->code + code_attr->code_length;
    current_env->current_class = class_info->pclass;
    current_env->current_stack = stf;
    current_env->call_depth++;

    debug("real call: class_index=%d", method_ref->class_index);
    debug("real class name = %s", get_class_name(current_env->current_class->constant_pool, current_env->current_class->this_class));
}

void callClassSpecialMethod(OPENV* current_env, int mindex)
{
    Class* current_class = current_env->current_class;
    CONSTANT_Methodref_info* method_ref = (CONSTANT_Methodref_info*)(current_class->constant_pool[mindex]);

    debug("current_class=%s", get_utf8(current_class->constant_pool[((CONSTANT_Class_info*)(current_class->constant_pool[current_class->this_class]))->name_index]));
    debug("method class index=%d", method_ref->class_index);

    if (NULL == method_ref->ref_addr) {
        resolveClassSpecialMethod(current_class, &method_ref);
    }

    callResolvedClassSpecialMethod(current_env, method_ref);
}
