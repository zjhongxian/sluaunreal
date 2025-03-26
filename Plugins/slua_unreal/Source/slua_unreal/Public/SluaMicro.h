#pragma once

#include <lstate.h>
#ifdef G
    #undef G
#endif

#ifndef LUA_VERSION_RELEASE_NUM
#define LUA_VERSION_RELEASE_NUM (LUA_VERSION_NUM * 100 + 0)
#endif

#include "CoreMinimal.h"
#include "UObject/CoreNative.h"
#include "Runtime/Launch/Resources/Version.h"

namespace NS_SLUA
{
    typedef lua_State lua_State;
#ifndef UE_5_5_OR_LATER
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5
#define UE_5_5_OR_LATER 1
#else
#define UE_5_5_OR_LATER 0
#endif
#endif

#if (ENGINE_MINOR_VERSION<25) && (ENGINE_MAJOR_VERSION==4)
    #define CastField Cast
    typedef UProperty FProperty;
    typedef UField FField;
    typedef UClass FFieldClass;
    
    typedef UIntProperty FIntProperty;
    typedef UUInt32Property FUInt32Property;
    typedef UInt64Property FInt64Property;
    typedef UUInt64Property FUInt64Property;
    typedef UInt16Property FInt16Property;
    typedef UUInt16Property FUInt16Property;
    typedef UInt8Property FInt8Property;
    typedef UByteProperty FByteProperty;
    typedef UFloatProperty FFloatProperty;
    typedef UDoubleProperty FDoubleProperty;
    typedef UBoolProperty FBoolProperty;
    typedef UTextProperty FTextProperty;
    typedef UStrProperty FStrProperty;
    typedef UNameProperty FNameProperty;
    typedef UMulticastDelegateProperty FMulticastDelegateProperty;
    typedef UObjectProperty FObjectProperty;
    typedef UEnumProperty FEnumProperty;
    typedef UArrayProperty FArrayProperty;
    typedef UMapProperty FMapProperty;
    typedef USetProperty FSetProperty;
    typedef USoftObjectProperty FSoftObjectProperty;
    typedef USoftClassProperty FSoftClassProperty;
    typedef UWeakObjectProperty FWeakObjectProperty;
    typedef UDelegateProperty FDelegateProperty;
    typedef UStructProperty FStructProperty;
    typedef UClassProperty FClassProperty;
    typedef UInterfaceProperty FInterfaceProperty;
#else
    typedef FProperty FProperty;
    typedef FStructProperty FStructProperty;
    typedef FMapProperty FMapProperty;
    typedef FArrayProperty FArrayProperty;
    typedef FField FField;
#endif

#if (ENGINE_MINOR_VERSION<19) && (ENGINE_MAJOR_VERSION==4)
    typedef Native FNativeFuncPtr;
#else
    typedef FNativeFuncPtr FNativeFuncPtr;
#endif

    inline int32 getPropertySize(const FProperty* prop)
    {
#if UE_5_5_OR_LATER
        return prop->GetElementSize();
#else
	    return prop->ElementSize;
#endif
    }

#if ENGINE_MAJOR_VERSION==5
    FORCEINLINE int32 getPropertyAlignment(FProperty* prop)
    {
        return prop->GetMinAlignment();
    }
#endif
}