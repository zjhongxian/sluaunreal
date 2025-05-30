// Tencent is pleased to support the open source community by making sluaunreal available.

// Copyright (C) 2018 THL A29 Limited, a Tencent company. All rights reserved.
// Licensed under the BSD 3-Clause License (the "License"); 
// you may not use this file except in compliance with the License. You may obtain a copy of the License at

// https://opensource.org/licenses/BSD-3-Clause

// Unless required by applicable law or agreed to in writing, 
// software distributed under the License is distributed on an "AS IS" BASIS, 
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. 
// See the License for the specific language governing permissions and limitations under the License.

#pragma once

#include "SluaMicro.h"
#include "lua.h"
#include "lauxlib.h"
#include "UObject/UnrealType.h"
#include "UObject/GCObject.h"
#include "PropertyUtil.h"

namespace NS_SLUA {

    class SLUA_UNREAL_API LuaArray : public FGCObject {
    public:
        static void reg(lua_State* L);
        static void clone(FScriptArray* destArray, FProperty* p, const FScriptArray* srcArray);
        static void clone(FScriptArray* destArray, FArrayProperty* arrayP, const FScriptArray* srcArray);
        static int push(lua_State* L, FProperty* prop, FScriptArray* array, bool bIsNewInner);
        static int push(lua_State* L, FArrayProperty* prop, FScriptArray* data);
        static int push(lua_State* L, LuaArray* luaArray);

        template<typename T>
        static typename std::enable_if<DeduceType<T>::value != EPropertyClass::Struct, int>::type push(lua_State* L, const TArray<T>& v) {
            FProperty* prop = PropertyProto::createDeduceProperty<T>();
            auto array = reinterpret_cast<const FScriptArray*>(&v);
            return push(L, prop, const_cast<FScriptArray*>(array), false);
        }

        template<typename T>
        static typename std::enable_if<DeduceType<T>::value == EPropertyClass::Struct, int>::type push(lua_State* L, const TArray<T>& v)
        {
            FProperty* prop = PropertyProto::createDeduceProperty<T>(T::StaticStruct());
            auto array = reinterpret_cast<const FScriptArray*>(&v);
            return push(L, prop, const_cast<FScriptArray*>(array), false);
        }

        LuaArray(FProperty* prop, FScriptArray* buf, bool bIsRef, bool bIsNewInner);
        LuaArray(FArrayProperty* arrayProp, FScriptArray* buf, bool bIsRef, struct FLuaNetSerializationProxy* netProxy, uint16 replicatedIndex);
        ~LuaArray();

        FScriptArray* get() const {
            return array;
        }

        FProperty* getInnerProp() const
        {
            return inner;
        }

        static bool markDirty(LuaArray* luaArray);

        // Cast FScriptArray to TArray<T> if ElementSize matched
        template<typename T>
        const TArray<T>& asTArray(lua_State* L) const {
            if(sizeof(T)!= getPropertySize(inner))
                luaL_error(L,"Cast to TArray error, element size isn't mathed(%d,%d)", sizeof(T), getPropertySize(inner));
            return *(reinterpret_cast<const TArray<T>*>( array ));
        }

        virtual void AddReferencedObjects( FReferenceCollector& Collector ) override;

#if !((ENGINE_MINOR_VERSION<20) && (ENGINE_MAJOR_VERSION==4))
        virtual FString GetReferencerName() const override
        {
            return "LuaArray";
        }
#endif
        
    protected:
        static int __ctor(lua_State* L);
        static int Num(lua_State* L);
        static int Get(lua_State* L);
        static int Set(lua_State* L);
        static int Add(lua_State* L);
        static int AddUnique(lua_State* L);
        static int Remove(lua_State* L);
        static int Insert(lua_State* L);
        static int Clear(lua_State* L);
        static int Pairs(lua_State* L);
        static int PairsLessGC(lua_State* L);
        static int Iterate(lua_State* L);
        static int IterateReverse(lua_State* L);
        static int PushElement(lua_State* L, LuaArray* UD, int32 index);
        static int IterateLessGC(lua_State* L);
        static int IterateLessGCReverse(lua_State* L);
        static int PushElementLessGC(lua_State* L, LuaArray* UD, int32 index);
        static int CreateValueTypeObject(lua_State* L);

    private:
        FProperty* inner;
        FScriptArray* array;
        bool isRef;
        bool isNewInner;

        struct FLuaNetSerializationProxy* proxy;
        uint16 luaReplicatedIndex;

        void clear();
        uint8* getRawPtr(int index) const;
        bool isValidIndex(int index) const;
        uint8* insert(int index);
        uint8* add();
        void remove(int index);
        int num() const;
        void constructItems(int index,int count);
        void destructItems(int index,int count);      

        static int setupMT(lua_State* L);
        static int gc(lua_State* L);
    };
}