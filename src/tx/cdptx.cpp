// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2017-2019 The GreenVenturesChain Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "cdptx.h"

#include "config/const.h"
#include "main.h"
#include "persistence/cdpdb.h"

#include <cmath>

using namespace std;


#define ERROR_TITLE(msg) (std::string(__func__) + "(), " + msg)
#define TX_OBJ_ERR_TITLE(tx) ERROR_TITLE(tx.GetTxTypeName())

static bool ReadCdpParam(CBaseTx &tx, CTxExecuteContext &context, const CCdpCoinPair &cdpCoinPair,
    CdpParamType paramType, uint64_t &value) {
    if (!context.pCw->sysParamCache.GetCdpParam(cdpCoinPair, paramType, value)) {
        return context.pState->DoS(100, ERRORMSG("%s, read cdp param %s error! cdpCoinPair=%s",
            TX_OBJ_ERR_TITLE(tx), GetCdpParamName(paramType), cdpCoinPair.ToString()),
                    READ_SYS_PARAM_FAIL, "read-cdp-param-error");
    }
    return true;
}

namespace cdp_util {

    static string ToString(const CDPStakeAssetMap& assetMap) {
        string ret = "";
        for (auto item : assetMap) {
            ret = strprintf("{%s=%llu}", item.first, item.second.get());
            if (!ret.empty()) ret += ",";
        }
        return "{" + ret + "}";
    }

    static Object ToJson(const CDPStakeAssetMap& assetMap) {
        Object ret;
        for (auto item : assetMap) {
            ret.push_back(Pair(item.first, item.second.get()));
        }
        return ret;
    }
}

static uint64_t CalcCollateralRatio(uint64_t assetAmount, uint64_t scoinAmount, uint64_t price) {

    return scoinAmount == 0 ? UINT64_MAX :
        uint64_t(double(assetAmount) * price / PRICE_BOOST / scoinAmount * RATIO_BOOST);
}

/**
 *  Interest Ratio Formula: ( a / Log10(b + N) )
 *
 *  ==> ratio = a / Log10 (b+N)
 */
uint64_t ComputeCDPInterest(const uint64_t total_owed_scoins, const int32_t beginHeight, const uint32_t endHeight,
                            uint64_t A, uint64_t B) {

    int32_t blockInterval = endHeight - beginHeight;
    int32_t loanedDays    = std::max<int32_t>(1, ceil((double)blockInterval / ::GetDayBlockCount(endHeight)));

    uint64_t N                = total_owed_scoins;
    double annualInterestRate = 0.1 * A / log10(1.0 + B * N / (double)COIN);
    uint64_t interest         = (uint64_t)(((double)N / 365) * loanedDays * annualInterestRate);

    LogPrint(BCLog::CDP, "ComputeCDPInterest, beginHeight=%d, endHeight=%d, loanedDays=%d, A=%llu, B=%llu, N="
             "%llu, annualInterestRate=%f, interest=%llu\n",
             beginHeight, endHeight, loanedDays, A, B, N, annualInterestRate, interest);

    return interest;
}

/**
 *  Interest Ratio Formula: ( a / Log10(b + N) )
 *
 *  ==> ratio = a / Log10 (b+N)
 */
bool ComputeCDPInterest(CTxExecuteContext &context, const CCdpCoinPair& coinPair, uint64_t total_owed_scoins,
        int32_t beginHeight, int32_t endHeight, uint64_t &interestOut) {
    if (total_owed_scoins == 0 || beginHeight >= endHeight) {
        interestOut = 0;
        return true;
    }

    list<CCdpInterestParamChange> changes;
    if (!context.pCw->sysParamCache.GetCdpInterestParamChanges(coinPair, beginHeight, endHeight, changes)) {
        return context.pState->DoS(100, ERRORMSG("%s(), get cdp interest param changes error! coinPiar=%s",
                __func__, coinPair.ToString()), REJECT_INVALID, "get-cdp-interest-param-changes-error");
    }

    interestOut = 0;
    for (auto &change : changes) {
        interestOut += ComputeCDPInterest(total_owed_scoins, change.begin_height, change.end_height,
            change.param_a, change.param_b);
    }

    LogPrint(BCLog::CDP, "ComputeCDPInterest, beginHeight: %d, endHeight: %d, totalInterest: %llu\n",
             beginHeight, endHeight, interestOut);
    return true;
}

// CDP owner can redeem his or her CDP that are in liquidation list
bool CCDPStakeTx::CheckTx(CTxExecuteContext &context) {
    IMPLEMENT_DEFINE_CW_STATE;
    IMPLEMENT_DISABLE_TX_PRE_STABLE_COIN_RELEASE;
    IMPLEMENT_CHECK_TX_REGID_OR_PUBKEY(txUid);
    if (!CheckFee(context)) return false;

    if (assets_to_stake.size() != 1) {
        return state.DoS(100, ERRORMSG("CCDPStakeTx::CheckTx, only support to stake one asset!"),
                        REJECT_INVALID, "invalid-stake-asset");
    }

    const TokenSymbol &assetSymbol = assets_to_stake.begin()->first;
    if (!kCDPCoinPairSet.count(std::pair<TokenSymbol, TokenSymbol>(assetSymbol, scoin_symbol))) {
        return state.DoS(100, ERRORMSG("CCDPStakeTx::CheckTx, invalid bcoin-scoin CDPCoinPair!"),
                        REJECT_INVALID, "invalid-CDPCoinPair-symbol");
    }

    CAccount account;
    if (!cw.accountCache.GetAccount(txUid, account)) {
        return state.DoS(100, ERRORMSG("CCDPStakeTx::CheckTx, read txUid %s account info error",
                        txUid.ToString()), READ_ACCOUNT_FAIL, "bad-read-accountdb");
    }

    CPubKey pubKey = (txUid.is<CPubKey>() ? txUid.get<CPubKey>() : account.owner_pubkey);
    IMPLEMENT_CHECK_TX_SIGNATURE(pubKey);

    return true;
}

bool CCDPStakeTx::ExecuteTx(CTxExecuteContext &context) {
    CCacheWrapper &cw = *context.pCw; CValidationState &state = *context.pState;
    //0. check preconditions

    assert(assets_to_stake.size() == 1);
    const TokenSymbol &assetSymbol = assets_to_stake.begin()->first;
    uint64_t assetAmount = assets_to_stake.begin()->second.get();
    CCdpCoinPair cdpCoinPair(assetSymbol, scoin_symbol);

    const TokenSymbol &quoteSymbol = GetPriceQuoteByCdpScoin(scoin_symbol);
    if (quoteSymbol.empty()) {
        return state.DoS(100, ERRORMSG("%s(), get price quote by cdp scoin=%s failed!", __func__, scoin_symbol),
                        REJECT_INVALID, "get-price-quote-by-cdp-scoin-failed");
    }

    uint64_t globalCollateralRatioMin;
    if (!ReadCdpParam(*this, context, cdpCoinPair, CdpParamType::CDP_GLOBAL_COLLATERAL_RATIO_MIN, globalCollateralRatioMin))
        return false;

    // TODO: multi stable coin
    uint64_t bcoinMedianPrice = cw.priceFeedCache.GetMedianPrice(CoinPricePair(assetSymbol, quoteSymbol));
    if (bcoinMedianPrice == 0) {
        return state.DoS(100, ERRORMSG("CCDPStakeTx::ExecuteTx, failed to acquire bcoin median price! coinPricePair=%s:%s",
                assetSymbol, quoteSymbol), REJECT_INVALID, "acquire-asset-price-err");
    }

    CCdpGlobalData cdpGlobalData = cw.cdpCache.GetCdpGlobalData(cdpCoinPair);
    uint64_t globalCollateralRatio = cdpGlobalData.GetCollateralRatio(bcoinMedianPrice);
    if (globalCollateralRatio < globalCollateralRatioMin) {
        return state.DoS(100, ERRORMSG("CCDPStakeTx::ExecuteTx, GlobalCollateralFloorReached! ratio=%llu,"
                " min=%llu", globalCollateralRatio, globalCollateralRatioMin),
                REJECT_INVALID, "global-collateral-floor-reached");
    }

    uint64_t globalCollateralCeiling;
    if (!ReadCdpParam(*this, context, cdpCoinPair, CdpParamType::CDP_GLOBAL_COLLATERAL_CEILING_AMOUNT,
            globalCollateralCeiling)) {
        return false;
    }

    if (cdpGlobalData.CheckGlobalCollateralCeilingReached(assetAmount, globalCollateralCeiling)) {
        return state.DoS(100, ERRORMSG("CCDPStakeTx::ExecuteTx, GlobalCollateralCeilingReached!"),
                        REJECT_INVALID, "global-collateral-ceiling-reached");
    }

    LogPrint(BCLog::CDP,
             "CCDPStakeTx::ExecuteTx, globalCollateralRatioMin: %llu, bcoinMedianPrice: %llu, globalCollateralCeiling: %llu\n",
             globalCollateralRatioMin, bcoinMedianPrice, globalCollateralCeiling);

    CAccount account;
    if (!cw.accountCache.GetAccount(txUid, account))
        return state.DoS(100, ERRORMSG("CCDPStakeTx::ExecuteTx, read txUid %s account info error",
                        txUid.ToString()), PRICE_FEED_FAIL, "bad-read-accountdb");

    if (!GenerateRegID(context, account)) {
        return false;
    }

    //1. pay miner fees (GVC)
    if (!account.OperateBalance(fee_symbol, BalanceOpType::SUB_FREE, llFees))
        return state.DoS(100, ERRORMSG("CCDPStakeTx::ExecuteTx, deduct fees from regId=%s failed,",
                        txUid.ToString()), UPDATE_ACCOUNT_FAIL, "deduct-account-fee-failed");

    //2. check collateral ratio: parital or total >= 200%
    uint64_t startingCdpCollateralRatio;
    if (!ReadCdpParam(*this, context, cdpCoinPair, CdpParamType::CDP_START_COLLATERAL_RATIO, startingCdpCollateralRatio))
        return state.DoS(100, ERRORMSG("CCDPStakeTx::ExecuteTx, read CDP_START_COLLATERAL_RATIO error!!"),
                        READ_SYS_PARAM_FAIL, "read-sysparamdb-error");

    vector<CReceipt> receipts;
    uint64_t mintScoinForInterest = 0;

    if (cdp_txid.IsEmpty()) { // 1st-time CDP creation
        if (assetAmount == 0 || scoins_to_mint == 0) {
            return state.DoS(100, ERRORMSG("CCDPStakeTx::ExecuteTx, invalid amount"), REJECT_INVALID, "invalid-amount");
        }

        vector<CUserCDP> userCdps;
        if (cw.cdpCache.UserHaveCdp(account.regid, assetSymbol, scoin_symbol)) {
            return state.DoS(100, ERRORMSG("CCDPStakeTx::ExecuteTx, the user (regid=%s) has existing CDP (txid=%s)!"
                            "asset_symbol=%s, scoin_symbol=%s",
                             GetHash().GetHex(), account.regid.ToString(), assetSymbol, scoin_symbol),
                             REJECT_INVALID, "user-cdp-created");
        }

        uint64_t collateralRatio = CalcCollateralRatio(assetAmount, scoins_to_mint, bcoinMedianPrice);
        if (collateralRatio < startingCdpCollateralRatio)
            return state.DoS(100,
                             ERRORMSG("CCDPStakeTx::ExecuteTx, 1st-time CDP creation, collateral ratio (%.2f%%) is "
                                      "smaller than the minimal (%.2f%%), price: %llu",
                                      100.0 * collateralRatio / RATIO_BOOST,
                                      100.0 * startingCdpCollateralRatio / RATIO_BOOST, bcoinMedianPrice),
                             REJECT_INVALID, "CDP-collateral-ratio-toosmall");

        CUserCDP cdp(account.regid, GetHash(), context.height, assetSymbol, scoin_symbol, assetAmount, scoins_to_mint);

        if (!cw.cdpCache.NewCDP(context.height, cdp)) {
            return state.DoS(100, ERRORMSG("CCDPStakeTx::ExecuteTx, save new cdp to db failed"),
                            READ_SYS_PARAM_FAIL, "save-new-cdp-failed");
        }

        uint64_t bcoinsToStakeAmountMinInScoin;
        if (!ReadCdpParam(*this, context, cdpCoinPair, CdpParamType::CDP_BCOINSTOSTAKE_AMOUNT_MIN_IN_SCOIN,
                        bcoinsToStakeAmountMinInScoin)) {
            return false;
        }

        uint64_t bcoinsToStakeAmountMin = bcoinsToStakeAmountMinInScoin / (double(bcoinMedianPrice) / PRICE_BOOST);
        if (cdp.total_staked_bcoins < bcoinsToStakeAmountMin) {
            return state.DoS(100, ERRORMSG("CCDPStakeTx::ExecuteTx, total staked bcoins (%llu vs %llu) is too small, price: %llu",
                            cdp.total_staked_bcoins, bcoinsToStakeAmountMin, bcoinMedianPrice), REJECT_INVALID,
                            "total-staked-bcoins-too-small");
        }
    } else { // further staking on one's existing CDP
        CUserCDP cdp;
        if (!cw.cdpCache.GetCDP(cdp_txid, cdp))
            return state.DoS(100, ERRORMSG("CCDPStakeTx::ExecuteTx, the cdp not exist! cdp_txid=%s", cdp_txid.ToString()),
                             REJECT_INVALID, "cdp-not-exist");

        if (assetSymbol != cdp.bcoin_symbol)
            return state.DoS(100, ERRORMSG("CCDPStakeTx::ExecuteTx, the asset symbol=%s does not match with the current CDP's=%s",
                            assetSymbol, cdp.bcoin_symbol), REJECT_INVALID, "invalid-asset-symbol");

        if (account.regid != cdp.owner_regid)
            return state.DoS(100, ERRORMSG("CCDPStakeTx::ExecuteTx, permission denied! cdp_txid=%s, owner(%s) vs operator(%s)",
                            cdp_txid.ToString(), cdp.owner_regid.ToString(), txUid.ToString()), REJECT_INVALID, "permission-denied");

        CUserCDP oldCDP = cdp; // copy before modify.

        if (context.height < cdp.block_height) {
            return state.DoS(100, ERRORMSG("CCDPStakeTx::ExecuteTx, height: %d < cdp.block_height: %d",
                            context.height, cdp.block_height), UPDATE_ACCOUNT_FAIL, "height-error");
        }

        uint64_t scoinsInterestToRepay = 0;
        if (!ComputeCDPInterest(context, cdpCoinPair, cdp.total_owed_scoins, cdp.block_height, context.height,
                                scoinsInterestToRepay)) {
            return false;
        }

        uint64_t ownerScoins = account.GetToken(scoin_symbol).free_amount;
        if (scoinsInterestToRepay > ownerScoins) {
            mintScoinForInterest = scoinsInterestToRepay - ownerScoins;
            LogPrint(BCLog::CDP, "Mint scoins=%llu for interest!\n", mintScoinForInterest);
        }

        uint64_t newMintScoins          = scoins_to_mint + mintScoinForInterest;
        uint64_t totalBcoinsToStake     = cdp.total_staked_bcoins + assetAmount;
        uint64_t totalScoinsToOwe       = cdp.total_owed_scoins + newMintScoins;
        uint64_t partialCollateralRatio = CalcCollateralRatio(assetAmount, newMintScoins, bcoinMedianPrice);
        uint64_t totalCollateralRatio   = CalcCollateralRatio(totalBcoinsToStake, totalScoinsToOwe, bcoinMedianPrice);

        if (partialCollateralRatio < startingCdpCollateralRatio && totalCollateralRatio < startingCdpCollateralRatio) {
            return state.DoS(100,
                             ERRORMSG("CCDPStakeTx::ExecuteTx, further staking CDP, collateral ratio (partial=%.2f%%, "
                                      "total=%.2f%%) is smaller than the minimal, price: %llu",
                                      100.0 * partialCollateralRatio / RATIO_BOOST,
                                      100.0 * totalCollateralRatio / RATIO_BOOST, bcoinMedianPrice),
                             REJECT_INVALID, "CDP-collateral-ratio-toosmall");
        }

        if (!SellInterestForFcoins(CTxCord(context.height, context.index), cdp, scoinsInterestToRepay, cw, state, receipts))
            return false;

        if (!account.OperateBalance(scoin_symbol, BalanceOpType::SUB_FREE, ownerScoins)) {
            return state.DoS(100, ERRORMSG("CCDPStakeTx::ExecuteTx, scoins balance < scoinsInterestToRepay: %llu",
                            scoinsInterestToRepay), UPDATE_ACCOUNT_FAIL,
                            strprintf("deduct-interest(%llu)-error", scoinsInterestToRepay));
        }

        // settle cdp state & persist
        cdp.AddStake(context.height, assetAmount, scoins_to_mint);
        if (!cw.cdpCache.UpdateCDP(oldCDP, cdp)) {
            return state.DoS(100, ERRORMSG("CCDPStakeTx::ExecuteTx, save changed cdp to db failed"),
                            READ_SYS_PARAM_FAIL, "save-changed-cdp-failed");
        }
    }

    // update account accordingly
    if (!account.OperateBalance(assetSymbol, BalanceOpType::PLEDGE, assetAmount)) {
        return state.DoS(100, ERRORMSG("CCDPStakeTx::ExecuteTx, bcoins insufficient to pledge"), UPDATE_ACCOUNT_FAIL,
                         "bcoins-insufficient-error");
    }
    if (!account.OperateBalance(scoin_symbol, BalanceOpType::ADD_FREE, scoins_to_mint)) {
        return state.DoS(100, ERRORMSG("CCDPStakeTx::ExecuteTx, add scoins failed"), UPDATE_ACCOUNT_FAIL,
                         "add-scoins-error");
    }
    if (!cw.accountCache.SaveAccount(account)) {
        return state.DoS(100, ERRORMSG("CCDPStakeTx::ExecuteTx, update account %s failed",
                        txUid.ToString()), UPDATE_ACCOUNT_FAIL, "bad-save-account");
    }

    receipts.emplace_back(txUid, nullId, assetSymbol, assetAmount, ReceiptCode::CDP_STAKED_ASSET_FROM_OWNER);
    receipts.emplace_back(nullId, txUid, scoin_symbol, scoins_to_mint + mintScoinForInterest,
                        ReceiptCode::CDP_MINTED_SCOIN_TO_OWNER);

    if (!cw.txReceiptCache.SetTxReceipts(GetHash(), receipts))
        return state.DoS(100, ERRORMSG("CCDPStakeTx::ExecuteTx, set tx receipts failed!! txid=%s",
                        GetHash().ToString()), REJECT_INVALID, "set-tx-receipt-failed");

    return true;
}

string CCDPStakeTx::ToString(CAccountDBCache &accountCache) {
    CKeyID keyId;
    accountCache.GetKeyId(txUid, keyId);

    return strprintf(
        "txType=%s, hash=%s, ver=%d, txUid=%s, addr=%s, valid_height=%llu, cdp_txid=%s, assets_to_stake=%s, "
        "scoin_symbol=%s, scoins_to_mint=%d",
        GetTxType(nTxType), GetHash().ToString(), nVersion, txUid.ToString(), keyId.ToAddress(), valid_height,
        cdp_txid.ToString(), cdp_util::ToString(assets_to_stake), scoin_symbol, scoins_to_mint);
}

Object CCDPStakeTx::ToJson(const CAccountDBCache &accountCache) const {
    Object result = CBaseTx::ToJson(accountCache);
    TxID cdpId = cdp_txid;
    if (cdpId.IsEmpty()) { // this is new cdp tx
        cdpId = GetHash();
    }

    result.push_back(Pair("cdp_txid",           cdpId.ToString()));
    result.push_back(Pair("assets_to_stake",    cdp_util::ToJson(assets_to_stake)));
    result.push_back(Pair("scoin_symbol",       scoin_symbol));
    result.push_back(Pair("scoins_to_mint",     scoins_to_mint));

    return result;
}

bool CCDPStakeTx::SellInterestForFcoins(const CTxCord &txCord, const CUserCDP &cdp,
                                        const uint64_t scoinsInterestToRepay, CCacheWrapper &cw,
                                        CValidationState &state, vector<CReceipt> &receipts) {
    if (scoinsInterestToRepay == 0)
        return true;

    CAccount fcoinGenesisAccount;
    cw.accountCache.GetFcoinGenesisAccount(fcoinGenesisAccount);
    // send interest to fcoin genesis account
    if (!fcoinGenesisAccount.OperateBalance(SYMB::WUSD, BalanceOpType::ADD_FREE, scoinsInterestToRepay)) {
        return state.DoS(100, ERRORMSG("CCDPStakeTx::SellInterestForFcoins, operate balance failed"),
                        UPDATE_ACCOUNT_FAIL, "operate-fcoin-genesis-account-failed");
    }

    // should freeze user's coin for buying the asset
    if (!fcoinGenesisAccount.OperateBalance(SYMB::WUSD, BalanceOpType::FREEZE, scoinsInterestToRepay)) {
        return state.DoS(100, ERRORMSG("CCDPStakeTx::SellInterestForFcoins, account has insufficient funds"),
                        UPDATE_ACCOUNT_FAIL, "operate-fcoin-genesis-account-failed");
    }

    if (!cw.accountCache.SetAccount(fcoinGenesisAccount.keyid, fcoinGenesisAccount))
        return state.DoS(100, ERRORMSG("CCDPStakeTx::SellInterestForFcoins, set account info error"),
                        WRITE_ACCOUNT_FAIL, "bad-write-accountdb");

    auto pSysBuyMarketOrder = dex::CSysOrder::CreateBuyMarketOrder(txCord, cdp.scoin_symbol,
        SYMB::WGRT, scoinsInterestToRepay);
    if (!cw.dexCache.CreateActiveOrder(GetHash(), *pSysBuyMarketOrder)) {
        return state.DoS(100, ERRORMSG("CCDPStakeTx::SellInterestForFcoins, create system buy order failed"),
                        CREATE_SYS_ORDER_FAILED, "create-sys-order-failed");
    }

    assert(!fcoinGenesisAccount.regid.IsEmpty());
    receipts.emplace_back(txUid, fcoinGenesisAccount.regid, cdp.scoin_symbol, scoinsInterestToRepay,
                          ReceiptCode::CDP_INTEREST_BUY_DEFLATE_FCOINS);
    return true;
}

/************************************<< CCDPRedeemTx >>***********************************************/
bool CCDPRedeemTx::CheckTx(CTxExecuteContext &context) {
    IMPLEMENT_DEFINE_CW_STATE;
    IMPLEMENT_DISABLE_TX_PRE_STABLE_COIN_RELEASE;
    IMPLEMENT_CHECK_TX_REGID_OR_PUBKEY(txUid);
    if (!CheckFee(context)) return false;

    CAccount account;
    if (!cw.accountCache.GetAccount(txUid, account)) {
        return state.DoS(100, ERRORMSG("CCDPRedeemTx::CheckTx, read txUid %s account info error",
                        txUid.ToString()), READ_ACCOUNT_FAIL, "bad-read-accountdb");
    }

    if (cdp_txid.IsEmpty()) {
        return state.DoS(100, ERRORMSG("CCDPRedeemTx::CheckTx, cdp_txid is empty"),
                        REJECT_INVALID, "empty-cdpid");
    }

    CPubKey pubKey = (txUid.is<CPubKey>() ? txUid.get<CPubKey>() : account.owner_pubkey);
    IMPLEMENT_CHECK_TX_SIGNATURE(pubKey);

    return true;
}

bool CCDPRedeemTx::ExecuteTx(CTxExecuteContext &context) {
    CCacheWrapper &cw = *context.pCw; CValidationState &state = *context.pState;
    //0. check preconditions
    CAccount account;
    if (!cw.accountCache.GetAccount(txUid, account)) {
        return state.DoS(100, ERRORMSG("CCDPRedeemTx::ExecuteTx, read txUid %s account info error",
                        txUid.ToString()), READ_ACCOUNT_FAIL, "bad-read-accountdb");
    }

    if (!GenerateRegID(context, account)) {
        return false;
    }

    CUserCDP cdp;
    if (!cw.cdpCache.GetCDP(cdp_txid, cdp)) {
        return state.DoS(100, ERRORMSG("CCDPRedeemTx::ExecuteTx, cdp (%s) not exist", cdp_txid.ToString()),
                         REJECT_INVALID, "cdp-not-exist");
    }

    if (assets_to_redeem.size() != 1) {
        return state.DoS(100, ERRORMSG("CCDPStakeTx::CheckTx, only support to redeem one asset!"),
                        REJECT_INVALID, "invalid-stake-asset");
    }
    const TokenSymbol &assetSymbol = assets_to_redeem.begin()->first;
    uint64_t assetAmount = assets_to_redeem.begin()->second.get();
    if (assetSymbol != cdp.bcoin_symbol)
        return state.DoS(100, ERRORMSG("CCDPStakeTx::CheckTx, asset symbol to redeem is not match!"),
                        REJECT_INVALID, "invalid-stake-asset");

    if (account.regid != cdp.owner_regid) {
        return state.DoS(100, ERRORMSG("CCDPRedeemTx::ExecuteTx, permission denied! cdp_txid=%s, owner(%s) vs operator(%s)",
                        cdp_txid.ToString(), cdp.owner_regid.ToString(), txUid.ToString()), REJECT_INVALID, "permission-denied");
    }

    CCdpCoinPair cdpCoinPair(cdp.bcoin_symbol, cdp.scoin_symbol);
    CUserCDP oldCDP = cdp; // copy before modify.

    uint64_t globalCollateralRatioFloor;

    if (!ReadCdpParam(*this, context, cdpCoinPair, CDP_GLOBAL_COLLATERAL_RATIO_MIN, globalCollateralRatioFloor)) {
        return false;
    }

    uint64_t bcoinMedianPrice = cw.priceFeedCache.GetMedianPrice(CoinPricePair(cdp.bcoin_symbol, SYMB::USD));
    if (bcoinMedianPrice == 0)
        return state.DoS(100, ERRORMSG("CCDPRedeemTx::ExecuteTx, failed to acquire bcoin median price!!"),
                        REJECT_INVALID, "acquire-bcoin-median-price-err");

    CCdpGlobalData cdpGlobalData = cw.cdpCache.GetCdpGlobalData(cdpCoinPair);
    if (cdpGlobalData.CheckGlobalCollateralRatioFloorReached(bcoinMedianPrice, globalCollateralRatioFloor)) {
        return state.DoS(100, ERRORMSG("CCDPRedeemTx::ExecuteTx, GlobalCollateralFloorReached!!"), REJECT_INVALID,
                         "global-cdp-lock-is-on");
    }

    //1. pay miner fees (GVC)
    if (!account.OperateBalance(fee_symbol, SUB_FREE, llFees)) {
        return state.DoS(100, ERRORMSG("CCDPRedeemTx::ExecuteTx, deduct fees from regId=%s failed",
                        txUid.ToString()), UPDATE_ACCOUNT_FAIL, "deduct-account-fee-failed");
    }

    //2. pay interest fees in wusd
    if (context.height < cdp.block_height) {
        return state.DoS(100, ERRORMSG("CCDPRedeemTx::ExecuteTx, height: %d < cdp.block_height: %d",
                        context.height, cdp.block_height), UPDATE_ACCOUNT_FAIL, "height-error");
    }

    uint64_t scoinsInterestToRepay = 0;
    if (!ComputeCDPInterest(context, cdpCoinPair, cdp.total_owed_scoins, cdp.block_height, context.height,
            scoinsInterestToRepay)) {
        return false;
    }

    if (!account.OperateBalance(cdp.scoin_symbol, BalanceOpType::SUB_FREE, scoinsInterestToRepay)) {
        return state.DoS(100, ERRORMSG("CCDPRedeemTx::ExecuteTx, Deduct interest error!"),
                        REJECT_INVALID, "deduct-interest-error");
    }

    vector<CReceipt> receipts;
    if (!SellInterestForFcoins(CTxCord(context.height, context.index), cdp, scoinsInterestToRepay, cw, state, receipts)) {
        return state.DoS(100, ERRORMSG("CCDPRedeemTx::ExecuteTx, SellInterestForFcoins error!"),
                        REJECT_INVALID, "sell-interest-for-fcoins-error");
    }

    uint64_t startingCdpCollateralRatio;
    if (!ReadCdpParam(*this, context, cdpCoinPair, CDP_START_COLLATERAL_RATIO, startingCdpCollateralRatio))
        return false;

    //3. redeem in scoins and update cdp
    if (assetAmount > cdp.total_staked_bcoins) {
        LogPrint(BCLog::CDP, "CCDPRedeemTx::ExecuteTx, the redeemed bcoins=%llu is bigger than total_staked_bcoins=%llu, use the min one",
                assetAmount, cdp.total_staked_bcoins);

        assetAmount = cdp.total_staked_bcoins;
    }
    uint64_t actualScoinsToRepay = scoins_to_repay;
    if (actualScoinsToRepay > cdp.total_owed_scoins) {
        LogPrint(BCLog::CDP, "CCDPRedeemTx::ExecuteTx, the repay scoins=%llu is bigger than total_owed_scoins=%llu, use the min one",
                actualScoinsToRepay, cdp.total_staked_bcoins);

        actualScoinsToRepay = cdp.total_owed_scoins;
    }

    // check account balance vs scoins_to_repay
    if (account.GetToken(cdp.scoin_symbol).free_amount < scoins_to_repay) {
        return state.DoS(100, ERRORMSG("CCDPRedeemTx::ExecuteTx, account balance insufficient"), REJECT_INVALID,
                         "account-balance-insufficient");
    }

    cdp.Redeem(context.height, assetAmount, actualScoinsToRepay);

    // check and save CDP to db
    if (cdp.IsFinished()) {
        if (!cw.cdpCache.EraseCDP(oldCDP, cdp)) {
            return state.DoS(100, ERRORMSG("CCDPRedeemTx::ExecuteTx, erase the finished CDP %s failed",
                            cdp.cdpid.ToString()), UPDATE_CDP_FAIL, "erase-cdp-failed");

        } else {
            if (SysCfg().GetArg("-persistclosedcdp", false)) {
                if (!cw.closedCdpCache.AddClosedCdpIndex(oldCDP.cdpid, GetHash(), CDPCloseType::BY_REDEEM)) {
                    LogPrint(BCLog::ERROR, "persistclosedcdp AddClosedCdpIndex failed for redeemed cdpid (%s)", oldCDP.cdpid.GetHex());
                }

                if (!cw.closedCdpCache.AddClosedCdpTxIndex(GetHash(), oldCDP.cdpid, CDPCloseType::BY_REDEEM)) {
                    LogPrint(BCLog::ERROR, "persistclosedcdp AddClosedCdpTxIndex failed for redeemed cdpid (%s)", oldCDP.cdpid.GetHex());
                }
            }
        }
    } else { // partial redeem
        if (assetAmount != 0) {
            uint64_t collateralRatio  = cdp.GetCollateralRatio(bcoinMedianPrice);
            if (collateralRatio < startingCdpCollateralRatio) {
                return state.DoS(100,
                                 ERRORMSG("CCDPRedeemTx::ExecuteTx, the cdp collatera ratio=%.2f%% cannot < %.2f%% "
                                          "after redeem, price: %llu",
                                          100.0 * collateralRatio / RATIO_BOOST,
                                          100.0 * startingCdpCollateralRatio / RATIO_BOOST, bcoinMedianPrice),
                                 UPDATE_CDP_FAIL, "invalid-collatera-ratio");
            }

            uint64_t bcoinsToStakeAmountMinInScoin;

           if (!ReadCdpParam(*this, context, cdpCoinPair, CDP_BCOINSTOSTAKE_AMOUNT_MIN_IN_SCOIN,
                            bcoinsToStakeAmountMinInScoin))
                return false;

            uint64_t bcoinsToStakeAmountMin = bcoinsToStakeAmountMinInScoin / (double(bcoinMedianPrice) / PRICE_BOOST);
            if (cdp.total_staked_bcoins < bcoinsToStakeAmountMin) {
                return state.DoS(100, ERRORMSG("CCDPRedeemTx::ExecuteTx, total staked bcoins (%llu vs %llu) is too small",
                                cdp.total_staked_bcoins, bcoinsToStakeAmountMin), REJECT_INVALID, "total-staked-bcoins-too-small");
            }
        }

        if (!cw.cdpCache.UpdateCDP(oldCDP, cdp)) {
            return state.DoS(100, ERRORMSG("CCDPRedeemTx::ExecuteTx, update CDP %s failed", cdp.cdpid.ToString()),
                            UPDATE_CDP_FAIL, "bad-save-cdp");
        }
    }

    if (!account.OperateBalance(cdp.scoin_symbol, BalanceOpType::SUB_FREE, actualScoinsToRepay)) {
        return state.DoS(100, ERRORMSG("CCDPRedeemTx::ExecuteTx, update account(%s) SUB WUSD(%lu) failed",
                        account.regid.ToString(), actualScoinsToRepay), UPDATE_ACCOUNT_FAIL, "bad-operate-account");
    }
    if (!account.OperateBalance(cdp.bcoin_symbol, BalanceOpType::UNPLEDGE, assetAmount)) {
        return state.DoS(100, ERRORMSG("CCDPRedeemTx::ExecuteTx, update account(%s) ADD GVC(%lu) failed",
                        account.regid.ToString(), assetAmount), UPDATE_ACCOUNT_FAIL, "bad-operate-account");
    }
    if (!cw.accountCache.SaveAccount(account)) {
        return state.DoS(100, ERRORMSG("CCDPRedeemTx::ExecuteTx, update account %s failed",
                        txUid.ToString()), UPDATE_ACCOUNT_FAIL, "bad-save-account");
    }

    receipts.emplace_back(txUid, nullId, cdp.scoin_symbol, actualScoinsToRepay, ReceiptCode::CDP_REPAID_SCOIN_FROM_OWNER);
    receipts.emplace_back(nullId, txUid, cdp.bcoin_symbol, assetAmount, ReceiptCode::CDP_REDEEMED_ASSET_TO_OWNER);

    if (!cw.txReceiptCache.SetTxReceipts(GetHash(), receipts))
        return state.DoS(100, ERRORMSG("CCDPRedeemTx::ExecuteTx, set tx receipts failed!! txid=%s", GetHash().ToString()),
                         REJECT_INVALID, "set-tx-receipt-failed");

    return true;
}

string CCDPRedeemTx::ToString(CAccountDBCache &accountCache) {
    CKeyID keyId;
    accountCache.GetKeyId(txUid, keyId);

    return strprintf(
        "txType=%s, hash=%s, ver=%d, txUid=%s, addr=%s, valid_height=%llu, cdp_txid=%s, scoins_to_repay=%d, "
        "assets_to_redeem=%d",
        GetTxType(nTxType), GetHash().ToString(), nVersion, txUid.ToString(), keyId.ToAddress(), valid_height,
        cdp_txid.ToString(), scoins_to_repay, cdp_util::ToString(assets_to_redeem));
}

Object CCDPRedeemTx::ToJson(const CAccountDBCache &accountCache) const {
    Object result = CBaseTx::ToJson(accountCache);
    result.push_back(Pair("cdp_txid",           cdp_txid.ToString()));
    result.push_back(Pair("scoins_to_repay",    scoins_to_repay));
    result.push_back(Pair("assets_to_redeem",   cdp_util::ToJson(assets_to_redeem)));

    return result;
}

bool CCDPRedeemTx::SellInterestForFcoins(const CTxCord &txCord, const CUserCDP &cdp,
                                        const uint64_t scoinsInterestToRepay, CCacheWrapper &cw,
                                        CValidationState &state, vector<CReceipt> &receipts) {
    if (scoinsInterestToRepay == 0)
        return true;

    CAccount fcoinGenesisAccount;
    cw.accountCache.GetFcoinGenesisAccount(fcoinGenesisAccount);
    // send interest to fcoin genesis account
    if (!fcoinGenesisAccount.OperateBalance(SYMB::WUSD, BalanceOpType::ADD_FREE, scoinsInterestToRepay)) {
        return state.DoS(100, ERRORMSG("CCDPRedeemTx::SellInterestForFcoins, operate balance failed"),
                        UPDATE_ACCOUNT_FAIL, "operate-fcoin-genesis-account-failed");
    }

    // should freeze user's coin for buying the asset
    if (!fcoinGenesisAccount.OperateBalance(SYMB::WUSD, BalanceOpType::FREEZE, scoinsInterestToRepay)) {
        return state.DoS(100, ERRORMSG("CCDPRedeemTx::SellInterestForFcoins, account has insufficient funds"),
                        UPDATE_ACCOUNT_FAIL, "operate-fcoin-genesis-account-failed");
    }

    if (!cw.accountCache.SetAccount(fcoinGenesisAccount.keyid, fcoinGenesisAccount))
        return state.DoS(100, ERRORMSG("CCDPRedeemTx::SellInterestForFcoins, set account info error"),
                        WRITE_ACCOUNT_FAIL, "bad-write-accountdb");

    auto pSysBuyMarketOrder = dex::CSysOrder::CreateBuyMarketOrder(txCord, cdp.scoin_symbol,
                                                                SYMB::WGRT, scoinsInterestToRepay);

    if (!cw.dexCache.CreateActiveOrder(GetHash(), *pSysBuyMarketOrder)) {
        return state.DoS(100, ERRORMSG("CCDPRedeemTx::SellInterestForFcoins, create system buy order failed"),
                        CREATE_SYS_ORDER_FAILED, "create-sys-order-failed");
    }

    assert(!fcoinGenesisAccount.regid.IsEmpty());
    receipts.emplace_back(txUid, fcoinGenesisAccount.regid, cdp.scoin_symbol, scoinsInterestToRepay,
                          ReceiptCode::CDP_INTEREST_BUY_DEFLATE_FCOINS);

    return true;
}

 /************************************<< CdpLiquidateTx >>***********************************************/
 bool CCDPLiquidateTx::CheckTx(CTxExecuteContext &context) {
    IMPLEMENT_DEFINE_CW_STATE;
    IMPLEMENT_DISABLE_TX_PRE_STABLE_COIN_RELEASE;
    IMPLEMENT_CHECK_TX_REGID_OR_PUBKEY(txUid);
    if (!CheckFee(context)) return false;

    if (scoins_to_liquidate == 0) {
        return state.DoS(100, ERRORMSG("CCDPLiquidateTx::CheckTx, invalid liquidate amount(0)"), REJECT_INVALID,
                         "invalid-liquidate-amount");
    }

    if (cdp_txid.IsEmpty()) {
        return state.DoS(100, ERRORMSG("CCDPLiquidateTx::CheckTx, cdp_txid is empty"), REJECT_INVALID, "empty-cdpid");
    }

    CAccount account;
    if (!cw.accountCache.GetAccount(txUid, account))
        return state.DoS(100, ERRORMSG("CdpLiquidateTx::CheckTx, read txUid %s account info error", txUid.ToString()),
                         READ_ACCOUNT_FAIL, "bad-read-accountdb");

    CPubKey pubKey = (txUid.is<CPubKey>() ? txUid.get<CPubKey>() : account.owner_pubkey);
    IMPLEMENT_CHECK_TX_SIGNATURE(pubKey);

    return true;
}

/**
  * total_staked_bcoinsInScoins : total_owed_scoins = M : N
  *
  * Liquidator paid         1.13lN          (0 < l ≤ 100%)
  *   Liquidate Amount:     l * N       = lN
  *   Penalty Fees:         l * N * 13% = 0.13lN
  * Liquidator received:    Bcoins only
  *   Bcoins:               1.13lN ~ 1.16lN (GVC)
  *   Net Profit:           0 ~ 0.03lN (GVC)
  *
  * CDP Owner returned
  *   Bcoins:               lM - 1.16lN = l(M - 1.16N)
  *
  *  when M is 1.16 N and below, there'll be no return to the CDP owner
  *  when M is 1.13 N and below, there'll be no profit for the liquidator, hence requiring force settlement
  */
bool CCDPLiquidateTx::ExecuteTx(CTxExecuteContext &context) {
    CCacheWrapper &cw = *context.pCw; CValidationState &state = *context.pState;
    //0. check preconditions
    CAccount account;
    if (!cw.accountCache.GetAccount(txUid, account)) {
        return state.DoS(100, ERRORMSG("CCDPLiquidateTx::ExecuteTx, read txUid %s account info error",
                        txUid.ToString()), READ_ACCOUNT_FAIL, "bad-read-accountdb");
    }

    if (!GenerateRegID(context, account)) {
        return false;
    }

    CUserCDP cdp;
    if (!cw.cdpCache.GetCDP(cdp_txid, cdp)) {
        return state.DoS(100, ERRORMSG("CCDPLiquidateTx::ExecuteTx, cdp (%s) not exist!",
                        txUid.ToString()), REJECT_INVALID, "cdp-not-exist");
    }

    if (!liquidate_asset_symbol.empty() && liquidate_asset_symbol != cdp.bcoin_symbol)
        return state.DoS(100, ERRORMSG("CCDPStakeTx::ExecuteTx, the liquidate_asset_symbol=%s must be empty of match with the asset symbols of CDP",
            liquidate_asset_symbol), REJECT_INVALID, "invalid-asset-symbol");

    CCdpCoinPair cdpCoinPair(cdp.bcoin_symbol, cdp.scoin_symbol);
    CUserCDP oldCDP = cdp; // copy before modify.

    uint64_t free_scoins = account.GetToken(cdp.scoin_symbol).free_amount;
    if (free_scoins < scoins_to_liquidate) {  // more applicable when scoinPenalty is omitted
        return state.DoS(100, ERRORMSG("CdpLiquidateTx::ExecuteTx, account scoins %d < scoins_to_liquidate: %d", free_scoins,
                        scoins_to_liquidate), CDP_LIQUIDATE_FAIL, "account-scoins-insufficient");
    }

    uint64_t globalCollateralRatioFloor;
    if (!ReadCdpParam(*this, context, cdpCoinPair, CDP_GLOBAL_COLLATERAL_RATIO_MIN, globalCollateralRatioFloor))
        return false;


    uint64_t bcoinMedianPrice = cw.priceFeedCache.GetMedianPrice(CoinPricePair(cdp.bcoin_symbol, SYMB::USD));
    if (bcoinMedianPrice == 0) {
        return state.DoS(100, ERRORMSG("CCDPLiquidateTx::ExecuteTx, failed to acquire bcoin median price!!"),
                         REJECT_INVALID, "acquire-bcoin-median-price-err");
    }

    CCdpGlobalData cdpGlobalData = cw.cdpCache.GetCdpGlobalData(cdpCoinPair);
    if (cdpGlobalData.CheckGlobalCollateralRatioFloorReached(bcoinMedianPrice, globalCollateralRatioFloor)) {
        return state.DoS(100, ERRORMSG("CCDPLiquidateTx::ExecuteTx, GlobalCollateralFloorReached!!"), REJECT_INVALID,
                         "global-cdp-lock-is-on");
    }

    //1. pay miner fees (GVC)
    if (!account.OperateBalance(fee_symbol, SUB_FREE, llFees)) {
        return state.DoS(100, ERRORMSG("CCDPLiquidateTx::ExecuteTx, deduct fees from regId=%s failed",
                        txUid.ToString()), UPDATE_ACCOUNT_FAIL, "deduct-account-fee-failed");
    }

    //2. pay penalty fees: 0.13lN --> 50% burn, 50% to Risk Reserve
    CAccount cdpOwnerAccount;
    if (!cw.accountCache.GetAccount(CUserID(cdp.owner_regid), cdpOwnerAccount)) {
        return state.DoS(100, ERRORMSG("CCDPLiquidateTx::ExecuteTx, read CDP Owner txUid %s account info error",
                        txUid.ToString()), READ_ACCOUNT_FAIL, "bad-read-accountdb");
    }

    uint64_t startingCdpLiquidateRatio;
    if (!ReadCdpParam(*this, context, cdpCoinPair, CDP_START_LIQUIDATE_RATIO, startingCdpLiquidateRatio))
        return false;

    uint64_t nonReturnCdpLiquidateRatio;
    if (!ReadCdpParam(*this, context, cdpCoinPair, CDP_NONRETURN_LIQUIDATE_RATIO, nonReturnCdpLiquidateRatio))
        return false;

    uint64_t cdpLiquidateDiscountRate;
    if (!ReadCdpParam(*this, context, cdpCoinPair, CDP_LIQUIDATE_DISCOUNT_RATIO, cdpLiquidateDiscountRate))
        return false;

    uint64_t forcedCdpLiquidateRatio;
    if (!ReadCdpParam(*this, context, cdpCoinPair, CDP_FORCE_LIQUIDATE_RATIO, forcedCdpLiquidateRatio))
        return false;

    uint64_t totalBcoinsToReturnLiquidator = 0;
    uint64_t totalScoinsToLiquidate        = 0;
    uint64_t totalScoinsToReturnSysFund    = 0;
    uint64_t totalBcoinsToCdpOwner         = 0;

    uint64_t collateralRatio = cdp.GetCollateralRatio(bcoinMedianPrice);
    if (collateralRatio > startingCdpLiquidateRatio) {  // 1.5++
        return state.DoS(100,
                         ERRORMSG("CCDPLiquidateTx::ExecuteTx, cdp collateralRatio(%.2f%%) > %.2f%%, price: %llu",
                                  100.0 * collateralRatio / RATIO_BOOST,
                                  100.0 * startingCdpLiquidateRatio / RATIO_BOOST, bcoinMedianPrice),
                         REJECT_INVALID, "cdp-not-liquidate-ready");

    } else if (collateralRatio > nonReturnCdpLiquidateRatio) { // 1.13 ~ 1.5
        totalBcoinsToReturnLiquidator = cdp.total_owed_scoins * (double)nonReturnCdpLiquidateRatio / RATIO_BOOST /
                                        ((double)bcoinMedianPrice / PRICE_BOOST);  // 1.13N
        assert(cdp.total_staked_bcoins >= totalBcoinsToReturnLiquidator);

        totalBcoinsToCdpOwner = cdp.total_staked_bcoins - totalBcoinsToReturnLiquidator;

        totalScoinsToLiquidate = ( cdp.total_owed_scoins * (double)nonReturnCdpLiquidateRatio / RATIO_BOOST )
                                * cdpLiquidateDiscountRate / RATIO_BOOST; //1.096N

        totalScoinsToReturnSysFund = totalScoinsToLiquidate - cdp.total_owed_scoins;

    } else if (collateralRatio > forcedCdpLiquidateRatio) {    // 1.04 ~ 1.13
        totalBcoinsToReturnLiquidator = cdp.total_staked_bcoins; //M
        totalBcoinsToCdpOwner = 0;
        totalScoinsToLiquidate = totalBcoinsToReturnLiquidator * ((double) bcoinMedianPrice / PRICE_BOOST)
                                * cdpLiquidateDiscountRate / RATIO_BOOST; //M * 97%

        totalScoinsToReturnSysFund = totalScoinsToLiquidate - cdp.total_owed_scoins; // M * 97% - N

    } else {                                                    // 0 ~ 1.04
        // Although not likely to happen, but when it does, execute it accordingly.
        totalBcoinsToReturnLiquidator = cdp.total_staked_bcoins;
        totalBcoinsToCdpOwner         = 0;
        totalScoinsToLiquidate        = cdp.total_owed_scoins;  // N
        totalScoinsToReturnSysFund    = 0;
    }

    vector<CReceipt> receipts;

    if (scoins_to_liquidate >= totalScoinsToLiquidate) {
        if (!account.OperateBalance(cdp.scoin_symbol, SUB_FREE, totalScoinsToLiquidate)) {
            return state.DoS(100, ERRORMSG("CCDPLiquidateTx::ExecuteTx, deduct scoins from regId=%s failed",
                            txUid.ToString()), UPDATE_ACCOUNT_FAIL, "deduct-account-scoins-failed");
        }
        if (!account.OperateBalance(cdp.bcoin_symbol, ADD_FREE, totalBcoinsToReturnLiquidator)) {
            return state.DoS(100, ERRORMSG("CCDPLiquidateTx::ExecuteTx, add bcoins failed"), UPDATE_ACCOUNT_FAIL,
                             "add-bcoins-failed");
        }

        // clean up cdp owner's pledged_amount
        if (!cdpOwnerAccount.OperateBalance(cdp.bcoin_symbol, UNPLEDGE, totalBcoinsToReturnLiquidator)) {
            return state.DoS(100, ERRORMSG("CCDPLiquidateTx::ExecuteTx, unpledge bcoins failed"), UPDATE_ACCOUNT_FAIL,
                             "unpledge-bcoins-failed");
        }
        if (!cdpOwnerAccount.OperateBalance(cdp.bcoin_symbol, SUB_FREE, totalBcoinsToReturnLiquidator)) {
            return state.DoS(100, ERRORMSG("CCDPLiquidateTx::ExecuteTx, sub unpledged bcoins failed"), UPDATE_ACCOUNT_FAIL,
                             "deduct-bcoins-failed");
        }

        if (account.regid != cdpOwnerAccount.regid) { //liquidate by others
            if (!cdpOwnerAccount.OperateBalance(cdp.bcoin_symbol, UNPLEDGE, totalBcoinsToCdpOwner)) {
                return state.DoS(100, ERRORMSG("CCDPLiquidateTx::ExecuteTx, unpledge bcoins failed"), UPDATE_ACCOUNT_FAIL,
                                 "unpledge-bcoins-failed");
            }
            if (!cw.accountCache.SetAccount(CUserID(cdp.owner_regid), cdpOwnerAccount))
                return state.DoS(100, ERRORMSG("CCDPLiquidateTx::ExecuteTx, write cdp owner account info error! owner_regid=%s",
                                cdp.owner_regid.ToString()), UPDATE_ACCOUNT_FAIL, "bad-write-accountdb");
        } else {  // liquidate by oneself
            if (!account.OperateBalance(cdp.bcoin_symbol, UNPLEDGE, totalBcoinsToCdpOwner)) {
                return state.DoS(100, ERRORMSG("CCDPLiquidateTx::ExecuteTx, unpledge bcoins failed"), UPDATE_ACCOUNT_FAIL,
                                 "unpledge-bcoins-failed");
            }
        }

        if (!ProcessPenaltyFees(context, cdp, (uint64_t)totalScoinsToReturnSysFund, receipts))
            return false;

        // close CDP
        if (!cw.cdpCache.EraseCDP(oldCDP, cdp)) {
            return state.DoS(100, ERRORMSG("CCDPLiquidateTx::ExecuteTx, erase CDP failed! cdpid=%s",
                        cdp.cdpid.ToString()), UPDATE_CDP_FAIL, "erase-cdp-failed");

        } else if (SysCfg().GetArg("-persistclosedcdp", false)) {
            if (!cw.closedCdpCache.AddClosedCdpIndex(oldCDP.cdpid, GetHash(), CDPCloseType::BY_MANUAL_LIQUIDATE)) {
                LogPrint(BCLog::ERROR, "persistclosedcdp AddClosedCdpIndex failed for redeemed cdpid (%s)", oldCDP.cdpid.GetHex());
            }

            if (!cw.closedCdpCache.AddClosedCdpTxIndex(GetHash(), oldCDP.cdpid, CDPCloseType::BY_MANUAL_LIQUIDATE)) {
                LogPrint(BCLog::ERROR, "persistclosedcdp AddClosedCdpTxIndex failed for redeemed cdpid (%s)", oldCDP.cdpid.GetHex());
            }
        }

        receipts.emplace_back(txUid, nullId, cdp.scoin_symbol, totalScoinsToLiquidate,
                              ReceiptCode::CDP_SCOIN_FROM_LIQUIDATOR);
        receipts.emplace_back(nullId, txUid, cdp.bcoin_symbol, totalBcoinsToReturnLiquidator,
                              ReceiptCode::CDP_ASSET_TO_LIQUIDATOR);
        receipts.emplace_back(nullId, cdp.owner_regid, cdp.bcoin_symbol, (uint64_t)totalBcoinsToCdpOwner,
                              ReceiptCode::CDP_LIQUIDATED_ASSET_TO_OWNER);
        receipts.emplace_back(nullId, nullId, cdp.scoin_symbol, cdp.total_owed_scoins,
                              ReceiptCode::CDP_LIQUIDATED_CLOSEOUT_SCOIN);

    } else {    // partial liquidation
        double liquidateRate = (double)scoins_to_liquidate / totalScoinsToLiquidate;  // unboosted on purpose
        assert(liquidateRate < 1);
        totalBcoinsToReturnLiquidator *= liquidateRate;

        if (!account.OperateBalance(cdp.scoin_symbol, SUB_FREE, scoins_to_liquidate)) {
            return state.DoS(100, ERRORMSG("CCDPLiquidateTx::ExecuteTx, sub scoins to liquidator failed"),
                             UPDATE_ACCOUNT_FAIL, "sub-scoins-to-liquidator-failed");
        }
        if (!account.OperateBalance(cdp.bcoin_symbol, ADD_FREE, totalBcoinsToReturnLiquidator)) {
            return state.DoS(100, ERRORMSG("CCDPLiquidateTx::ExecuteTx, add bcoins to liquidator failed"),
                             UPDATE_ACCOUNT_FAIL, "add-bcoins-to-liquidator-failed");
        }

        // clean up cdp owner's pledged_amount
        if (!cdpOwnerAccount.OperateBalance(cdp.bcoin_symbol, UNPLEDGE, totalBcoinsToReturnLiquidator)) {
            return state.DoS(100, ERRORMSG("CCDPLiquidateTx::ExecuteTx, unpledge bcoins failed"), UPDATE_ACCOUNT_FAIL,
                             "unpledge-bcoins-failed");
        }
        if (!cdpOwnerAccount.OperateBalance(cdp.bcoin_symbol, SUB_FREE, totalBcoinsToReturnLiquidator)) {
            return state.DoS(100, ERRORMSG("CCDPLiquidateTx::ExecuteTx, sub unpledged bcoins failed"), UPDATE_ACCOUNT_FAIL,
                             "deduct-bcoins-failed");
        }

        uint64_t bcoinsToCDPOwner = totalBcoinsToCdpOwner * liquidateRate;
        if (account.regid != cdpOwnerAccount.regid) {
            if (!cdpOwnerAccount.OperateBalance(cdp.bcoin_symbol, UNPLEDGE, bcoinsToCDPOwner)) {
                return state.DoS(100, ERRORMSG("CCDPLiquidateTx::ExecuteTx, unpledge bcoins to cdp owner failed"),
                                 UPDATE_ACCOUNT_FAIL, "unpledge-bcoins-to-cdp-owner-failed");
            }
            if (!cw.accountCache.SetAccount(CUserID(cdp.owner_regid), cdpOwnerAccount))
                return state.DoS(100, ERRORMSG("CCDPLiquidateTx::ExecuteTx, write cdp owner account info error! owner_regid=%s",
                                cdp.owner_regid.ToString()), UPDATE_ACCOUNT_FAIL, "bad-write-accountdb");
        } else {  // liquidate by oneself
            if (!account.OperateBalance(cdp.bcoin_symbol, UNPLEDGE, bcoinsToCDPOwner)) {
                return state.DoS(100, ERRORMSG("CCDPLiquidateTx::ExecuteTx, unpledge bcoins to cdp owner failed"),
                                 UPDATE_ACCOUNT_FAIL, "unpledge-bcoins-to-cdp-owner-failed");
            }
        }

        uint64_t scoinsToCloseout = cdp.total_owed_scoins * liquidateRate;
        uint64_t bcoinsToLiquidate = totalBcoinsToReturnLiquidator + bcoinsToCDPOwner;

        assert(cdp.total_owed_scoins > scoinsToCloseout);
        assert(cdp.total_staked_bcoins > bcoinsToLiquidate);
        cdp.PartialLiquidate(context.height, bcoinsToLiquidate, scoinsToCloseout);

        uint64_t bcoinsToStakeAmountMinInScoin;
        if (!ReadCdpParam(*this, context, cdpCoinPair, CDP_BCOINSTOSTAKE_AMOUNT_MIN_IN_SCOIN,
                        bcoinsToStakeAmountMinInScoin))
            return false;

        uint64_t bcoinsToStakeAmountMin = bcoinsToStakeAmountMinInScoin / (double(bcoinMedianPrice) / PRICE_BOOST);
        if (cdp.total_staked_bcoins < bcoinsToStakeAmountMin) {
            return state.DoS(100, ERRORMSG("CCDPLiquidateTx::ExecuteTx, total staked bcoins (%llu vs %llu) is too small, "
                            "txid=%s, cdp=%s, height=%d, price=%llu", cdp.total_staked_bcoins, bcoinsToStakeAmountMin,
                            GetHash().GetHex(), cdp.ToString(), context.height, bcoinMedianPrice),
                            REJECT_INVALID, "total-staked-bcoins-too-small");
        }

        CCdpCoinPair cdpCoinPair(cdp.bcoin_symbol, cdp.scoin_symbol);
        uint64_t scoinsToReturnSysFund = scoins_to_liquidate -  scoinsToCloseout;
        if (!ProcessPenaltyFees(context, cdp, scoinsToReturnSysFund, receipts))
            return false;

        if (!cw.cdpCache.UpdateCDP(oldCDP, cdp)) {
            return state.DoS(100, ERRORMSG("CCDPLiquidateTx::ExecuteTx, update CDP failed! cdpid=%s",
                        cdp.cdpid.ToString()), UPDATE_CDP_FAIL, "bad-save-cdp");
        }

        receipts.emplace_back(txUid, nullId, cdp.scoin_symbol, scoins_to_liquidate,
                              ReceiptCode::CDP_SCOIN_FROM_LIQUIDATOR);
        receipts.emplace_back(nullId, txUid, cdp.bcoin_symbol, totalBcoinsToReturnLiquidator,
                              ReceiptCode::CDP_ASSET_TO_LIQUIDATOR);
        receipts.emplace_back(nullId, cdp.owner_regid, cdp.bcoin_symbol, bcoinsToCDPOwner,
                              ReceiptCode::CDP_LIQUIDATED_ASSET_TO_OWNER);
        receipts.emplace_back(nullId, nullId, cdp.scoin_symbol, scoinsToCloseout,
                              ReceiptCode::CDP_LIQUIDATED_CLOSEOUT_SCOIN);
    }

    if (!cw.accountCache.SetAccount(txUid, account))
        return state.DoS(100, ERRORMSG("CCDPLiquidateTx::ExecuteTx, write txUid %s account info error",
            txUid.ToString()), UPDATE_ACCOUNT_FAIL, "bad-write-accountdb");

    if (!cw.txReceiptCache.SetTxReceipts(GetHash(), receipts))
        return state.DoS(100, ERRORMSG("CCDPLiquidateTx::ExecuteTx, write tx receipt failed! txid=%s",
            GetHash().ToString()), REJECT_INVALID, "write-tx-receipt-failed");

    return true;
}

string CCDPLiquidateTx::ToString(CAccountDBCache &accountCache) {
    CKeyID keyId;
    accountCache.GetKeyId(txUid, keyId);

    return strprintf(
        "txType=%s, hash=%s, ver=%d, txUid=%s, addr=%s, valid_height=%llu, cdp_txid=%s, liquidate_asset_symbol=%s, "
        "scoins_to_liquidate=%d",
        GetTxType(nTxType), GetHash().ToString(), nVersion, txUid.ToString(), keyId.ToAddress(), valid_height,
        cdp_txid.ToString(), liquidate_asset_symbol, scoins_to_liquidate);
}

Object CCDPLiquidateTx::ToJson(const CAccountDBCache &accountCache) const {
    Object result = CBaseTx::ToJson(accountCache);
    result.push_back(Pair("cdp_txid",               cdp_txid.ToString()));
    result.push_back(Pair("liquidate_asset_symbol", liquidate_asset_symbol));
    result.push_back(Pair("scoins_to_liquidate",    scoins_to_liquidate));

    return result;
}

bool CCDPLiquidateTx::ProcessPenaltyFees(CTxExecuteContext &context, const CUserCDP &cdp, uint64_t scoinPenaltyFees,
                                        vector<CReceipt> &receipts) {

    CCacheWrapper &cw = *context.pCw; CValidationState &state = *context.pState;
    CTxCord txCord = CTxCord(context.height, context.index);

    if (scoinPenaltyFees == 0)
        return true;

    CAccount fcoinGenesisAccount;
    if (!cw.accountCache.GetFcoinGenesisAccount(fcoinGenesisAccount)) {
        return state.DoS(100, ERRORMSG("CCDPStakeTx::ProcessPenaltyFees, read fcoinGenesisUid %s account info error"),
                        READ_ACCOUNT_FAIL, "bad-read-accountdb");
    }

    CCdpCoinPair cdpCoinPair(cdp.bcoin_symbol, cdp.scoin_symbol);
    uint64_t minSysOrderPenaltyFee;
    if (!ReadCdpParam(*this, context, cdpCoinPair, CDP_SYSORDER_PENALTY_FEE_MIN, minSysOrderPenaltyFee))
        return false;

    if (scoinPenaltyFees > minSysOrderPenaltyFee ) { //10+ WUSD
        uint64_t halfScoinsPenalty = scoinPenaltyFees / 2;
        uint64_t leftScoinPenalty  = scoinPenaltyFees - halfScoinsPenalty;  // handle odd amount

        // 1) save 50% penalty fees into risk reserve
        if (!fcoinGenesisAccount.OperateBalance(cdp.scoin_symbol, BalanceOpType::ADD_FREE, halfScoinsPenalty)) {
            return state.DoS(100, ERRORMSG("CCDPLiquidateTx::ExecuteTx, add scoins to fcoin genesis account failed"),
                             UPDATE_ACCOUNT_FAIL, "add-scoins-to-fcoin-genesis-account-failed");
        }

        // 2) sell 50% penalty fees for Fcoins and burn
        // send half scoin penalty to fcoin genesis account
        if (!fcoinGenesisAccount.OperateBalance(cdp.scoin_symbol, BalanceOpType::ADD_FREE, leftScoinPenalty)) {
            return state.DoS(100, ERRORMSG("CCDPLiquidateTx::ExecuteTx, add scoins to fcoin genesis account failed"),
                             UPDATE_ACCOUNT_FAIL, "add-scoins-to-fcoin-genesis-account-failed");
        }

        // should freeze user's coin for buying the asset
        if (!fcoinGenesisAccount.OperateBalance(cdp.scoin_symbol, BalanceOpType::FREEZE, leftScoinPenalty)) {
            return state.DoS(100, ERRORMSG("CdpLiquidateTx::ProcessPenaltyFees, account has insufficient funds"),
                            UPDATE_ACCOUNT_FAIL, "operate-fcoin-genesis-account-failed");
        }

        auto pSysBuyMarketOrder = dex::CSysOrder::CreateBuyMarketOrder(txCord, cdp.scoin_symbol, SYMB::WGRT, leftScoinPenalty);
        if (!cw.dexCache.CreateActiveOrder(GetHash(), *pSysBuyMarketOrder)) {
            return state.DoS(100, ERRORMSG("CdpLiquidateTx::ProcessPenaltyFees, create system buy order failed"),
                            CREATE_SYS_ORDER_FAILED, "create-sys-order-failed");
        }

        CUserID fcoinGenesisUid(fcoinGenesisAccount.regid);
        receipts.emplace_back(nullId, fcoinGenesisUid, cdp.scoin_symbol, halfScoinsPenalty,
                              ReceiptCode::CDP_PENALTY_TO_RESERVE);
        receipts.emplace_back(nullId, fcoinGenesisUid, cdp.scoin_symbol, leftScoinPenalty,
                              ReceiptCode::CDP_PENALTY_BUY_DEFLATE_FCOINS);
    } else {
        // send penalty fees into risk reserve
        if (!fcoinGenesisAccount.OperateBalance(cdp.scoin_symbol, BalanceOpType::ADD_FREE, scoinPenaltyFees)) {
            return state.DoS(100, ERRORMSG("CCDPLiquidateTx::ExecuteTx, add scoins to fcoin genesis account failed"),
                             UPDATE_ACCOUNT_FAIL, "add-scoins-to-fcoin-genesis-account-failed");
        }
        receipts.emplace_back(nullId, fcoinGenesisAccount.regid, cdp.scoin_symbol, scoinPenaltyFees,
                              ReceiptCode::CDP_PENALTY_TO_RESERVE);
    }

    if (!cw.accountCache.SetAccount(fcoinGenesisAccount.keyid, fcoinGenesisAccount))
        return state.DoS(100, ERRORMSG("CCDPLiquidateTx::ProcessPenaltyFees, write fcoin genesis account info error!"),
                UPDATE_ACCOUNT_FAIL, "bad-write-accountdb");

    return true;
}
