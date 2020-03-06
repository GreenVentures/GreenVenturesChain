// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2017-2019 The GreenVenturesChain Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PERSIST_TXDB_H
#define PERSIST_TXDB_H

#include "entities/account.h"
#include "entities/id.h"
#include "commons/serialize.h"
#include "commons/json/json_spirit_value.h"
#include "dbaccess.h"
#include "dbconf.h"
#include "block.h"

#include <map>
#include <vector>

using namespace std;
using namespace json_spirit;

class CTxMemCache {
public:
    CTxMemCache() : pBase(nullptr) {}
    CTxMemCache(CTxMemCache *pBaseIn) : pBase(pBaseIn) {}

public:
    bool HasTx(const uint256 &txid);

    bool AddBlockTx(const CBlock &block);
    bool RemoveBlockTx(const CBlock &block);

    void Clear();
    void SetBaseViewPtr(CTxMemCache *pBaseIn) { pBase = pBaseIn; }
    void Flush();

    Object ToJsonObj() const;
    uint64_t GetSize();

private:
    bool HaveBlock(const uint256 &blockHash) const;
    bool HaveBlock(const CBlock &block);
    void BatchWrite(const UnorderedHashSet &mapBlockTxHashSetIn);

private:
    UnorderedHashSet txids;
    CTxMemCache *pBase;
};

#endif // PERSIST_TXDB_H
