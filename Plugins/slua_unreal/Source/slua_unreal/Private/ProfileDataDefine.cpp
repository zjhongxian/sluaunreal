﻿#include "ProfileDataDefine.h"

FProfileNameSet* FProfileNameSet::GlobalProfileNameSet = nullptr;
FLuaFunctionDefine* FLuaFunctionDefine::Root = new FLuaFunctionDefine();
FLuaFunctionDefine* FLuaFunctionDefine::Other = new FLuaFunctionDefine();

void FileMemInfo::Serialize(FArchive& Ar)
{
    Ar << fileNameIndex;
    Ar << lineNumber;
    Ar << size;
    Ar << difference;
}


void FProflierMemNode::Serialize(FArchive& Ar)
{
    Ar << totalSize;

    if (Ar.IsLoading())
    {
        infoList.Empty();
        int32 infoChildNum = 0;
        Ar << infoChildNum;
        infoList.Reserve(infoChildNum);

        for (int i = 0; i < infoChildNum; ++i)
        {
        	TMap<int, FileMemInfo> node;
            int32 nodeChildeNum = 0;
            Ar << nodeChildeNum;
            node.Reserve(nodeChildeNum);
            for (int childIndex = 0; childIndex < nodeChildeNum; ++childIndex)
            {
                int key;
                Ar << key;

                FileMemInfo fielMemInfo;
                fielMemInfo.Serialize(Ar);

                node.Add(key, fielMemInfo);
            }
            uint32 outKey;
            Ar << outKey;
            infoList.Add(outKey, node);
        }

        parentFileMap.Empty();
        int32 parentFileChildNum = 0;
        Ar << parentFileChildNum;
        parentFileMap.Reserve(parentFileChildNum);

        for (int i = 0; i < parentFileChildNum; ++i)
        {
            uint32 key;
            Ar << key;
            FileMemInfo value;
            value.Serialize(Ar);
            parentFileMap.Add(key, value);
        }
    }
    else if (Ar.IsSaving())
    {

        int32 infoListNum = infoList.Num();
        Ar << infoListNum;

        for (auto infoMap : infoList)
        {
            int32 mapNum = infoMap.Value.Num();
            Ar << mapNum;

            for (auto infoItem : infoMap.Value)
            {
                Ar << infoItem.Key;
                infoItem.Value.Serialize(Ar);
            }
            Ar << infoMap.Key;
        }

        int32 parentFileChildNum = parentFileMap.Num();
        Ar << parentFileChildNum;

        for (auto fileMap : parentFileMap)
        {
            Ar << fileMap.Key;
            fileMap.Value.Serialize(Ar);
        }
    }
}


void FunctionProfileNode::Serialize(FArchive& Ar)
{
    Ar << functionDefine;
    Ar << costTime;
    Ar << countOfCalls;
    Ar << layerIdx;

    if (Ar.IsLoading())
    {
        childNode = MakeShared<FChildNode>();
        int32 childNum = 0;
        Ar << childNum;

        for (int i = 0; i < childNum; ++i)
        {
            FLuaFunctionDefine childFuncDefine;
            TSharedPtr<FunctionProfileNode> node = MakeShared<FunctionProfileNode>();
            Ar << childFuncDefine;
            node->Serialize(Ar);
            childNode->Add(childFuncDefine, node);
        }
    }
    else if (Ar.IsSaving() && childNode.IsValid())
    {
        int32 childNum = childNode->Num();
        Ar << childNum;

        for (auto iter : *childNode)
        {
            auto childFuncDefine = iter.Key;
            auto node = iter.Value;
            Ar << childFuncDefine;
            if (iter.Value.IsValid())
            {
                node->Serialize(Ar);
            }
            else
            {
                TSharedPtr<FunctionProfileNode> emptyNode = MakeShared<FunctionProfileNode>();
                emptyNode->Serialize(Ar);
            }
        }
    }
}

void FunctionProfileNode::CopyData(TSharedPtr<FunctionProfileNode> NewData)
{
    NewData->functionDefine = this->functionDefine;
    NewData->costTime = this->costTime;
    NewData->countOfCalls = this->countOfCalls;
    NewData->layerIdx = this->layerIdx;
    for (auto ChildNodeInfo : *this->childNode)
    {
        TSharedPtr<FunctionProfileNode> NewChildNode = MakeShared<FunctionProfileNode>();
        ChildNodeInfo.Value->CopyData(NewData);

        TMap<FString, TSharedPtr<FunctionProfileNode>> NewMapNode;
        NewData->childNode->Add(ChildNodeInfo.Key, NewData);
    }
}
