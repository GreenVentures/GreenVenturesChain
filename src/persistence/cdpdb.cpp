// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2017-2019 The GreenVenturesChain Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "cdpdb.h"
#include "persistence/dbiterator.h"

CCdpDBCache::CCdpDBCache(CDBAccess *pDbAccess)
    : cdpGlobalDataCache(pDbAccess),
      cdpCache(pDbAccess),
      userCdpCache(pDbAccess),
      cdpCoinPairsCache(pDbAccess),
      cdpRatioSortedCache(pDbAccess) {}

CCdpDBCache::CCdpDBCache(CCdpDBCache *pBaseIn)
    : cdpGlobalDataCache(pBaseIn->cdpGlobalDataCache),
      cdpCache(pBaseIn->cdpCache),
      userCdpCache(pBaseIn->userCdpCache),
      cdpCoinPairsCache(pBaseIn->cdpCoinPairsCache),
      cdpRatioSortedCache(pBaseIn->cdpRatioSortedCache) {}

bool CCdpDBCache::NewCDP(const int32_t blockHeight, CUserCDP &cdp) {
    assert(!cdpCache.HasData(cdp.cdpid));
    assert(!userCdpCache.HasData(make_pair(CRegIDKey(cdp.owner_regid), cdp.GetCoinPair())));

    return cdpCache.SetData(cdp.cdpid, cdp) &&
        userCdpCache.SetData(make_pair(CRegIDKey(cdp.owner_regid), cdp.GetCoinPair()), cdp.cdpid) &&
        SaveCDPToRatioDB(cdp);
}

bool CCdpDBCache::EraseCDP(const CUserCDP &oldCDP, const CUserCDP &cdp) {
    return cdpCache.SetData(cdp.cdpid, cdp) &&
        userCdpCache.EraseData(make_pair(CRegIDKey(cdp.owner_regid), cdp.GetCoinPair())) &&
        EraseCDPFromRatioDB(oldCDP);
}

// Need to delete the old cdp(before updating cdp), then save the new cdp if necessary.
bool CCdpDBCache::UpdateCDP(const CUserCDP &oldCDP, const CUserCDP &newCDP) {
    assert(!newCDP.IsEmpty());
    return cdpCache.SetData(newCDP.cdpid, newCDP) && EraseCDPFromRatioDB(oldCDP) && SaveCDPToRatioDB(newCDP);
}

bool CCdpDBCache::UserHaveCdp(const CRegID &regid, const TokenSymbol &assetSymbol, const TokenSymbol &scoinSymbol) {
    return userCdpCache.HasData(make_pair(CRegIDKey(regid), CCdpCoinPair(assetSymbol, scoinSymbol)));
}

bool CCdpDBCache::GetCDPList(const CRegID &regid, vector<CUserCDP> &cdpList) {

    CRegIDKey prefixKey(regid);
    CDBPrefixIterator<decltype(userCdpCache), CRegIDKey> dbIt(userCdpCache, prefixKey);
    dbIt.First();
    for(dbIt.First(); dbIt.IsValid(); dbIt.Next()) {
        CUserCDP userCdp;
        if (!cdpCache.GetData(dbIt.GetValue().value(), userCdp)) {
            return false; // has invalid data
        }

        cdpList.push_back(userCdp);
    }

    return true;
}

bool CCdpDBCache::GetCDP(const uint256 cdpid, CUserCDP &cdp) {
    return cdpCache.GetData(cdpid, cdp);
}

// Attention: update cdpCache and userCdpCache synchronously.
bool CCdpDBCache::SaveCDPToDB(const CUserCDP &cdp) {
    return cdpCache.SetData(cdp.cdpid, cdp);
}

bool CCdpDBCache::EraseCDPFromDB(const CUserCDP &cdp) {
    return cdpCache.EraseData(cdp.cdpid);
}

bool CCdpDBCache::SaveCDPToRatioDB(const CUserCDP &userCdp) {
    CCdpCoinPair cdpCoinPair = userCdp.GetCoinPair();
    CCdpGlobalData cdpGlobalData = GetCdpGlobalData(cdpCoinPair);

    cdpGlobalData.total_staked_assets += userCdp.total_staked_bcoins;
    cdpGlobalData.total_owed_scoins += userCdp.total_owed_scoins;

    if (!cdpGlobalDataCache.SetData(cdpCoinPair, cdpGlobalData)) return false;

    return cdpRatioSortedCache.SetData(MakeCdpRatioSortedKey(userCdp), userCdp);
}

bool CCdpDBCache::EraseCDPFromRatioDB(const CUserCDP &userCdp) {
    CCdpCoinPair cdpCoinPair = userCdp.GetCoinPair();
    CCdpGlobalData cdpGlobalData = GetCdpGlobalData(cdpCoinPair);

    cdpGlobalData.total_staked_assets -= userCdp.total_staked_bcoins;
    cdpGlobalData.total_owed_scoins -= userCdp.total_owed_scoins;

    cdpGlobalDataCache.SetData(cdpCoinPair, cdpGlobalData);

    return cdpRatioSortedCache.EraseData(MakeCdpRatioSortedKey(userCdp));
}

// global collateral ratio floor check

bool CCdpDBCache::GetCdpListByCollateralRatio(const CCdpCoinPair &cdpCoinPair,
        const uint64_t collateralRatio, const uint64_t bcoinMedianPrice,
        CdpRatioSortedCache::Map &userCdps) {
    double ratio = (double(collateralRatio) / RATIO_BOOST) / (double(bcoinMedianPrice) / PRICE_BOOST);
    assert(uint64_t(ratio * CDP_BASE_RATIO_BOOST) < UINT64_MAX);
    uint64_t ratioBoost = uint64_t(ratio * CDP_BASE_RATIO_BOOST) + 1;
    CdpRatioSortedCache::KeyType endKey(cdpCoinPair, ratioBoost, 0, uint256());

    return cdpRatioSortedCache.GetAllElements(endKey, userCdps);
}

CCdpGlobalData CCdpDBCache::GetCdpGlobalData(const CCdpCoinPair &cdpCoinPair) const {
    CCdpGlobalData ret;
    cdpGlobalDataCache.GetData(cdpCoinPair, ret);
    return ret;
}

bool CCdpDBCache::GetCdpCoinPairStatus(const CCdpCoinPair &cdpCoinPair, CdpCoinPairStatus &status) {
    // TODO: GetDefaultData
    uint8_t value;
    if (!cdpCoinPairsCache.GetData(cdpCoinPair, value)) {
        if (kCDPCoinPairSet.count(make_pair(cdpCoinPair.bcoin_symbol, cdpCoinPair.scoin_symbol)) > 0) {
            status = CdpCoinPairStatus::NORMAL;
            return true;
        }
        return false;
    }
    status = (CdpCoinPairStatus)value;
    return true;
}

map<CCdpCoinPair, CdpCoinPairStatus> CCdpDBCache::GetCdpCoinPairMap() {
    map<CCdpCoinPair, CdpCoinPairStatus> ret;
    for (auto item : kCDPCoinPairSet) {
        ret[CCdpCoinPair(item.first, item.second)] = CdpCoinPairStatus::NORMAL;
    }
    CDBIterator<decltype(cdpCoinPairsCache)> dbIt(cdpCoinPairsCache);
    for (dbIt.First(); dbIt.IsValid(); dbIt.Next()) {
        ret[dbIt.GetKey()] = (CdpCoinPairStatus)dbIt.GetValue();
    }
    return ret;
}

bool CCdpDBCache::SetCdpCoinPairStatus(const CCdpCoinPair &cdpCoinPair, const CdpCoinPairStatus &status) {
    return cdpCoinPairsCache.SetData(cdpCoinPair, (uint8_t)status);
}

void CCdpDBCache::SetBaseViewPtr(CCdpDBCache *pBaseIn) {
    cdpGlobalDataCache.SetBase(&pBaseIn->cdpGlobalDataCache);
    cdpCache.SetBase(&pBaseIn->cdpCache);
    userCdpCache.SetBase(&pBaseIn->userCdpCache);
    cdpCoinPairsCache.SetBase(&pBaseIn->cdpCoinPairsCache);

    cdpRatioSortedCache.SetBase(&pBaseIn->cdpRatioSortedCache);
}

void CCdpDBCache::SetDbOpLogMap(CDBOpLogMap *pDbOpLogMapIn) {
    cdpGlobalDataCache.SetDbOpLogMap(pDbOpLogMapIn);
    cdpCache.SetDbOpLogMap(pDbOpLogMapIn);
    userCdpCache.SetDbOpLogMap(pDbOpLogMapIn);
    cdpCoinPairsCache.SetDbOpLogMap(pDbOpLogMapIn);
    cdpRatioSortedCache.SetDbOpLogMap(pDbOpLogMapIn);
}

uint32_t CCdpDBCache::GetCacheSize() const {
    return cdpGlobalDataCache.GetCacheSize() + cdpCache.GetCacheSize() + userCdpCache.GetCacheSize() +
            cdpCoinPairsCache.GetCacheSize() + cdpRatioSortedCache.GetCacheSize();
}

bool CCdpDBCache::Flush() {
    cdpGlobalDataCache.Flush();
    cdpCache.Flush();
    userCdpCache.Flush();
    cdpCoinPairsCache.Flush();
    cdpRatioSortedCache.Flush();

    return true;
}

CdpRatioSortedCache::KeyType CCdpDBCache::MakeCdpRatioSortedKey(const CUserCDP &cdp) {

    CCdpCoinPair cdpCoinPair = cdp.GetCoinPair();
    uint64_t boostedRatio = cdp.collateral_ratio_base * CDP_BASE_RATIO_BOOST;
    uint64_t ratio        = (boostedRatio < cdp.collateral_ratio_base /* overflown */) ? UINT64_MAX : boostedRatio;
    CdpRatioSortedCache::KeyType key(cdpCoinPair, CFixedUInt64(ratio), CFixedUInt64(cdp.block_height),
         cdp.cdpid);
    return key;
}

string GetCdpCloseTypeName(const CDPCloseType type) {
    switch (type) {
        case CDPCloseType:: BY_REDEEM:
            return "redeem" ;
        case CDPCloseType:: BY_FORCE_LIQUIDATE :
            return "force_liquidate" ;
        case CDPCloseType ::BY_MANUAL_LIQUIDATE:
            return "manual_liquidate" ;
    }
    return "undefined" ;
}
