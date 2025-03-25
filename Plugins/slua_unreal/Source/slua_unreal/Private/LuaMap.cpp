// Tencent is pleased to support the open source community by making sluaunreal available.

// Copyright (C) 2018 THL A29 Limited, a Tencent company. All rights reserved.
// Licensed under the BSD 3-Clause License (the "License"); 
// you may not use this file except in compliance with the License. You may obtain a copy of the License at

// https://opensource.org/licenses/BSD-3-Clause

// Unless required by applicable law or agreed to in writing, 
// software distributed under the License is distributed on an "AS IS" BASIS, 
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. 
// See the License for the specific language governing permissions and limitations under the License.

#include "LuaMap.h"
#include "SluaLib.h"
#include "LuaObject.h"
#include "LuaState.h"
#include "LuaReference.h"
#include "LuaNetSerialization.h"

#define GET_CHECKER(tag) \
    auto tag##Checker = LuaObject::getChecker(UD->tag##Prop);\
    if (!tag##Checker) { \
        auto tn = UD->tag##Prop->GetClass()->GetName(); \
        luaL_error(L, "unsupport tag type %s to get", TCHAR_TO_UTF8(*tn)); \
        return 0; \
    }

namespace NS_SLUA {

    DefTypeName(LuaMap::Enumerator);

    void LuaMap::reg(lua_State* L) {
        SluaUtil::reg(L, "Map", __ctor);
    }

    int LuaMap::push(lua_State* L, FProperty* keyProp, FProperty* valueProp, FScriptMap* buf, bool bIsNewInner) {
        auto luaMap = new LuaMap(keyProp, valueProp, buf, false, bIsNewInner);
        return LuaObject::pushType(L, luaMap, "LuaMap", setupMT, gc);
    }

    int LuaMap::push(lua_State* L, LuaMap* luaMap)
    {
        return LuaObject::pushType(L,luaMap,"LuaMap",setupMT,gc);
    }

    void LuaMap::clone(FScriptMap* dest,FProperty* keyProp, FProperty* valueProp,const FScriptMap* src) {
        if(!src || (dest == src))
            return;

        FScriptMapHelper dstHelper = FScriptMapHelper::CreateHelperFormInnerProperties(keyProp,valueProp, dest);
        if (src->Num() == 0) {
            dstHelper.EmptyValues();
            return;
        }
        FScriptMapHelper srcHelper = FScriptMapHelper::CreateHelperFormInnerProperties(keyProp,valueProp, src);
        dstHelper.EmptyValues(srcHelper.Num());
        for (auto n = 0; n < srcHelper.GetMaxIndex(); n++) {
            if (srcHelper.IsValidIndex(n)) {
                auto keyPtr = srcHelper.GetKeyPtr(n);
                auto valuePtr = srcHelper.GetValuePtr(n);
                dstHelper.AddPair(keyPtr, valuePtr);
            }
        }
    }


    LuaMap::LuaMap(FProperty* kp, FProperty* vp, FScriptMap* buf, bool bIsRef, bool bIsNewInner)
        : map(bIsRef ? buf : new FScriptMap())
        , keyProp(kp)
        , valueProp(vp)
        , helper(FScriptMapHelper::CreateHelperFormInnerProperties(keyProp, valueProp, map))
        , isRef(bIsRef)
        , isNewInner(bIsNewInner)
        , proxy(nullptr)
        , luaReplicatedIndex(InvalidReplicatedIndex)
    {
        if (!bIsRef) {
            clone(map,kp,vp,buf);
        }
    }

    LuaMap::LuaMap(FMapProperty* p, FScriptMap* buf, bool bIsRef, FLuaNetSerializationProxy* netProxy, uint16 replicatedIndex)
        : map(bIsRef ? buf : new FScriptMap())
        , keyProp(p->KeyProp)
        , valueProp(p->ValueProp)
        , helper(FScriptMapHelper::CreateHelperFormInnerProperties(keyProp, valueProp, map))
        , isRef(bIsRef)
        , isNewInner(false)
        , proxy(netProxy)
        , luaReplicatedIndex(replicatedIndex)
    {
        if (!bIsRef) {
            clone(map,keyProp,valueProp,buf);
        }
    }

    LuaMap::~LuaMap() {
        if (!isRef) {
            clear();
            ensure(map);
            delete map;
            map = nullptr;
        }
        if (isNewInner)
        {
#if !((ENGINE_MINOR_VERSION<25) && (ENGINE_MAJOR_VERSION==4))
            delete keyProp;
            delete valueProp;
#endif
        }
        keyProp = valueProp = nullptr;
    }

    bool LuaMap::markDirty(LuaMap* luaMap)
    {
        auto proxy = luaMap->proxy;
        if (proxy)
        {
            proxy->dirtyMark.Add(luaMap->luaReplicatedIndex);
            proxy->assignTimes++;
            return true;
        }

        return false;
    }

    void LuaMap::AddReferencedObjects( FReferenceCollector& Collector )
    {
        if (keyProp) {
#if (ENGINE_MINOR_VERSION<25) && (ENGINE_MAJOR_VERSION==4)
            Collector.AddReferencedObject(keyProp);
#else
#if ENGINE_MAJOR_VERSION==5 && ENGINE_MINOR_VERSION >= 4
            TObjectPtr<UObject> ownerObject = keyProp->GetOwnerUObject();
#else
            auto ownerObject = keyProp->GetOwnerUObject();
#endif
            Collector.AddReferencedObject(ownerObject);
#endif
        }

        if (valueProp) {
#if (ENGINE_MINOR_VERSION<25) && (ENGINE_MAJOR_VERSION==4)
            Collector.AddReferencedObject(valueProp);
#else
#if ENGINE_MAJOR_VERSION==5 && ENGINE_MINOR_VERSION >= 4
            TObjectPtr<UObject> ownerObject = valueProp->GetOwnerUObject();
#else
            auto ownerObject = valueProp->GetOwnerUObject();
#endif
            Collector.AddReferencedObject(ownerObject);
#endif
        }

        // if is reference, skip AddReferencedObject
        if(isRef || num()<=0) return;

        if (!LuaReference::isRefProperty(keyProp) && !LuaReference::isRefProperty(valueProp))
            return;
        
        bool rehash = false;
        // for each valid entry of map
        for (int index = helper.GetMaxIndex()-1;index>=0; index--) {

            if (helper.IsValidIndex(index)) {
                auto pairPtr = helper.GetPairPtr(index);
                auto keyPtr = getKeyPtr(pairPtr);
                auto valuePtr = getValuePtr(pairPtr);

                bool keyChanged = LuaReference::addRefByProperty(Collector, keyProp, keyPtr);
                bool valuesChanged = LuaReference::addRefByProperty(Collector, valueProp, valuePtr);
                // if key auto null, we remove pair
                if (keyChanged || valuesChanged) {
                    removeAt(index);
                    rehash = true;
                }
            }
        }
        if (rehash) helper.Rehash();
    }

    uint8* LuaMap::getKeyPtr(uint8* pairPtr) {
#if (ENGINE_MINOR_VERSION<22) && (ENGINE_MAJOR_VERSION==4)
        return pairPtr + helper.MapLayout.KeyOffset;
#else
        return pairPtr;
#endif
    }

    uint8* LuaMap::getValuePtr(uint8* pairPtr) {
        return pairPtr + helper.MapLayout.ValueOffset;
    }

    void LuaMap::clear() {
        if(!keyProp || !valueProp)
            return;
        emptyValues();
    }

    // modified FScriptMapHelper::EmptyValues function to add value property offset on value ptr 
    void LuaMap::emptyValues(int32 Slack) {
        checkSlow(Slack >= 0);

        int32 OldNum = num();
        if(!isRef) {
            if (OldNum) {
                destructItems(0, OldNum);
            }
        }
        if (OldNum || Slack) {
            map->Empty(Slack, helper.MapLayout);
        }
    }

    // modified FScriptMapHelper::DestructItems function to add value property offset on value ptr 
    void LuaMap::destructItems(uint8* PairPtr, uint32 Stride, int32 Index, int32 Count, bool bDestroyKeys, bool bDestroyValues) {
        // if map is owned by uobject, don't destructItems
        if(isRef) return;
#if (ENGINE_MINOR_VERSION<22) && (ENGINE_MAJOR_VERSION==4)
        int32 keyOffset = helper.MapLayout.KeyOffset;
#else
        int32 keyOffset = 0;
#endif
        auto valueOffset = helper.MapLayout.ValueOffset;
        for (; Count; ++Index) {
            if (helper.IsValidIndex(Index)) {
                if (bDestroyKeys) {
                    keyProp->DestroyValue(PairPtr + keyOffset);
                }
                if (bDestroyValues) {
                    valueProp->DestroyValue(PairPtr + valueOffset);
                }
                --Count;
            }
            PairPtr += Stride;
        }
    }

    // modified FScriptMapHelper::DestructItems function to add value property offset on value ptr 
    void LuaMap::destructItems(int32 Index, int32 Count) {
        check(Index >= 0);
        check(Count >= 0);

        if (Count == 0) {
            return;
        }

        bool bDestroyKeys = !(keyProp->PropertyFlags & (CPF_IsPlainOldData | CPF_NoDestructor));
        bool bDestroyValues = !(valueProp->PropertyFlags & (CPF_IsPlainOldData | CPF_NoDestructor));

        if (bDestroyKeys || bDestroyValues) {
            uint32 Stride = helper.MapLayout.SetLayout.Size;
            uint8* PairPtr = (uint8*)map->GetData(Index, helper.MapLayout);
            destructItems(PairPtr, Stride, Index, Count, bDestroyKeys, bDestroyValues);
        }
    }

    // modified FScriptMapHelper::RemovePair function to call LuaMap::RemoveAt
    bool LuaMap::removePair(const void* KeyPtr) {
        FProperty* LocalKeyPropForCapture = keyProp;
        if (uint8* Entry = map->FindValue(KeyPtr, helper.MapLayout,
            [LocalKeyPropForCapture](const void* ElementKey) { return LocalKeyPropForCapture->GetValueTypeHash(ElementKey); },
            [LocalKeyPropForCapture](const void* A, const void* B) { return LocalKeyPropForCapture->Identical(A, B); }
        )) {
            int32 Idx = (Entry - (uint8*)map->GetData(0, helper.MapLayout)) / helper.MapLayout.SetLayout.Size;
            removeAt(Idx);
            return true;
        } else {
            return false;
        }
    }

    // modified FScriptMapHelper::RemoveAt function to call LuaMap::DestructItems
    void LuaMap::removeAt(int32 Index, int32 Count) {
        check(helper.IsValidIndex(Index));
        destructItems(Index, Count);
        for (; Count; ++Index) {
            if (helper.IsValidIndex(Index)) {
                map->RemoveAt(Index, helper.MapLayout);
                --Count;
            }
        }
    }

    int32 LuaMap::num() const {
        return helper.Num();
    }

    int LuaMap::__ctor(lua_State* L) {
        auto keyType = (EPropertyClass)LuaObject::checkValue<int>(L, 1);
        auto valueType = (EPropertyClass)LuaObject::checkValue<int>(L, 2);

        FProperty* keyProp;
        FProperty* valueProp;
        switch (keyType)
        {
        case EPropertyClass::Object:
            {
                auto cls = LuaObject::checkValueOpt<UClass*>(L, 3, nullptr);
                if (!cls)
                    luaL_error(L, "Map's UObject type of key should have 3rd parameter is UClass");
                keyProp = PropertyProto::createProperty(PropertyProto(keyType, cls));
            }
            break;
        case EPropertyClass::Struct:
            {
                auto scriptStruct = LuaObject::checkValueOpt<UScriptStruct*>(L, 3, nullptr);
                if (!scriptStruct)
                    luaL_error(L, "Map's Struct type of key should have 3rd parameter is UStruct");
                keyProp = PropertyProto::createProperty(PropertyProto(keyType, scriptStruct));
            }
            break;
        default:
            keyProp = PropertyProto::createProperty(PropertyProto(keyType));
            break;
        }

        switch (valueType)
        {
        case EPropertyClass::Object:
            {
                auto cls = LuaObject::checkValueOpt<UClass*>(L, 4, nullptr);
                if (!cls)
                    luaL_error(L, "UObject value should have 4th parameter is UClass");
                valueProp = PropertyProto::createProperty(PropertyProto(valueType, cls));
            }
            break;
        case EPropertyClass::Struct:
            {
                auto scriptStruct = LuaObject::checkValueOpt<UScriptStruct*>(L, 4, nullptr);
                if (!scriptStruct)
                    luaL_error(L, "Struct value should have 4th parameter is UStruct");
                valueProp = PropertyProto::createProperty(PropertyProto(valueType, scriptStruct));
            }
            break;
        default:
            valueProp = PropertyProto::createProperty(PropertyProto(valueType));
            break;
        }
        
        return push(L, keyProp, valueProp, nullptr, true);
    }

    int LuaMap::Num(lua_State* L) {
        CheckUD(LuaMap, L, 1);
        if (!UD) {
            luaL_error(L, "arg 1 expect LuaMap, but got nil!");
        }
        return LuaObject::push(L, UD->num());
    }

    int LuaMap::Get(lua_State* L)
    {
        int top = lua_gettop(L);

        CheckUD(LuaMap, L, 1);
        if (!UD)
        {
            luaL_error(L, "arg 1 expect LuaMap, but got nil!");
        }
        GET_CHECKER(key);
        FDefaultConstructedPropertyElement tempKey(UD->keyProp);
        auto keyPtr = tempKey.GetObjAddress();
        keyChecker(L, UD->keyProp, (uint8*)keyPtr, 2, true);

        auto valuePtr = UD->helper.FindValueFromHash(keyPtr);
        if (valuePtr)
        {
            auto prop = UD->valueProp;
            int outIndex = 0;
            if (top > 2)
            {
                for (int i = 3; i <= top; ++i)
                {
                    int keyType = lua_type(L, i);
                    if ((keyType == LUA_TNIL || keyType == LUA_TUSERDATA) && i == top)
                    {
                        outIndex = i;
                        break;
                    }

                    auto p = CastField<FStructProperty>(prop);
                    if (!p)
                    {
                        LuaObject::pushNil(L);
                        LuaObject::push(L, true);
                        return 2;
                    }

                    const char* key = lua_tostring(L, i);
                    prop = LuaObject::findCacheProperty(L, p->Struct, key);
                    if (!prop)
                    {
                        LuaObject::pushNil(L);
                        LuaObject::push(L, true);
                        return 2;
                    }
                    valuePtr = prop->ContainerPtrToValuePtr<uint8>(valuePtr);
                }
            }

            auto pusher = LuaObject::getPusher(prop);
            pusher(L, prop, valuePtr, outIndex, nullptr);
            LuaObject::push(L, true);
            return 2;
        }

        LuaObject::pushNil(L);
        LuaObject::push(L, false);
        return 2;
    }

    int LuaMap::Set(lua_State* L)
    {
        int top = lua_gettop(L);

        CheckUD(LuaMap, L, 1);
        if (!UD) 
        {
            luaL_error(L, "arg 1 expect LuaMap, but got nil!");
        }

        if (top < 3)
        {
            luaL_error(L, "expect 3 or more arguments, but got %d!", top);
        }

        GET_CHECKER(key);
        FDefaultConstructedPropertyElement tempKey(UD->keyProp);
        auto keyPtr = tempKey.GetObjAddress();
        keyChecker(L, UD->keyProp, (uint8*)keyPtr, 2, true);
        auto valuePtr = UD->helper.FindValueFromHash(keyPtr);

        if (!valuePtr)
        {
            luaL_error(L, "Map key[%s] not found!", lua_tostring(L, 2));
            return 0;
        }

        auto prop = UD->valueProp;
        for (int i = 3; i < top; ++i)
        {
            auto p = CastField<FStructProperty>(prop);
            if (!p)
            {
                luaL_error(L, "only struct property support but got %s", TCHAR_TO_UTF8(*p->GetName()));
            }

            const char* key = lua_tostring(L, i);
            prop = LuaObject::findCacheProperty(L, p->Struct, key);
            if (!prop)
            {
                luaL_error(L, "%s of %s's member not found.", key, TCHAR_TO_UTF8(*p->GetName()));
            }
            valuePtr = prop->ContainerPtrToValuePtr<uint8>(valuePtr);
        }

        auto valueChecker = LuaObject::getChecker(prop);
        valueChecker(L, prop, (uint8*)valuePtr, top, true);

        lua_pushboolean(L, 1);
        return 1;
    }

    int LuaMap::Add(lua_State* L) {
        CheckUD(LuaMap, L, 1);
        if (!UD) {
            luaL_error(L, "arg 1 expect LuaMap, but got nil!");
        }
        GET_CHECKER(key);
        GET_CHECKER(value);
        FDefaultConstructedPropertyElement tempKey(UD->keyProp);
        FDefaultConstructedPropertyElement tempValue(UD->valueProp);
        auto keyPtr = tempKey.GetObjAddress();
        auto valuePtr = tempValue.GetObjAddress();
        keyChecker(L, UD->keyProp, (uint8*)keyPtr, 2, true);
        valueChecker(L, UD->valueProp, (uint8*)valuePtr, 3, true);
        UD->helper.AddPair(keyPtr, valuePtr);

        markDirty(UD);

        return 0;
    }

    int LuaMap::Remove(lua_State* L) {
        CheckUD(LuaMap, L, 1);
        if (!UD) {
            luaL_error(L, "arg 1 expect LuaMap, but got nil!");
        }
        GET_CHECKER(key);
        FDefaultConstructedPropertyElement tempKey(UD->keyProp);
        auto keyPtr = tempKey.GetObjAddress();
        keyChecker(L, UD->keyProp, (uint8*)keyPtr, 2, true);

        markDirty(UD);

        return LuaObject::push(L, UD->removePair(keyPtr));
    }

    int LuaMap::Clear(lua_State* L) {
        CheckUD(LuaMap, L, 1);
        if (!UD) {
            luaL_error(L, "arg 1 expect LuaMap, but got nil!");
        }
        UD->clear();

        markDirty(UD);

        return 0;
    }

    int LuaMap::Pairs(lua_State* L) {
        CheckUD(LuaMap, L, 1);
        if (!UD) {
            luaL_error(L, "arg 1 expect LuaMap, but got nil!");
        }
        auto iter = new LuaMap::Enumerator();
        iter->map = UD;
        iter->index = 0;
        iter->num = UD->helper.Num();
        lua_pushcfunction(L, LuaMap::Enumerable);
        LuaObject::pushType(L, iter, "LuaMap::Enumerator", nullptr, LuaMap::Enumerator::gc, 1);
        // hold referrence of LuaMap, avoid gc
        lua_pushvalue(L, 1);
        lua_setuservalue(L, 3);
        LuaObject::pushNil(L);
        return 3;
    }

    int LuaMap::Enumerable(lua_State* L) {
        CheckUD(LuaMap::Enumerator, L, 1);
        auto map = UD->map;
        auto& helper = map->helper;
        do {
            if (UD->num <= 0) {
                return 0;
            } else if (helper.IsValidIndex(UD->index)) {
                auto pairPtr = helper.GetPairPtr(UD->index);
                auto keyPtr = map->getKeyPtr(pairPtr);
                auto valuePtr = map->getValuePtr(pairPtr);
                LuaObject::push(L, map->keyProp, keyPtr);
                LuaObject::push(L, map->valueProp, valuePtr);
                UD->index += 1;
                UD->num -= 1;
                return 2;
            } else {
                UD->index += 1;
            }
        } while (true);
    }

    int LuaMap::CreateValueTypeObject(lua_State* L) {
        CheckUD(LuaMap, L, 1);
        if (!UD) {
            luaL_error(L, "arg 1 expect LuaMap, but got nil!");
        }
        FFieldClass* uclass = UD->valueProp->GetClass();
        if (uclass) {
            FDefaultConstructedPropertyElement tempValue(UD->valueProp);
            auto valuePtr = tempValue.GetObjAddress();
            if (valuePtr) {
                return LuaObject::push(L, UD->valueProp, (uint8*)valuePtr);
            }
        }

        return 0;
    }

    int LuaMap::Enumerator::gc(lua_State* L) {
        CheckUD(LuaMap::Enumerator, L, 1);
        delete UD;
        return 0;
    }

    int LuaMap::gc(lua_State* L) {
        auto userdata = (UserData<LuaMap*>*)lua_touserdata(L, 1);
        auto self = userdata->ud;
        if (self->isRef) {
            LuaObject::unlinkProp(L, userdata);
        }
        delete self;
        return 0;
    }

    int LuaMap::setupMT(lua_State* L) {
        LuaObject::setupMTSelfSearch(L);

        RegMetaMethod(L, Pairs);
        RegMetaMethod(L, Num);
        RegMetaMethod(L, Get);
        RegMetaMethod(L, Set);
        RegMetaMethod(L, Add);
        RegMetaMethod(L, Remove);
        RegMetaMethod(L, Clear);
        RegMetaMethod(L, CreateValueTypeObject);

        RegMetaMethodByName(L, "__pairs", Pairs);

        return 0;
    }

}
