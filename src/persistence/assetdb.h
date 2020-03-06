// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2017-2019 The GreenVenturesChain Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PERSIST_ASSETDB_H
#define PERSIST_ASSETDB_H

#include "commons/arith_uint256.h"
#include "dbconf.h"
#include "dbaccess.h"
#include "dbiterator.h"
#include "entities/asset.h"
#include "leveldbwrapper.h"

#include <map>
#include <string>
#include <utility>
#include <vector>


/*  CCompositeKVCache     prefixType            key              value           variable           */
/*  -------------------- --------------------   --------------  -------------   --------------------- */
    // <asset$tokenSymbol -> asset>
typedef CCompositeKVCache< dbk::ASSET,         TokenSymbol,        CAsset>      DBAssetCache;

class CUserAssetsIterator: public CDBIterator<DBAssetCache> {
public:
    typedef CDBIterator<DBAssetCache> Base;
    using Base::Base;

    const CAsset& GetAsset() const {
        return GetValue();
    }
};

class CAssetDBCache {
public:
    CAssetDBCache() {}

    CAssetDBCache(CDBAccess *pDbAccess) : assetCache(pDbAccess) {
        assert(pDbAccess->GetDbNameType() == DBNameType::ASSET);
    }

    ~CAssetDBCache() {}

public:
    bool GetAsset(const TokenSymbol &tokenSymbol, CAsset &asset);
    bool SetAsset(const CAsset &asset);
    bool HasAsset(const TokenSymbol &tokenSymbol);
    /**
     * check transfer coin symbol
     * return nullptr if succeed, else error msg
     */
    shared_ptr<string> CheckAssetSymbol(const TokenSymbol &symbol);

    bool Flush();

    uint32_t GetCacheSize() const { return assetCache.GetCacheSize(); }

    void SetBaseViewPtr(CAssetDBCache *pBaseIn) {
        assetCache.SetBase(&pBaseIn->assetCache);
    }

    void SetDbOpLogMap(CDBOpLogMap *pDbOpLogMapIn) {
        assetCache.SetDbOpLogMap(pDbOpLogMapIn);
    }

    void RegisterUndoFunc(UndoDataFuncMap &undoDataFuncMap) {
        assetCache.RegisterUndoFunc(undoDataFuncMap);
    }

    shared_ptr<CUserAssetsIterator> CreateUserAssetsIterator() {
        return make_shared<CUserAssetsIterator>(assetCache);
    }

public:
/*  CCompositeKVCache     prefixType            key              value           variable           */
/*  -------------------- --------------------   --------------  -------------   --------------------- */
    // <asset_tokenSymbol -> asset>
    DBAssetCache   assetCache;
    // // <asset_trading_pair -> 1>
    // CCompositeKVCache< dbk::ASSET_TRADING_PAIR, CAssetTradingPair,  uint8_t>        assetTradingPairCache;
};

#endif  // PERSIST_ASSETDB_H
