// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2017-2019 The GreenVenturesChain Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "assetdb.h"

#include "commons/uint256.h"
#include "commons/util/util.h"

#include <stdint.h>

using namespace std;

bool CAssetDBCache::GetAsset(const TokenSymbol &tokenSymbol, CAsset &asset) {
    return assetCache.GetData(tokenSymbol, asset);
}

bool CAssetDBCache::SetAsset(const CAsset &asset) {
    return assetCache.SetData(asset.asset_symbol, asset);
}

bool CAssetDBCache::HasAsset(const TokenSymbol &tokenSymbol) {
    return assetCache.HasData(tokenSymbol);
}

shared_ptr<string> CAssetDBCache::CheckAssetSymbol(const TokenSymbol &symbol) {
    if (symbol.size() == 0 || symbol.size() > MAX_TOKEN_SYMBOL_LEN) {
        if (kCoinTypeSet.count(symbol))
            return nullptr;

        CAsset asset;
        if (GetAsset(symbol, asset))
            return nullptr;
    }

    return make_shared<string>("unsupported symbol");
}

bool CAssetDBCache::Flush() {
    assetCache.Flush();
    return true;
}
