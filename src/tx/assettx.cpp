// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2017-2019 The GreenVenturesChain Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "assettx.h"

#include "config/const.h"
#include "main.h"
#include "entities/receipt.h"
#include "persistence/assetdb.h"

static const string ASSET_ACTION_ISSUE = "issue";
static const string ASSET_ACTION_UPDATE = "update";

Object AssetToJson(const CAccountDBCache &accountCache, const CUserIssuedAsset &asset) {
    Object result;
    CKeyID ownerKeyid;
    accountCache.GetKeyId(asset.owner_uid, ownerKeyid);
    result.push_back(Pair("asset_symbol",   asset.asset_symbol));
    result.push_back(Pair("asset_name",     asset.asset_name));
    result.push_back(Pair("owner_uid",      asset.owner_uid.ToString()));
    result.push_back(Pair("owner_addr",     ownerKeyid.ToAddress()));
    result.push_back(Pair("total_supply",   asset.total_supply));
    result.push_back(Pair("mintable",       asset.mintable));
    return result;
}

Object AssetToJson(const CAccountDBCache &accountCache, const CAsset &asset){
    //TODO
    return Object();
}
static bool ProcessAssetFee(CCacheWrapper &cw, CValidationState &state, const string &action,
    CAccount &txAccount, vector<CReceipt> &receipts,uint32_t currHeight) {

    uint64_t assetFee = 0;
    if (action == ASSET_ACTION_ISSUE) {
        if (!cw.sysParamCache.GetParam(ASSET_ISSUE_FEE, assetFee))
            return state.DoS(100, ERRORMSG("ProcessAssetFee, read param ASSET_ACTION_ISSUE error"),
                            REJECT_INVALID, "read-sysparam-error");
    } else {
        assert(action == ASSET_ACTION_UPDATE);
        if (!cw.sysParamCache.GetParam(ASSET_UPDATE_FEE, assetFee))
            return state.DoS(100, ERRORMSG("ProcessAssetFee, read param ASSET_UPDATE_FEE error"),
                            REJECT_INVALID, "read-sysparam-error");
    }

    if (!txAccount.OperateBalance(SYMB::GVC, BalanceOpType::SUB_FREE, assetFee))
        return state.DoS(100, ERRORMSG("ProcessAssetFee, insufficient funds in account for %s asset fee=%llu, tx_regid=%s",
                        action, assetFee, txAccount.regid.ToString()), UPDATE_ACCOUNT_FAIL, "insufficent-funds");

    uint64_t assetRiskFeeRatio ;
    if(!cw.sysParamCache.GetParam(SysParamType::ASSET_RISK_FEE_RATIO, assetRiskFeeRatio)) {
        return state.DoS(100, ERRORMSG("ProcessAssetFee, get assetRiskFeeRatio error",
                                       action, assetFee, txAccount.regid.ToString()), READ_SYS_PARAM_FAIL, "read-db-error");
    }

    uint64_t riskFee       = assetFee * assetRiskFeeRatio / RATIO_BOOST;
    uint64_t minerTotalFee = assetFee - riskFee;

    CAccount fcoinGenesisAccount;
    if (!cw.accountCache.GetFcoinGenesisAccount(fcoinGenesisAccount))
        return state.DoS(100, ERRORMSG("ProcessAssetFee, get risk reserve account failed"),
                        READ_ACCOUNT_FAIL, "get-account-failed");

    if (!fcoinGenesisAccount.OperateBalance(SYMB::GVC, BalanceOpType::ADD_FREE, riskFee)) {
        return state.DoS(100, ERRORMSG("ProcessAssetFee, operate balance failed! add %s asset fee=%llu to risk reserve account error",
            action, riskFee), UPDATE_ACCOUNT_FAIL, "update-account-failed");
    }
    if (action == ASSET_ACTION_ISSUE)
        receipts.emplace_back(txAccount.regid, fcoinGenesisAccount.regid, SYMB::GVC, riskFee, ReceiptCode::ASSET_ISSUED_FEE_TO_RESERVE);
    else
        receipts.emplace_back(txAccount.regid, fcoinGenesisAccount.regid, SYMB::GVC, riskFee, ReceiptCode::ASSET_UPDATED_FEE_TO_RESERVE);

    if (!cw.accountCache.SetAccount(fcoinGenesisAccount.keyid, fcoinGenesisAccount))
        return state.DoS(100, ERRORMSG("ProcessAssetFee, write fcoin genesis account info error, regid=%s",
            fcoinGenesisAccount.regid.ToString()), UPDATE_ACCOUNT_FAIL, "bad-read-accountdb");

    VoteDelegateVector delegates;
    if (!cw.delegateCache.GetActiveDelegates(delegates)) {
        return state.DoS(100, ERRORMSG("ProcessAssetFee, GetActiveDelegates failed"),
            REJECT_INVALID, "get-delegates-failed");
    }
    assert(delegates.size() != 0 );

    for (size_t i = 0; i < delegates.size(); i++) {
        const CRegID &delegateRegid = delegates[i].regid;
        CAccount delegateAccount;
        if (!cw.accountCache.GetAccount(CUserID(delegateRegid), delegateAccount)) {
            return state.DoS(100, ERRORMSG("ProcessAssetFee, get delegate account info failed! delegate regid=%s",
                delegateRegid.ToString()), UPDATE_ACCOUNT_FAIL, "bad-read-accountdb");
        }
        uint64_t minerUpdatedFee = minerTotalFee / delegates.size();
        if (i == 0) minerUpdatedFee += minerTotalFee % delegates.size(); // give the dust amount to topmost miner

        if (!delegateAccount.OperateBalance(SYMB::GVC, BalanceOpType::ADD_FREE, minerUpdatedFee)) {
            return state.DoS(100, ERRORMSG("ProcessAssetFee, add %s asset fee to miner failed, miner regid=%s",
                action, delegateRegid.ToString()), UPDATE_ACCOUNT_FAIL, "operate-account-failed");
        }

        if (!cw.accountCache.SetAccount(delegateRegid, delegateAccount))
            return state.DoS(100, ERRORMSG("ProcessAssetFee, write delegate account info error, delegate regid=%s",
                delegateRegid.ToString()), UPDATE_ACCOUNT_FAIL, "bad-read-accountdb");

        if (action == ASSET_ACTION_ISSUE)
            receipts.emplace_back(txAccount.regid, delegateRegid, SYMB::GVC, minerUpdatedFee, ReceiptCode::ASSET_ISSUED_FEE_TO_MINER);
        else
            receipts.emplace_back(txAccount.regid, delegateRegid, SYMB::GVC, minerUpdatedFee, ReceiptCode::ASSET_UPDATED_FEE_TO_MINER);
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////
// class CAssetIssueTx

bool CAssetIssueTx::CheckTx(CTxExecuteContext &context) {
    IMPLEMENT_DEFINE_CW_STATE;
    IMPLEMENT_DISABLE_TX_PRE_STABLE_COIN_RELEASE;
    IMPLEMENT_CHECK_TX_REGID(txUid);
    if (!CheckFee(context)) return false;

    auto symbolErr = CUserIssuedAsset::CheckSymbol(asset.asset_symbol);
    if (symbolErr) {
        return state.DoS(100, ERRORMSG("CAssetIssueTx::CheckTx, invlid asset symbol! %s", *symbolErr),
                        REJECT_INVALID, "invalid-asset-symbol");
    }

    if (asset.asset_name.empty() || asset.asset_name.size() > MAX_ASSET_NAME_LEN) {
        return state.DoS(100, ERRORMSG("CAssetIssueTx::CheckTx, asset_name is empty or len=%d greater than %d",
                        asset.asset_name.size(), MAX_ASSET_NAME_LEN), REJECT_INVALID, "invalid-asset-name");
    }

    if (asset.total_supply == 0 || asset.total_supply > MAX_ASSET_TOTAL_SUPPLY) {
        return state.DoS(100, ERRORMSG("CAssetIssueTx::CheckTx, asset total_supply=%llu can not == 0 or > %llu",
            asset.total_supply, MAX_ASSET_TOTAL_SUPPLY), REJECT_INVALID, "invalid-total-supply");
    }

    if (!asset.owner_uid.is<CRegID>()) {
        return state.DoS(100, ERRORMSG("%s, asset owner_uid must be regid", __FUNCTION__), REJECT_INVALID,
            "owner-uid-type-error");
    }

    if ((txUid.is<CPubKey>()) && !txUid.get<CPubKey>().IsFullyValid())
        return state.DoS(100, ERRORMSG("CAssetIssueTx::CheckTx, public key is invalid"), REJECT_INVALID,
                         "bad-publickey");

    CAccount txAccount;
    if (!cw.accountCache.GetAccount(txUid, txAccount))
        return state.DoS(100, ERRORMSG("CAssetIssueTx::CheckTx, read account failed! tx account not exist, txUid=%s",
                     txUid.ToDebugString()), REJECT_INVALID, "bad-getaccount");

    if (!txAccount.IsRegistered() || !txUid.get<CRegID>().IsMature(context.height))
        return state.DoS(100, ERRORMSG("CAssetIssueTx::CheckTx, account unregistered or immature"),
                         REJECT_INVALID, "account-unregistered-or-immature");

    IMPLEMENT_CHECK_TX_SIGNATURE(txAccount.owner_pubkey);

    return true;
}

bool CAssetIssueTx::ExecuteTx(CTxExecuteContext &context) {
    CCacheWrapper &cw = *context.pCw; CValidationState &state = *context.pState;
    vector<CReceipt> receipts;
    shared_ptr<CAccount> pTxAccount = make_shared<CAccount>();
    if (pTxAccount == nullptr || !cw.accountCache.GetAccount(txUid, *pTxAccount))
        return state.DoS(100, ERRORMSG("CAssetIssueTx::ExecuteTx, read source txUid %s account info error",
            txUid.ToDebugString()), UPDATE_ACCOUNT_FAIL, "bad-read-accountdb");

    if (!pTxAccount->OperateBalance(fee_symbol, BalanceOpType::SUB_FREE, llFees)) {
        return state.DoS(100, ERRORMSG("CAssetIssueTx::ExecuteTx, insufficient funds in account to sub fees, fees=%llu, txUid=%s",
                        llFees, txUid.ToDebugString()), UPDATE_ACCOUNT_FAIL, "insufficent-funds");
    }

    if (cw.assetCache.HasAsset(asset.asset_symbol))
        return state.DoS(100, ERRORMSG("CAssetUpdateTx::ExecuteTx, the asset has been issued! symbol=%s",
            asset.asset_symbol), REJECT_INVALID, "asset-existed-error");

    shared_ptr<CAccount> pOwnerAccount;
    if (pTxAccount->IsMyUid(asset.owner_uid)) {
        pOwnerAccount = pTxAccount;
    } else {
        pOwnerAccount = make_shared<CAccount>();
        if (pOwnerAccount == nullptr || !cw.accountCache.GetAccount(asset.owner_uid, *pOwnerAccount))
            return state.DoS(100, ERRORMSG("CAssetIssueTx::CheckTx, read account failed! asset owner "
                "account not exist, owner_uid=%s", asset.owner_uid.ToDebugString()), REJECT_INVALID, "bad-getaccount");
    }

    if (pOwnerAccount->regid.IsEmpty() || !pOwnerAccount->regid.IsMature(context.height)) {
        return state.DoS(100, ERRORMSG("CAssetIssueTx::CheckTx, owner regid=%s account is unregistered or immature",
            asset.owner_uid.get<CRegID>().ToString()), REJECT_INVALID, "owner-account-unregistered-or-immature");
    }

    if (!ProcessAssetFee(cw, state, ASSET_ACTION_ISSUE, *pTxAccount, receipts,context.height)) {
        return false;
    }

    if (!pOwnerAccount->OperateBalance(asset.asset_symbol, BalanceOpType::ADD_FREE, asset.total_supply)) {
        return state.DoS(100, ERRORMSG("CAssetIssueTx::ExecuteTx, fail to add total_supply to issued account! total_supply=%llu, txUid=%s",
                        asset.total_supply, txUid.ToDebugString()), UPDATE_ACCOUNT_FAIL, "insufficent-funds");
    }

    if (!cw.accountCache.SetAccount(txUid, *pTxAccount))
        return state.DoS(100, ERRORMSG("CAssetIssueTx::ExecuteTx, set tx account to db failed! txUid=%s",
            txUid.ToDebugString()), UPDATE_ACCOUNT_FAIL, "bad-set-accountdb");

    if (pOwnerAccount != pTxAccount) {
         if (!cw.accountCache.SetAccount(pOwnerAccount->keyid, *pOwnerAccount))
            return state.DoS(100, ERRORMSG("CAssetIssueTx::ExecuteTx, set asset owner account to db failed! owner_uid=%s",
                asset.owner_uid.ToDebugString()), UPDATE_ACCOUNT_FAIL, "bad-set-accountdb");
    }

    //Persist with Owner's RegID to save space than other User ID types
    CAsset savedAsset(asset.asset_symbol, asset.asset_name, AssetType::UIA, AssetPermType::DEX_BASE,
                    CUserID(pOwnerAccount->regid), asset.total_supply, asset.mintable);

    if (!cw.assetCache.SetAsset(savedAsset))
        return state.DoS(100, ERRORMSG("CAssetIssueTx::ExecuteTx, save asset failed! txUid=%s",
            txUid.ToDebugString()), UPDATE_ACCOUNT_FAIL, "save-asset-failed");

    if(!cw.txReceiptCache.SetTxReceipts(GetHash(), receipts))
        return state.DoS(100, ERRORMSG("CAssetIssueTx::ExecuteTx, set tx receipts failed!! txid=%s",
                        GetHash().ToString()), REJECT_INVALID, "set-tx-receipt-failed");
    return true;
}

string CAssetIssueTx::ToString(CAccountDBCache &accountCache) {
    return strprintf("txType=%s, hash=%s, ver=%d, txUid=%s, llFees=%ld, valid_height=%d, "
        "owner_uid=%s, asset_symbol=%s, asset_name=%s, total_supply=%llu, mintable=%d",
        GetTxType(nTxType), GetHash().ToString(), nVersion, txUid.ToDebugString(), llFees, valid_height,
        asset.owner_uid.ToDebugString(), asset.asset_symbol, asset.asset_name, asset.total_supply, asset.mintable);
}

Object CAssetIssueTx::ToJson(const CAccountDBCache &accountCache) const {
    Object result = CBaseTx::ToJson(accountCache);
    container::Append(result, AssetToJson(accountCache, asset));
    return result;
}

///////////////////////////////////////////////////////////////////////////////
// class CAssetUpdateData

static const EnumTypeMap<CAssetUpdateData::UpdateType, string> ASSET_UPDATE_TYPE_NAMES = {
    {CAssetUpdateData::OWNER_UID,   "owner_uid"},
    {CAssetUpdateData::NAME,        "name"},
    {CAssetUpdateData::MINT_AMOUNT, "mint_amount"}
};

static const unordered_map<string, CAssetUpdateData::UpdateType> ASSET_UPDATE_PARSE_MAP = {
    {"owner_addr",  CAssetUpdateData::OWNER_UID},
    {"name",        CAssetUpdateData::NAME},
    {"mint_amount", CAssetUpdateData::MINT_AMOUNT}
};

shared_ptr<CAssetUpdateData::UpdateType> CAssetUpdateData::ParseUpdateType(const string& str) {
    if (!str.empty()) {
        auto it = ASSET_UPDATE_PARSE_MAP.find(str);
        if (it != ASSET_UPDATE_PARSE_MAP.end()) {
            return make_shared<UpdateType>(it->second);
        }
    }
    return nullptr;
}

const string& CAssetUpdateData::GetUpdateTypeName(UpdateType type) {
    auto it = ASSET_UPDATE_TYPE_NAMES.find(type);
    if (it != ASSET_UPDATE_TYPE_NAMES.end()) return it->second;
    return EMPTY_STRING;
}
void CAssetUpdateData::Set(const CUserID &ownerUid) {
    type = OWNER_UID;
    value = ownerUid;
}

void CAssetUpdateData::Set(const string &name) {
    type = NAME;
    value = name;

}
void CAssetUpdateData::Set(const uint64_t &mintAmount) {
    type = MINT_AMOUNT;
    value = mintAmount;

}

string CAssetUpdateData::ValueToString() const {
    string s;
    switch (type) {
        case OWNER_UID:     s += get<CUserID>().ToString(); break;
        case NAME:          s += get<string>(); break;
        case MINT_AMOUNT:   s += std::to_string(get<uint64_t>()); break;
        default: break;
    }
    return s;
}

string CAssetUpdateData::ToString(const CAccountDBCache &accountCache) const {
    string s = "update_type=" + GetUpdateTypeName(type);
    s += ", update_value=" + ValueToString();
    return s;
}

Object CAssetUpdateData::ToJson(const CAccountDBCache &accountCache) const {
    Object result;
    result.push_back(Pair("update_type",   GetUpdateTypeName(type)));
    result.push_back(Pair("update_value",  ValueToString()));
    if (type == OWNER_UID) {
        CKeyID ownerKeyid;
        accountCache.GetKeyId(get<CUserID>(), ownerKeyid);
        result.push_back(Pair("owner_addr",   ownerKeyid.ToAddress()));
    }
    return result;
}

///////////////////////////////////////////////////////////////////////////////
// class CAssetUpdateTx

string CAssetUpdateTx::ToString(CAccountDBCache &accountCache) {
    return strprintf(
        "txType=%s, hash=%s, ver=%d, txUid=%s, fee_symbol=%s, llFees=%ld, valid_height=%d, asset_symbol=%s, "
        "update_data=%s",
        GetTxType(nTxType), GetHash().ToString(), nVersion, fee_symbol, txUid.ToDebugString(), llFees, valid_height,
        asset_symbol, update_data.ToString(accountCache));
}

Object CAssetUpdateTx::ToJson(const CAccountDBCache &accountCache) const {
    Object result = CBaseTx::ToJson(accountCache);

    result.push_back(Pair("asset_symbol",   asset_symbol));
    container::Append(result, update_data.ToJson(accountCache));

    return result;
}

bool CAssetUpdateTx::CheckTx(CTxExecuteContext &context) {
    IMPLEMENT_DEFINE_CW_STATE;
    IMPLEMENT_DISABLE_TX_PRE_STABLE_COIN_RELEASE;
    IMPLEMENT_CHECK_TX_REGID(txUid);
    if (!CheckFee(context)) return false;

    if (asset_symbol.empty() || asset_symbol.size() > MAX_TOKEN_SYMBOL_LEN) {
        return state.DoS(100, ERRORMSG("CAssetIssueTx::CheckTx, asset_symbol is empty or len=%d greater than %d",
            asset_symbol.size(), MAX_TOKEN_SYMBOL_LEN), REJECT_INVALID, "invalid-asset-symbol");
    }

    switch (update_data.GetType()) {
        case CAssetUpdateData::OWNER_UID: {
            const CUserID &newOwnerUid = update_data.get<CUserID>();
            if (!newOwnerUid.is<CRegID>()) {
                return state.DoS(100, ERRORMSG("%s, the new asset owner_uid must be regid", __FUNCTION__), REJECT_INVALID,
                    "owner-uid-type-error");
            }
            break;
        }
        case CAssetUpdateData::NAME: {
            const string &name = update_data.get<string>();
            if (name.empty() || name.size() > MAX_ASSET_NAME_LEN)
                return state.DoS(100, ERRORMSG("CAssetUpdateTx::CheckTx, asset name is empty or len=%d greater than %d",
                    name.size(), MAX_ASSET_NAME_LEN), REJECT_INVALID, "invalid-asset-name");
            break;
        }
        case CAssetUpdateData::MINT_AMOUNT: {
            uint64_t mintAmount = update_data.get<uint64_t>();
            if (mintAmount == 0 || mintAmount > MAX_ASSET_TOTAL_SUPPLY) {
                return state.DoS(100, ERRORMSG("CAssetUpdateTx::CheckTx, asset mint_amount=%llu is 0 or greater than %llu",
                    mintAmount, MAX_ASSET_TOTAL_SUPPLY), REJECT_INVALID, "invalid-mint-amount");
            }
            break;
        }
        default: {
            return state.DoS(100, ERRORMSG("CAssetUpdateTx::CheckTx, unsupported updated_type=%d",
                update_data.GetType()), REJECT_INVALID, "invalid-update-type");
        }
    }

    CAccount account;
    if (!cw.accountCache.GetAccount(txUid, account))
        return state.DoS(100, ERRORMSG("CAssetUpdateTx::CheckTx, read account failed"), REJECT_INVALID,
                         "bad-getaccount");
    if (!account.IsRegistered() || !txUid.get<CRegID>().IsMature(context.height))
        return state.DoS(100, ERRORMSG("CAssetUpdateTx::CheckTx, account unregistered or immature"),
                         REJECT_INVALID, "account-unregistered-or-immature");

    IMPLEMENT_CHECK_TX_SIGNATURE(account.owner_pubkey);

    return true;
}


bool CAssetUpdateTx::ExecuteTx(CTxExecuteContext &context) {
    CCacheWrapper &cw = *context.pCw; CValidationState &state = *context.pState;
    vector<CReceipt> receipts;
    CAccount account;
    if (!cw.accountCache.GetAccount(txUid, account))
        return state.DoS(100, ERRORMSG("CAssetUpdateTx::ExecuteTx, read source txUid %s account info error",
            txUid.ToDebugString()), READ_ACCOUNT_FAIL, "bad-read-accountdb");

    CAsset asset;
    if (!cw.assetCache.GetAsset(asset_symbol, asset))
        return state.DoS(100, ERRORMSG("CAssetUpdateTx::ExecuteTx, get asset by symbol=%s failed",
            asset_symbol), REJECT_INVALID, "get-asset-failed");

    if (!account.IsMyUid(asset.owner_uid))
        return state.DoS(100, ERRORMSG("CAssetUpdateTx::ExecuteTx, no privilege to update asset, uid dismatch,"
            " txUid=%s, old_asset_uid=%s",
            txUid.ToDebugString(), asset.owner_uid.ToString()), REJECT_INVALID, "asset-uid-dismatch");

    if (!asset.mintable)
        return state.DoS(100, ERRORMSG("CAssetUpdateTx::ExecuteTx, the asset is not mintable"),
                    REJECT_INVALID, "asset-not-mintable");


    switch (update_data.GetType()) {
        case CAssetUpdateData::OWNER_UID: {
            const CUserID &newOwnerUid = update_data.get<CUserID>();
            if (account.IsMyUid(newOwnerUid))
                return state.DoS(100, ERRORMSG("CAssetUpdateTx::ExecuteTx, the new owner uid=%s is belong to old owner account",
                    newOwnerUid.ToDebugString()), REJECT_INVALID, "invalid-new-asset-owner-uid");

            CAccount newAccount;
            if (!cw.accountCache.GetAccount(newOwnerUid, newAccount))
                return state.DoS(100, ERRORMSG("CAssetUpdateTx::ExecuteTx, the new owner uid=%s does not exist.",
                    newOwnerUid.ToDebugString()), READ_ACCOUNT_FAIL, "bad-read-accountdb");
            if (!newAccount.IsRegistered())
                return state.DoS(100, ERRORMSG("CAssetUpdateTx::ExecuteTx, the new owner account is not registered! new uid=%s",
                    newOwnerUid.ToDebugString()), REJECT_INVALID, "account-not-registered");
            if (!newAccount.regid.IsMature(context.height))
                return state.DoS(100, ERRORMSG("CAssetUpdateTx::ExecuteTx, the new owner regid is not matured! new uid=%s",
                    newOwnerUid.ToDebugString()), REJECT_INVALID, "account-not-matured");

            asset.owner_uid = newAccount.regid;
            break;
        }
        case CAssetUpdateData::NAME: {
            asset.asset_name = update_data.get<string>();
            break;
        }
        case CAssetUpdateData::MINT_AMOUNT: {
            uint64_t mintAmount = update_data.get<uint64_t>();
            uint64_t newTotalSupply = asset.total_supply + mintAmount;
            if (newTotalSupply > MAX_ASSET_TOTAL_SUPPLY || newTotalSupply < asset.total_supply) {
                return state.DoS(100, ERRORMSG("CAssetUpdateTx::ExecuteTx, the new mintAmount=%llu + total_supply=%s greater than %llu,",
                            mintAmount, asset.total_supply, MAX_ASSET_TOTAL_SUPPLY), REJECT_INVALID, "invalid-mint-amount");
            }

            if (!account.OperateBalance(asset_symbol, BalanceOpType::ADD_FREE, mintAmount)) {
                return state.DoS(100, ERRORMSG("CAssetUpdateTx::ExecuteTx, add mintAmount to asset owner account failed, txUid=%s, mintAmount=%llu",
                                txUid.ToDebugString(), mintAmount), UPDATE_ACCOUNT_FAIL, "account-add-free-failed");
            }
            asset.total_supply = newTotalSupply;
            break;
        }
        default: assert(false);
    }

    if (!account.OperateBalance(fee_symbol, BalanceOpType::SUB_FREE, llFees)) {
        return state.DoS(100, ERRORMSG("CAssetUpdateTx::ExecuteTx, insufficient funds in account, txUid=%s",
                        txUid.ToDebugString()), UPDATE_ACCOUNT_FAIL, "insufficent-funds");
    }

    if (!ProcessAssetFee(cw, state, ASSET_ACTION_UPDATE, account, receipts,context.height)) {
        return false;
    }

    if (!cw.assetCache.SetAsset(asset))
        return state.DoS(100, ERRORMSG("CAssetUpdateTx::ExecuteTx, save asset failed",
            txUid.ToDebugString()), UPDATE_ACCOUNT_FAIL, "save-asset-failed");

    if (!cw.accountCache.SetAccount(txUid, account))
        return state.DoS(100, ERRORMSG("CAssetUpdateTx::ExecuteTx, write txUid %s account info error",
            txUid.ToDebugString()), UPDATE_ACCOUNT_FAIL, "bad-read-accountdb");

    if(!cw.txReceiptCache.SetTxReceipts(GetHash(), receipts))
        return state.DoS(100, ERRORMSG("CAssetIssueTx::ExecuteTx, set tx receipts failed!! txid=%s",
                        GetHash().ToString()), REJECT_INVALID, "set-tx-receipt-failed");
    return true;
}
