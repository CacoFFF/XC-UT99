/*===========================================================================
    C++ class definitions exported from UnrealScript.
    This is automatically generated by the tools.
    DO NOT modify this manually! Edit the corresponding .uc files instead!
===========================================================================*/
#if _MSC_VER
#pragma pack (push,4)
#define FArchive_Proxy FArchive
#endif

#ifndef XC_CORE_API
#define XC_CORE_API DLL_IMPORT
#endif

#ifndef NAMES_ONLY
#define AUTOGENERATE_NAME(name) extern XC_CORE_API FName XC_CORE_##name;
#define AUTOGENERATE_FUNCTION(cls,idx,name)
#endif


#ifndef NAMES_ONLY


class XC_CORE_API UBinarySerializer : public UObject
{
public:
    class FArchive_Proxy* Archive;
    BITFIELD bWrite:1 GCC_PACK(4);
    DECLARE_FUNCTION(execTotalSize);
    DECLARE_FUNCTION(execPosition);
    DECLARE_FUNCTION(execCloseFile);
    DECLARE_FUNCTION(execOpenFileWrite);
    DECLARE_FUNCTION(execOpenFileRead);
    DECLARE_FUNCTION(execSerializeTo);
    DECLARE_FUNCTION(execWriteText);
	DECLARE_FUNCTION(execReadLine);
    DECLARE_FUNCTION(execSerializeVector);
    DECLARE_FUNCTION(execSerializeRotator);
    DECLARE_FUNCTION(execSerializeByte);
    DECLARE_FUNCTION(execSerializeFloat);
    DECLARE_FUNCTION(execSerializeInt);
    DECLARE_FUNCTION(execSerializeString);
	
	void Destroy();
	
    DECLARE_CLASS(UBinarySerializer,UObject,0,XC_Core)
    NO_DEFAULT_CONSTRUCTOR(UBinarySerializer)
};


class XC_CORE_API UXC_CoreStatics : public UObject
{
public:
    BITFIELD bGNatives:1 GCC_PACK(4);
    INT XC_Core_Version GCC_PACK(4);
    INT XC_Engine_Version;
    FLOAT iC[2];
    DECLARE_FUNCTION(execInvSqrt);
    DECLARE_FUNCTION(execHSize);
    DECLARE_FUNCTION(execHNormal);
    DECLARE_FUNCTION(execUnClock);
    DECLARE_FUNCTION(execClock);
    DECLARE_FUNCTION(execOr_ObjectObject);
    DECLARE_FUNCTION(execConnectedDests);
    DECLARE_FUNCTION(execAppSeconds);
    DECLARE_FUNCTION(execAppCycles);
    DECLARE_FUNCTION(execGetParentClass);
    DECLARE_FUNCTION(execStringToName);
    DECLARE_FUNCTION(execLocs);
    DECLARE_FUNCTION(execMakeColor);
	DECLARE_FUNCTION(execFindObject);
	DECLARE_FUNCTION(execAllObjects);
	DECLARE_FUNCTION(execHasFunction);
	DECLARE_FUNCTION(execFixName);
	DECLARE_FUNCTION(execDynamicLoadObject_Fix);

	void StaticConstructor();

    DECLARE_CLASS(UXC_CoreStatics,UObject,0,XC_Core)
    NO_DEFAULT_CONSTRUCTOR(UXC_CoreStatics)
};

#endif

AUTOGENERATE_FUNCTION(UBinarySerializer,-1,execTotalSize);
AUTOGENERATE_FUNCTION(UBinarySerializer,-1,execPosition);
AUTOGENERATE_FUNCTION(UBinarySerializer,-1,execCloseFile);
AUTOGENERATE_FUNCTION(UBinarySerializer,-1,execOpenFileWrite);
AUTOGENERATE_FUNCTION(UBinarySerializer,-1,execOpenFileRead);
AUTOGENERATE_FUNCTION(UBinarySerializer,-1,execSerializeTo);
AUTOGENERATE_FUNCTION(UBinarySerializer,-1,execWriteText);
AUTOGENERATE_FUNCTION(UBinarySerializer,-1,execReadLine);
AUTOGENERATE_FUNCTION(UBinarySerializer,-1,execSerializeVector);
AUTOGENERATE_FUNCTION(UBinarySerializer,-1,execSerializeRotator);
AUTOGENERATE_FUNCTION(UBinarySerializer,-1,execSerializeByte);
AUTOGENERATE_FUNCTION(UBinarySerializer,-1,execSerializeFloat);
AUTOGENERATE_FUNCTION(UBinarySerializer,-1,execSerializeInt);
AUTOGENERATE_FUNCTION(UBinarySerializer,-1,execSerializeString);
AUTOGENERATE_FUNCTION(UXC_CoreStatics,-1,execInvSqrt);
AUTOGENERATE_FUNCTION(UXC_CoreStatics,-1,execHSize);
AUTOGENERATE_FUNCTION(UXC_CoreStatics,-1,execHNormal);
AUTOGENERATE_FUNCTION(UXC_CoreStatics,-1,execUnClock);
AUTOGENERATE_FUNCTION(UXC_CoreStatics,-1,execClock);
AUTOGENERATE_FUNCTION(UXC_CoreStatics,-1,execOr_ObjectObject);
AUTOGENERATE_FUNCTION(UXC_CoreStatics,-1,execConnectedDests);
AUTOGENERATE_FUNCTION(UXC_CoreStatics,-1,execAppSeconds);
AUTOGENERATE_FUNCTION(UXC_CoreStatics,-1,execAppCycles);
AUTOGENERATE_FUNCTION(UXC_CoreStatics,-1,execGetParentClass);
AUTOGENERATE_FUNCTION(UXC_CoreStatics,-1,execStringToName);
AUTOGENERATE_FUNCTION(UXC_CoreStatics,-1,execLocs);
AUTOGENERATE_FUNCTION(UXC_CoreStatics,-1,execMakeColor);
AUTOGENERATE_FUNCTION(UXC_CoreStatics,-1,execFindObject);
AUTOGENERATE_FUNCTION(UXC_CoreStatics,-1,execAllObjects);
AUTOGENERATE_FUNCTION(UXC_CoreStatics,-1,execHasFunction);
AUTOGENERATE_FUNCTION(UXC_CoreStatics,-1,execFixName);
AUTOGENERATE_FUNCTION(UXC_CoreStatics,-1,execDynamicLoadObject_Fix);

#ifndef NAMES_ONLY
#undef AUTOGENERATE_NAME
#undef AUTOGENERATE_FUNCTION
#endif NAMES_ONLY

#if _MSC_VER
#pragma pack (pop)
#endif