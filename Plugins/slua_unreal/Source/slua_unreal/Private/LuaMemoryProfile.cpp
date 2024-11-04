// Tencent is pleased to support the open source community by making sluaunreal available.

// Copyright (C) 2018 THL A29 Limited, a Tencent company. All rights reserved.
// Licensed under the BSD 3-Clause License (the "License"); 
// you may not use this file except in compliance with the License. You may obtain a copy of the License at

// https://opensource.org/licenses/BSD-3-Clause

// Unless required by applicable law or agreed to in writing, 
// software distributed under the License is distributed on an "AS IS" BASIS, 
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. 
// See the License for the specific language governing permissions and limitations under the License.


#include "LuaMemoryProfile.h"
#include "LuaState.h"
#include "Log.h"
#include "lstate.h"
#include "LuaProfiler.h"
#include "HAL/LowLevelMemTracker.h"

namespace NS_SLUA {

    // only calc memory alloc from lua script
    // not include alloc from lua vm
    size_t totalMemory;
    
    TMap<LuaState*, MemoryDetail> memoryRecord;
    TMap<LuaState*, TArray<LuaMemInfo>> memoryIncreaseThisFrame;

    bool getMemInfo(LuaState* ls, size_t size, LuaMemInfo& info);

    MemoryDetail* TryGetMemoryRecord(LuaState* LS)
    {
        auto* memoryRecordDetail = memoryRecord.Find(LS);
        if (!memoryRecordDetail)
        {
            memoryRecordDetail = &memoryRecord.Add(LS);
        }
        return memoryRecordDetail;
    }

    TArray<LuaMemInfo>* TryGetMemoryIncrease(LuaState* LS)
    {
        auto* memoryIncrease = memoryIncreaseThisFrame.Find(LS);
        if (!memoryIncrease)
        {
            memoryIncrease = &memoryIncreaseThisFrame.Add(LS);
        }
        return memoryIncrease;
    }

    inline void addRecord(LuaState* LS, void* ptr, size_t size, LuaMemInfo &memInfo) {
        // skip if lua_State is null, lua_State hadn't binded to LS
        lua_State* L = LS->getLuaState();
        if (!L) return;

        // Log::Log("alloc memory %d from %s",size,TCHAR_TO_UTF8(*memInfo.hint));
        memInfo.ptr = (int64)ptr;
        memInfo.bAlloc = true;

        auto* memoryRecordDetail = TryGetMemoryRecord(LS);
        memoryRecordDetail->Add(ptr, memInfo);

        auto* memoryIncrease = TryGetMemoryIncrease(LS);
        memoryIncrease->Add(memInfo);
        totalMemory += size;
    }

    inline void removeRecord(LuaState* LS, void* ptr, size_t osize) {
        auto* memoryRecordDetail = TryGetMemoryRecord(LS);
        
        auto memInfoPtr = memoryRecordDetail->Find(ptr);
        if (memInfoPtr) {
            memInfoPtr->bAlloc = false;
            TryGetMemoryIncrease(LS)->Add(*memInfoPtr);
            memoryRecordDetail->Remove(ptr);
            // Log::Log("free memory %p size %d", ptr, osize);
            
            totalMemory -= osize;
        }
    }

    void* LuaMemoryProfile::alloc (void *ud, void *ptr, size_t osize, size_t nsize)
    {
        LuaState* ls = (LuaState*)ud;
        int memTrack = ls->memTrack;
        if (nsize == 0) 
        {
            if (memTrack) 
            {
                removeRecord(ls, ptr, osize);
            }
            FMemory::Free(ptr);
            return NULL;
        }
        else 
        {
            if (ptr && memTrack)
            {
                removeRecord(ls, ptr, osize);
            }
            
            static LuaMemInfo memInfo;
            bool bHasStack = false;
            if (memTrack)
            {
                // get stack before realloc to avoid luaD_reallocstack crash!
                bHasStack = getMemInfo(ls, nsize, memInfo);
            }
            ptr = FMemory::Realloc(ptr,nsize);
            if (bHasStack)
                addRecord(ls,ptr,nsize,memInfo);
            return ptr;
        }
    }

    size_t LuaMemoryProfile::total()
    {
        return totalMemory;
    }

    void LuaMemoryProfile::start(lua_State* L)
    {
        auto LS = LuaState::get(L);
        if (LS)
        {
            LS->memTrack = 1;
            onStart(LS);
        }
    }

    void LuaMemoryProfile::onStart(LuaState* LS)
    {
        auto *memRecord = TryGetMemoryRecord(LS);
        auto *memIncrease = TryGetMemoryIncrease(LS);
        memRecord->Empty();
        memIncrease->Empty();
    }

    void LuaMemoryProfile::stop(lua_State* L)
    {
        auto LS = LuaState::get(L);
        if (LS)
        {
            LS->memTrack = 0;
        }
    }

    void LuaMemoryProfile::tick(LuaState* LS)
    {
        auto *memoryIncrease = TryGetMemoryIncrease(LS);
        memoryIncrease->Empty();
    }

    const MemoryDetail& LuaMemoryProfile::memDetail(LuaState* LS)
    {
        auto *memoryRecordDetail = TryGetMemoryRecord(LS);
        return *memoryRecordDetail;
    }

    TArray<LuaMemInfo>& LuaMemoryProfile::memIncreaceThisFrame(LuaState* LS)
    {
        auto *memoryIncrease = TryGetMemoryIncrease(LS);
        return *memoryIncrease;
    }

    bool isCoroutineAlive(lua_State* co)
    {
        switch (lua_status(co)) {
        case LUA_YIELD:
            return false;
        case LUA_OK: {
            lua_Debug ar;
            if (lua_getstack(co, 0, &ar) > 0)
                return true;
            break;
        }
        default:
            break;
        }
        
        return false;
    }

    lua_CFunction getCFunction(lua_Debug &ar)
    {
        auto ci = ar.i_ci;
#if LUA_VERSION_NUM >= 504
#if LUA_VERSION_RELEASE_NUM >= 50406
        auto func = s2v(ci->func.p);
#else
        auto func = s2v(ci->func.p);
#endif
        if (ttislcf(func)) 
        {   
            return fvalue(func);
        }
        else if (ttisCclosure(func))
        {
            return clCvalue(func)->f;
        }
#else
        auto func = ci->func;
        if (ttislcf(func))
        {
            return fvalue(func);
        }
        else if (ttisCclosure(func))
        {
            return clCvalue(func)->f;
        }
#endif
        return nullptr;
    }

    bool getMemInfo(LuaState* ls, size_t size, LuaMemInfo& info) {
        // skip if lua_State is null, lua_State hadn't binded to LS
        lua_State* L = ls->getLuaState();
        if (!L) return false;

        info.ptr = 0;
        
        FString firstCName = TEXT("C");
        
        for (int i = 0;;i++) {
            lua_Debug ar;
#if LUA_VERSION_RELEASE_NUM >= 50406
            if (lua_getstack(L, i, &ar) && L->base_ci.func.p != nullptr && lua_getinfo(L, "nSl", &ar)) {
#else
            if (lua_getstack(L, i, &ar) && lua_getinfo(L, "nSl", &ar)) {
#endif
                if (strcmp(ar.what, "C") == 0) {
                    if (ar.name) {
                        firstCName += UTF8_TO_TCHAR(ar.name);
                        lua_CFunction cfunc = getCFunction(ar);
                        if (cfunc == LuaProfiler::resumeFunc) {
                            if (lua_isthread(L, 1)) {
                                lua_State* L1 = lua_tothread(L, 1);
                                if (isCoroutineAlive(L1)) {
                                    L = L1;
                                    i = -1;
                                    continue;
                                }
                            }
                        }
                    }
                }
    
                if (strcmp(ar.source, SLUA_LUACODE) == 0)
                    continue;

                if (strcmp(ar.source, LuaProfiler::ChunkName) == 0)
                    return false;
                
                if (strcmp(ar.what, "Lua") == 0 || strcmp(ar.what, "main") == 0) {
                    info.size = size;
                    info.hint = UTF8_TO_TCHAR(ar.source);
                    info.lineNumber = ar.currentline;
                    return true;
                }
            }
            else break;
        }

        info.size = size;
        info.hint = FString::Printf(TEXT("%s"), *firstCName);
        info.lineNumber = 0;
        return true;
    }

    void dumpMemoryDetail(FOutputDevice& Ar)
    {
        Ar.Logf(TEXT("Total memory alloc %d bytes"), totalMemory);
        for (auto& it : memoryRecord)
        {
            Ar.Logf(TEXT("Lua state %p"), it.Key);
            TMap<FString, int> MemAllocInfos;

            for (auto& itMemInfo : it.Value)
            {
                auto& memInfo = itMemInfo.Value;
                FString Key = FString::Printf(TEXT("%s:%d"), *memInfo.hint, memInfo.lineNumber);
                if (int* pSize = MemAllocInfos.Find(Key))
                {
                    *pSize += memInfo.size;
                }
                else
                {
                    MemAllocInfos.Add(Key, memInfo.size);
                }
            }

            MemAllocInfos.ValueSort(TGreater<int32>());
            for (auto& MemAllocInfoIt : MemAllocInfos)
            {
                Ar.Logf(TEXT("MemAllocInfo %d from %s"), MemAllocInfoIt.Value, *MemAllocInfoIt.Key);
            }
        }
    }

    static FAutoConsoleCommandWithOutputDevice CVarDumpMemoryDetail(
        TEXT("slua.DumpMemoryDetail"),
        TEXT("Dump memory datail information"),
        FConsoleCommandWithOutputDeviceDelegate::CreateStatic(dumpMemoryDetail),
        ECVF_Default);
}
