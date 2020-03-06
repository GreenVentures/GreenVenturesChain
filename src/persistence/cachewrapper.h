// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2017-2019 The GreenVenturesChain Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PERSIST_CACHEWRAPPER_H
#define PERSIST_CACHEWRAPPER_H

#include "accountdb.h"
#include "assetdb.h"
#include "blockdb.h"
#include "cdpdb.h"
#include "commons/uint256.h"
#include "contractdb.h"
#include "delegatedb.h"
#include "dexdb.h"
#include "pricefeeddb.h"
#include "sysparamdb.h"
#include "txdb.h"
#include "txreceiptdb.h"
#include "txutxodb.h"
#include "sysgoverndb.h"
#include "logdb.h"

class CCacheDBManager;

class CCacheWrapper {
public:
    CSysParamDBCache    sysParamCache;
    CBlockDBCache       blockCache;
    CAccountDBCache     accountCache;
    CAssetDBCache       assetCache;
    CContractDBCache    contractCache;
    CDelegateDBCache    delegateCache;
    CCdpDBCache         cdpCache;
    CClosedCdpDBCache   closedCdpCache;
    CDexDBCache         dexCache;
    CTxReceiptDBCache   txReceiptCache;
    CTxUTXODBCache      txUtxoCache;
    CSysGovernDBCache   sysGovernCache;

    CTxMemCache         txCache;
    CPricePointMemCache ppCache;
    CPriceFeedCache     priceFeedCache;
public:
    static std::shared_ptr<CCacheWrapper> NewCopyFrom(CCacheDBManager* pCdMan);
public:
    CCacheWrapper();

    CCacheWrapper(CCacheWrapper* cwIn);
    CCacheWrapper(CCacheDBManager* pCdMan);

    CCacheWrapper& operator=(CCacheWrapper& other);

    void CopyFrom(CCacheDBManager* pCdMan);

    void Flush();

    UndoDataFuncMap GetUndoDataFuncMap();

    void SetDbOpLogMap(CDBOpLogMap *pDbOpLogMap);
private:
    CCacheWrapper(const CCacheWrapper&) = delete;
    CCacheWrapper& operator=(const CCacheWrapper&) = delete;

};

class CCacheDBManager {
public:
    CDBAccess           *pSysParamDb;
    CSysParamDBCache    *pSysParamCache;

    CDBAccess           *pAccountDb;
    CAccountDBCache     *pAccountCache;

    CDBAccess           *pAssetDb;
    CAssetDBCache       *pAssetCache;

    CDBAccess           *pContractDb;
    CContractDBCache    *pContractCache;

    CDBAccess           *pDelegateDb;
    CDelegateDBCache    *pDelegateCache;

    CDBAccess           *pCdpDb;
    CCdpDBCache         *pCdpCache;

    CDBAccess           *pClosedCdpDb;
    CClosedCdpDBCache   *pClosedCdpCache;

    CDBAccess           *pDexDb;
    CDexDBCache         *pDexCache;

    CBlockIndexDB       *pBlockIndexDb;

    CDBAccess           *pBlockDb;
    CBlockDBCache       *pBlockCache;

    CDBAccess           *pLogDb;
    CLogDBCache         *pLogCache;

    CDBAccess           *pReceiptDb;
    CTxReceiptDBCache   *pReceiptCache;

    CDBAccess           *pUtxoDb;
    CTxUTXODBCache      *pUtxoCache;

    CDBAccess           *pSysGovernDb;
    CSysGovernDBCache   *pSysGovernCache;

    CDBAccess           *pPriceFeedDb;
    CPriceFeedCache     *pPriceFeedCache;

    CTxMemCache         *pTxCache;
    CPricePointMemCache *pPpCache;

public:
    CCacheDBManager(bool fReIndex, bool fMemory);

    ~CCacheDBManager();

    bool Flush();
};  // CCacheDBManager

#endif //PERSIST_CACHEWRAPPER_H
