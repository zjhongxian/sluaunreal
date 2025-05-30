// Tencent is pleased to support the open source community by making sluaunreal available.

// Copyright (C) 2018 THL A29 Limited, a Tencent company. All rights reserved.
// Licensed under the BSD 3-Clause License (the "License"); 
// you may not use this file except in compliance with the License. You may obtain a copy of the License at

// https://opensource.org/licenses/BSD-3-Clause

// Unless required by applicable law or agreed to in writing, 
// software distributed under the License is distributed on an "AS IS" BASIS, 
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. 
// See the License for the specific language governing permissions and limitations under the License.

#ifdef _WIN32
#pragma warning (push)
#pragma warning (disable : 4018)
#endif

#include "LuaVar.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"
#include "UObject/Stack.h"
#include "LuaState.h"
#include "lstate.h"
// #include "CrashContextCollector.h" // For PUBG Mobile

namespace NS_SLUA {

    FSafeOutputDelegate LuaVar::safeOutputLambda;

    const int INVALID_INDEX = -1;
    LuaVar::LuaVar()
        :stateIndex(INVALID_INDEX)
    {
        vars = nullptr;
        numOfVar = 0;
    }

    LuaVar::LuaVar(lua_Integer v)
        :LuaVar()
    {
        set(v);
    }

    LuaVar::LuaVar(int v)
        :LuaVar()
    {
        set((lua_Integer)v);
    }

    LuaVar::LuaVar(size_t v)
        :LuaVar()
    {
        set((lua_Integer)v);
    }

    LuaVar::LuaVar(lua_Number v)
        :LuaVar()
    {
        set(v);
    }

    LuaVar::LuaVar(bool v)
        :LuaVar()
    {
        set(v);
    }

    LuaVar::LuaVar(const char* v)
        :LuaVar()
    {
        set(v,strlen(v));
    }

    LuaVar::LuaVar(const char* v,size_t len)
        : LuaVar()
    {
        set(v, len);
    }

    LuaVar::LuaVar(lua_State* l,int p):LuaVar() {
        set(l,p);
    }

    void LuaVar::set(lua_State* l,int p) {
        free();
        int t = lua_type(l,p);
        LuaVar::Type type;
        switch(t) {
            case LUA_TNUMBER:
                {
                if(lua_isinteger(l,p))
                    type = LV_INT;
                else
                    type = LV_NUMBER;
                }
                break;
            case LUA_TSTRING:
                type = LV_STRING;
                break;
            case LUA_TFUNCTION:
                type = LV_FUNCTION;
                break;
            case LUA_TTABLE:
                type = LV_TABLE;
                break;
            case LUA_TUSERDATA:
                type = LV_USERDATA;
                break;
            case LUA_TLIGHTUSERDATA:
                type = LV_LIGHTUD;
                break;
            case LUA_TBOOLEAN:
                type = LV_BOOL;
                break;
            case LUA_TNIL:
            default:
                type = LV_NIL;
                break;
        }
        init(l,p,type);
    }

    LuaVar::LuaVar(lua_State* l,int p,LuaVar::Type type):LuaVar() {
        init(l,p,type);
    }

    // used to create number n of tuple
    // it used for return value from lua
    // don't call it to create n element of tuple
    LuaVar::LuaVar(lua_State* l,size_t n):LuaVar() {
        init(l,n,LV_TUPLE);
    }

    lua_State* LuaVar::getState() const
    {
        auto ls = LuaState::get(stateIndex);
        return ls ? ls->getLuaState() : nullptr;
    }

    void LuaVar::init(lua_State* l,int p,LuaVar::Type type) {
        auto state = LuaState::get(l);
        stateIndex = state->stateIndex();
        switch(type) {
        case LV_NIL:
            break;
        case LV_INT:
            set(lua_tointeger(l,p));
            break;
        case LV_NUMBER:
            set(lua_tonumber(l,p));
            break;
        case LV_STRING: {
            size_t len;
            const char* buf = lua_tolstring(l, p, &len);
            set(buf,len);
            break;
        }
        case LV_BOOL:
            set(!!lua_toboolean(l,p));
            break;
        case LV_LIGHTUD:
            alloc(1);
            vars[0].ptr = lua_touserdata(l,p);
            vars[0].luatype = type;
            break;
        case LV_FUNCTION: 
        case LV_TABLE:
        case LV_USERDATA:
            alloc(1);
            lua_pushvalue(l,p);
            vars[0].ref = new RefRef(l);
            vars[0].luatype=type;
            break;
        case LV_TUPLE:
            ensure(p>0 && lua_gettop(l)>=p);
            initTuple(l,p);
            break;
        default:
            break;
        }
    }

    bool LuaVar::operator==(const LuaVar& other) const
    {
        lua_State* L = getState();
        if (!L || L != other.getState()) { return false; }

        if (numOfVar != other.numOfVar) { return false; }
        if (numOfVar == 0 && vars == nullptr && other.vars == nullptr)
        {
            return true;
        }
        if (vars == nullptr || other.vars == nullptr) { return false; }

        for (size_t i = 0; i < numOfVar; i++) {
            Type luatype = vars[i].luatype;
            if (luatype != other.vars[i].luatype)
            {
                return false;
            }

            switch (luatype) {
                case LV_BOOL:
                {
                    if (vars[i].b != other.vars[i].b) {
                        return false;
                    }
                    break;
                }
                case LV_INT:
                {
                    if (vars[i].i != other.vars[i].i) {
                        return false;
                    }
                    break;
                }
                case LV_NUMBER:
                {
                    if (vars[i].d != other.vars[i].d) {
                        return false;
                    }
                    break;
                }
                case LV_STRING: 
                {
                    if (strcmp(vars[i].s->buf, other.vars[i].s->buf) != 0) {
                        return false;
                    }
                    break;
                }
                case LV_FUNCTION:
                case LV_TABLE:
                case LV_USERDATA:
                {
                    if (!vars[i].ref || !other.vars[i].ref) { return false; }

                    lua_rawgeti(L, LUA_REGISTRYINDEX, vars[i].ref->ref);
                    lua_rawgeti(L, LUA_REGISTRYINDEX, other.vars[i].ref->ref);
                    bool bEqual = lua_compare(L, -1, -2, LUA_OPEQ) == 1;
                    lua_pop(L, 2);
                    if (!bEqual)
                    {
                        return false;
                    }
                    break;
                }
                case LV_LIGHTUD:
                {
                    if (vars[i].ptr != other.vars[i].ptr) {
                        return false;
                    }
                    break;
                }
                case LUA_TNIL:
                default:
                    break;
            }
        }
        return true;
    }

    void LuaVar::initTuple(lua_State* l,size_t n) {
        ensure(lua_gettop(l)>=n);
        alloc(n);
        int f = lua_gettop(l)-n+1;
        for(size_t i=0;i<n;i++) {
            
            int p = i+f;
            int t = lua_type(l,p);

            switch(t) {
            case LUA_TBOOLEAN:
                vars[i].luatype = LV_BOOL;
                vars[i].b = !!lua_toboolean(l, p);
                break;
            case LUA_TNUMBER:
                {
                    if(lua_isinteger(l,p)) {
                        vars[i].luatype = LV_INT;
                        vars[i].i = lua_tointeger(l,p);
                    }
                    else {
                        vars[i].luatype = LV_NUMBER;
                        vars[i].d = lua_tonumber(l,p);
                    }
                }
                break;
            case LUA_TSTRING: {
                vars[i].luatype = LV_STRING;
                size_t len;
                const char* buf = lua_tolstring(l, p, &len);
                vars[i].s = new RefStr(buf,len);
                break;
            }
            case LUA_TFUNCTION:
                vars[i].luatype = LV_FUNCTION;
                lua_pushvalue(l,p);
                vars[i].ref = new RefRef(l);
                break;
            case LUA_TTABLE:
                vars[i].luatype = LV_TABLE;
                lua_pushvalue(l,p);
                vars[i].ref = new RefRef(l);
                break;
            case LUA_TUSERDATA:
                vars[i].luatype = LV_USERDATA;
                lua_pushvalue(l, p);
                vars[i].ref = new RefRef(l);
                break;
            case LUA_TLIGHTUSERDATA:
                vars[i].luatype = LV_LIGHTUD;
                vars[i].ptr = lua_touserdata(l, p);
                break;
            case LUA_TNIL:
            default:
                vars[i].luatype = LV_NIL;
                break;
            }
        }
    }

    LuaVar::~LuaVar() {
        free();
    }

    LuaVar::RefRef::RefRef(lua_State* l) 
        :LuaVar::Ref() 
    {
        ref=luaL_ref(l,LUA_REGISTRYINDEX);
        stateIndex = LuaState::get(l)->stateIndex();
#if UE_BUILD_DEVELOPMENT
        LuaState::get(l)->addRefTraceback(ref);
#endif
    }

    LuaVar::RefRef::~RefRef() {
        if(LuaState::isValid(stateIndex)) {
            auto state = LuaState::get(stateIndex);
            if (state) {
                luaL_unref(state->getLuaState(),LUA_REGISTRYINDEX,ref);
#if UE_BUILD_DEVELOPMENT
                state->removeRefTraceback(ref);
#endif
            }
        }
    }

    void LuaVar::free() {
        for(size_t n=0;n<numOfVar;n++) {
            if( (vars[n].luatype==LV_FUNCTION || vars[n].luatype==LV_TABLE || vars[n].luatype == LV_USERDATA)
                && vars[n].ref->isValid() )
                vars[n].ref->release();
            else if(vars[n].luatype==LV_STRING)
                vars[n].s->release();
        }
        numOfVar = 0;
        if (vars) {
            delete[] vars;
            vars = nullptr;
        }
    }

    void LuaVar::alloc(int n) {
        if(n>0) {
            vars = new lua_var[n];
            numOfVar = n;
        }
    }

    bool LuaVar::next(LuaVar& key,LuaVar& value) {
        if(!isTable())
            return false;

        auto L = getState();
        push(L);
        key.push(L);
        if(lua_next(L,-2)!=0) {
            key.set(L,-2);
            value.set(L,-1);
            lua_pop(L,3);
            return true;
        }
        else {
            key.free();
            value.free();
            lua_pop(L,1);
            return false;
        }
    }

    const char* LuaVar::toString() {
        auto L = getState();
        push(L);
        const char* ret;
        ret = luaL_tolstring(L,-1,NULL);
        lua_pop(L,2);
        return ret;
    }

    size_t LuaVar::count() const {
        if(isTable()) {
            auto L = getState();
            push(L);
            size_t n = lua_rawlen(L,-1);
            lua_pop(L,1);
            return n;
        }
        return numOfVar;
    }

    int LuaVar::asInt() const {
        ensure(numOfVar==1);
        switch(vars[0].luatype) {
        case LV_INT:
            return vars[0].i;
        case LV_NUMBER:
            return vars[0].d;
        default:
            return -1;
        }
    }

    int64 LuaVar::asInt64() const {
        ensure(numOfVar==1);
        switch(vars[0].luatype) {
        case LV_INT:
            return vars[0].i;
        case LV_NUMBER:
            return vars[0].d;
        default:
            return -1;
        }
    }

    float LuaVar::asFloat() const {
        ensure(numOfVar==1);
        switch(vars[0].luatype) {
        case LV_INT:
            return vars[0].i;
        case LV_NUMBER:
            return vars[0].d;
        default:
            return NAN;
        }
    }

    double LuaVar::asDouble() const {
        ensure(numOfVar==1);
        switch(vars[0].luatype) {
        case LV_INT:
            return vars[0].i;
        case LV_NUMBER:
            return vars[0].d;
        default:
            return NAN;
        }
    }

    const char* LuaVar::asString(size_t* outlen) const {
        ensure(numOfVar==1 && vars[0].luatype==LV_STRING);
        if(outlen) *outlen = vars[0].s->length;
        return vars[0].s->buf;
    }

    LuaLString LuaVar::asLString() const
    {
        ensure(numOfVar == 1 && vars[0].luatype == LV_STRING);
        return { vars[0].s->buf,vars[0].s->length };
    }

    bool LuaVar::asBool() const {
        ensure(numOfVar==1 && vars[0].luatype==LV_BOOL);
        return vars[0].b;
    }

    void* LuaVar::asLightUD() const {
        ensure(numOfVar==1 && vars[0].luatype==LV_LIGHTUD);
        return vars[0].ptr;
    }

    LuaVar LuaVar::getAt(size_t index) const {
        auto L = getState();
        if(isTable()) {
            push(L); // push this table
            lua_geti(L,-1,index); // get by index
            LuaVar r(L,-1); // construct LuaVar
            lua_pop(L,2); // pop table and value
            return r;
        }
        else {
            ensure(index>0);
            ensure(numOfVar>=index);
            LuaVar r;
            r.alloc(1);
            r.stateIndex = this->stateIndex;
            varClone(r.vars[0],vars[index-1]);
            return r;
        }
    }

    void LuaVar::set(lua_Integer v) {
        free();
        alloc(1);
        vars[0].i = v;
        vars[0].luatype = LV_INT;
    }

    void LuaVar::set(int v) {
        free();
        alloc(1);
        vars[0].i = v;
        vars[0].luatype = LV_INT;
    }

    void LuaVar::set(lua_Number v) {
        free();
        alloc(1);
        vars[0].d = v;
        vars[0].luatype = LV_NUMBER;
    }

    void LuaVar::set(const char* v,size_t len) {
        free();
        alloc(1);
        vars[0].s = new RefStr(v,len);
        vars[0].luatype = LV_STRING;
    }

    void LuaVar::set(const LuaLString & lstr)
    {
        set(lstr.buf, lstr.len);
    }

    void LuaVar::set(bool b) {
        free();
        alloc(1);
        vars[0].b = b;
        vars[0].luatype = LV_BOOL;
    }

    void LuaVar::pushVar(lua_State* l,const lua_var& ov) const {
        switch(ov.luatype) {
        case LV_INT:
            lua_pushinteger(l,ov.i);
            break;
        case LV_NUMBER:
            lua_pushnumber(l,ov.d);
            break;
        case LV_BOOL:
            lua_pushboolean(l,ov.b);
            break;
        case LV_STRING:
            lua_pushlstring(l,ov.s->buf,ov.s->length);
            break;
        case LV_FUNCTION:
        case LV_TABLE:
        case LV_USERDATA:
            ov.ref->push(l);
            break;
        case LV_LIGHTUD:
            lua_pushlightuserdata(l,ov.ptr);
            break;
        default:
            lua_pushnil(l);
            break;
        }
    }

    int LuaVar::push(lua_State* l) const {
        if(l==nullptr) l=getState();
        if(l==nullptr) return 0;

        if(vars==nullptr || numOfVar==0) {
            lua_pushnil(l);
            return 1;
        }
        
        if(numOfVar==1) {
            const lua_var& ov = vars[0];
            pushVar(l,ov);
            return 1;
        }
        for(size_t n=0;n<numOfVar;n++) {
            const lua_var& ov = vars[n];
            pushVar(l,ov);
        }
        return numOfVar;
    }

    bool LuaVar::isValid() const {
        return numOfVar>0 && stateIndex>0 && LuaState::isValid(stateIndex);
    }

    bool LuaVar::isNil() const {
        return vars==nullptr || numOfVar==0;
    }

    bool LuaVar::isFunction() const {
        return numOfVar==1 && vars[0].luatype==LV_FUNCTION;
    }

    bool LuaVar::isTuple() const {
        return numOfVar>1;
    }

    bool LuaVar::isTable() const {
        return numOfVar==1 && vars[0].luatype==LV_TABLE;
    }

    bool LuaVar::isInt() const {
        return numOfVar==1 && vars[0].luatype==LV_INT;
    }

    bool LuaVar::isNumber() const {
        return numOfVar==1 && vars[0].luatype==LV_NUMBER;
    }

    bool LuaVar::isBool() const {
        return numOfVar==1 && vars[0].luatype==LV_BOOL;
    }

    bool LuaVar::isUserdata(const char* t) const {
        if(numOfVar==1 && vars[0].luatype==LV_USERDATA) {
            auto L = getState();
            push(L);
            auto typeName = LuaObject::getType(L, -1);
            if (strcmp(typeName, t) != 0) {
                lua_pop(L,1);
                return false;
            }
            void* p = lua_touserdata(L, -1);
            lua_pop(L,1);
            return p!=nullptr;
        }
        return false;
    }

    bool LuaVar::isLightUserdata() const {
        return numOfVar==1 && vars[0].luatype==LV_LIGHTUD;
    }

    bool LuaVar::isString() const {
        return numOfVar==1 && vars[0].luatype==LV_STRING;
    }

    LuaVar::Type LuaVar::type() const {
        if(numOfVar==0)
            return LV_NIL;
        else if(numOfVar==1)
            return vars[0].luatype;
        else
            return LV_TUPLE;
    }

    int LuaVar::docall(const FillParamCallback& fillParam) const
    {
        if (!isValid()) {
            Log::Error("State of lua function is invalid");
            return 0;
        }
        auto L = getState();
        int errhandle = LuaState::pushErrorHandler(L);
        vars[0].ref->push(L);
        int argn = 0;
        if (fillParam) {
            argn = fillParam();
        }
        {
#if WITH_EDITOR
            LuaScriptCallGuard g(L);
#endif
            if (lua_pcallk(L, argn, LUA_MULTRET, errhandle, NULL, NULL))
                lua_pop(L, 1);
            lua_remove(L, errhandle); // remove err handler
        }
        return lua_gettop(L) - errhandle + 1;
    }

    int LuaVar::pushArgByParms(FProperty* prop,uint8* parms) {
        auto L = getState();
        if (LuaObject::push(L,prop,parms,nullptr))
            return getPropertySize(prop);
        return 0;
    }

    bool LuaVar::callByUFunction(UFunction* func,uint8* parms,FOutParmRec *outParams,LuaVar* pSelf) {
        
        if(!func) return false;

        if(!isValid()) {
            Log::Error("State of lua function is invalid");
            return false;
        }

        // For PUBG Mobile
        // GCrashContextCollector->AddCrashContextData(ECrashContextDataType::CVT_SEQ_CALL_FUNCTIONS, func->GetName(), false, ECrashContextDataLogLevel::CVLL_700);

        auto L = getState();

        const bool bHasReturnParam = func->ReturnValueOffset != MAX_uint16;
        if (func->ParmsSize == 0 && !bHasReturnParam) {
            auto fillParam = [&]
            {
                int nArg = 0;
                if (pSelf) {
                    pSelf->push();
                    nArg++;
                }
                return nArg;
            };

            int n = docall(fillParam);
            lua_pop(L, n);
            return true;
        }

        auto fillParam = [&]
        {
            // push self if valid
            int nArg = 0;
            if (pSelf) {
                pSelf->push();
                nArg++;
            }
            // push arguments to lua state
            for (TFieldIterator<FProperty> it(func); it && (it->PropertyFlags & CPF_Parm); ++it) {
                FProperty* prop = *it;
                uint64 propflag = prop->GetPropertyFlags();
                if (func->HasAnyFunctionFlags(FUNC_Native)) {
                    if (propflag & CPF_ReturnParm)
                        continue;
                }
                else if (IsRealOutParam(propflag))
                    continue;
                if (outParams && (propflag & CPF_OutParm) && (propflag & CPF_BlueprintReadOnly)) {
                    FOutParmRec* out = outParams;
                    while (out->Property != prop) {
                        out = out->NextOutParm;
                        checkSlow(out);
                    }
                    pushArgByParms(prop, out->PropAddr);
                    nArg++;
                }
                else {
                    pushArgByParms(prop, parms + prop->GetOffset_ForInternal());
                    nArg++;
                }
                
            }
            return nArg;
        };
        
        int retCount = docall(fillParam);
        int remain = retCount;

        auto checkOutputValue = [&](FProperty *prop) {
            auto checker = LuaObject::getChecker(prop);
            FOutParmRec* out = outParams;
            while (out && out->Property != prop) {
                out = out->NextOutParm;
            }
            uint8* outParam = out ? out->PropAddr : parms + prop->GetOffset_ForInternal();
            if (checker) {
                (*checker)(L, prop, outParam, lua_absindex(L, -remain), true);
            }
            remain--;
        };

        if (remain > 0)
        {
            safeOutputLambda = FSafeOutputDelegate::CreateLambda([&]()
            {
                // if lua return value
                // we only handle first lua return value
                if (bHasReturnParam) {
                    auto prop = func->GetReturnProperty();
                    checkOutputValue(prop);
                }

                // fill lua return value to blueprint stack if argument is out param
                for (TFieldIterator<FProperty> it(func); remain > 0 && it && (it->PropertyFlags & CPF_Parm); ++it) {
                    FProperty* prop = *it;
                    uint64 propflag = prop->GetPropertyFlags();
                    if (IsRealOutParam(propflag)) {
                        checkOutputValue(prop);
                    }
                }
            });
            
            if (!L->errfunc)
            {
                int errhandle = LuaState::pushErrorHandler(L);
                lua_pushcfunction(L, safeOutput);
                for (int i = 0; i < remain; ++i)
                    lua_pushvalue(L, -remain - 2);

                if (lua_pcall(L, remain, 0, errhandle) != 0) {
                    LuaState::get(L)->onError(TCHAR_TO_UTF8(
                        *FString::Printf(TEXT("Class[%s] function[%s] return type mismatch! error: %s"),
                            *func->GetOuter()->GetName(), *func->GetName(), UTF8_TO_TCHAR(lua_tostring(L, -1)))));

                    lua_pop(L, 1);
                }
                lua_pop(L, 1);
            }
            else 
            {
                auto errfunc = L->errfunc;
                L->errfunc = 0;

                try
                {
                    safeOutputLambda.Execute();
                }
                catch (...)
                {
                    LuaState::get(L)->onError(TCHAR_TO_UTF8(
                        *FString::Printf(TEXT("Class[%s] function[%s] return type mismatch! error: %s"),
                            *func->GetOuter()->GetName(), *func->GetName(), UTF8_TO_TCHAR(lua_tostring(L, -1)))));

                    lua_pop(L, 1); // pop error msg
                }

                L->errfunc = errfunc;
            }
        }

        // pop returned value
        lua_pop(L, retCount);
        return true;
    }

    int LuaVar::safeOutput(lua_State* L)
    {
        safeOutputLambda.Execute();
        return 0;
    }

    // clone luavar
    void LuaVar::varClone(lua_var& tv,const lua_var& ov) const {
        switch(ov.luatype) {
        case LV_INT:
            tv.i = ov.i;
            break;
        case LV_BOOL:
            tv.b = ov.b;
            break;
        case LV_NUMBER:
            tv.d = ov.d;
            break;
        case LV_STRING:
            tv.s = ov.s;
            tv.s->addRef();
            break;
        case LV_FUNCTION:
        case LV_TABLE:
        case LV_USERDATA:
            tv.ref = ov.ref;
            tv.ref->addRef();
            break;
        case LV_LIGHTUD:
            tv.ptr = ov.ptr;
            break;
        // nil and tuple not need to clone
        case LV_NIL:
        case LV_TUPLE:
            break;
        }
        tv.luatype = ov.luatype;
    }

    void LuaVar::clone(const LuaVar& other) {
        stateIndex = other.stateIndex;
        numOfVar = other.numOfVar;
        if(numOfVar>0 && other.vars) {
            vars = new lua_var[numOfVar];
            for(size_t n=0;n<numOfVar;n++) {
                varClone( vars[n], other.vars[n] );
            }
        }
    }

    void LuaVar::move(LuaVar&& other) {
        stateIndex = other.stateIndex;
        numOfVar = other.numOfVar;
        vars = other.vars;

        other.numOfVar = 0;
        other.vars = nullptr;
    }
}

#ifdef _WIN32
#pragma warning (pop)
#endif
