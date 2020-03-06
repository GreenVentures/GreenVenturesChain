// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2017-2019 The GreenVenturesChain Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TX_COIN_REWARD_H
#define TX_COIN_REWARD_H

#include "entities/asset.h"
#include "tx.h"

class CCoinRewardTx : public CBaseTx {
public:
    TokenSymbol coin_symbol;  // default: GVC
    uint64_t coin_amount;

public:
    CCoinRewardTx() : CBaseTx(UCOIN_REWARD_TX), coin_symbol(SYMB::GVC), coin_amount(0) {}

    CCoinRewardTx(const CUserID &txUidIn, const int32_t validHeightIn,
                  const TokenSymbol &coinSymbol, const uint64_t coinAmount)
        : CBaseTx(UCOIN_REWARD_TX, txUidIn, validHeightIn, 0),
          coin_symbol(coinSymbol),
          coin_amount(coinAmount) {}

    ~CCoinRewardTx() {}

    IMPLEMENT_SERIALIZE(
        READWRITE(VARINT(this->nVersion));
        nVersion = this->nVersion;
        READWRITE(txUid);
        READWRITE(VARINT(valid_height));

        READWRITE(coin_symbol);
        READWRITE(VARINT(coin_amount));

        READWRITE(signature);
    )

    virtual void SerializeForHash(CHashWriter &hw) const {
        hw << VARINT(nVersion) << uint8_t(nTxType) << txUid << VARINT(valid_height) << coin_symbol
           << VARINT(coin_amount);
    }

    std::shared_ptr<CBaseTx> GetNewInstance() const { return std::make_shared<CCoinRewardTx>(*this); }

    virtual string ToString(CAccountDBCache &accountCache);
    virtual Object ToJson(const CAccountDBCache &accountCache) const;

    bool GetInvolvedKeyIds(CCacheWrapper &cw, set<CKeyID> &keyIds) { return true; }

    virtual bool CheckTx(CTxExecuteContext &context);
    virtual bool ExecuteTx(CTxExecuteContext &context);
};

#endif  // TX_COIN_REWARD_H
