// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2017-2019 The GreenVenturesChain Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "commons/base58.h"
#include "config/const.h"
#include "rpc/core/rpccommons.h"
#include "rpc/rpcapi.h"
#include "init.h"
#include "net.h"
#include "commons/util/util.h"
#include "wallet/wallet.h"
#include "wallet/walletdb.h"
#include "tx/dextx.h"
#include "tx/dexoperatortx.h"

using namespace dex;

static Object DexOperatorToJson(const CAccountDBCache &accountCache, const DexOperatorDetail &dexOperator) {
    Object result;
    CKeyID ownerKeyid;
    accountCache.GetKeyId(dexOperator.owner_regid, ownerKeyid);
    CKeyID feeReceiverKeyId;
    accountCache.GetKeyId(dexOperator.fee_receiver_regid, feeReceiverKeyId);
    result.push_back(Pair("owner_regid",   dexOperator.owner_regid.ToString()));
    result.push_back(Pair("owner_addr",     ownerKeyid.ToAddress()));
    result.push_back(Pair("fee_receiver_regid", dexOperator.fee_receiver_regid.ToString()));
    result.push_back(Pair("fee_receiver_addr",   feeReceiverKeyId.ToAddress()));
    result.push_back(Pair("name",           dexOperator.name));
    result.push_back(Pair("portal_url",     dexOperator.portal_url));
    result.push_back(Pair("maker_fee_ratio", dexOperator.maker_fee_ratio));
    result.push_back(Pair("taker_fee_ratio", dexOperator.taker_fee_ratio));
    result.push_back(Pair("activated",       dexOperator.activated));
    result.push_back(Pair("memo",           dexOperator.memo));
    result.push_back(Pair("memo_hex",       HexStr(dexOperator.memo)));
    return result;
}

namespace RPC_PARAM {

    OrderType GetOrderType(const Value &jsonValue) {
        OrderType ret = OrderType::ORDER_TYPE_NULL;
        if (kOrderTypeHelper.Parse(jsonValue.get_str(), ret))
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("order_type=%s is invalid",
                jsonValue.get_str()));
        return ret;
    }

    OrderSide GetOrderSide(const Value &jsonValue) {
        OrderSide ret = OrderSide::ORDER_SIDE_NULL;
        if (kOrderSideHelper.Parse(jsonValue.get_str(), ret))
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("order_side=%s is invalid",
                jsonValue.get_str()));
        return ret;
    }

    DexID GetDexId(const Value &jsonValue) {
        return RPC_PARAM::GetUint32(jsonValue);
    }

    DexID GetDexId(const Array& params, const size_t index) {
        return params.size() > index? GetDexId(params[index]) : DEX_RESERVED_ID;
    }

    uint64_t GetOperatorFeeRatio(const Value &jsonValue) {

        uint64_t ratio = RPC_PARAM::GetUint64(jsonValue);
        if (ratio > DEX_OPERATOR_FEE_RATIO_MAX)
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("match fee ratio=%llu is large than %llu",
                ratio, DEX_OPERATOR_FEE_RATIO_MAX));
        return ratio;
    }


    uint64_t GetOperatorFeeRatio(const Array& params, const size_t index) {
        return params.size() > index ? GetOperatorFeeRatio(params[index]) : 0;
    }

    string GetMemo(const Array& params, const size_t index) {
        if (params.size() > index) {
            string memo = params[index].get_str();
            if (memo.size() > MAX_COMMON_TX_MEMO_SIZE)
                throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("memo.size=%u is large than %llu",
                    memo.size(), MAX_COMMON_TX_MEMO_SIZE));
            return memo;
        }
        return "";
    }

    PublicMode GetOrderPublicMode(const Value &jsonValue) {
        PublicMode ret = PublicMode::PRIVATE;
        if (!kPublicModeHelper.Parse(jsonValue.get_str(), ret))
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("order_public_mode=%s is invalid",
                jsonValue.get_str()));
        return ret;
    }

    PublicMode GetOrderPublicMode(const Array &params, const size_t index) {

        return params.size() > index ? GetOrderPublicMode(params[index].get_str()) : PublicMode::PUBLIC;
    }

    DexOperatorDetail GetDexOperator(const DexID &dexId) {
        DexOperatorDetail operatorDetail;
        if (!pCdMan->pDexCache->GetDexOperator(dexId, operatorDetail))
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("the dex operator does not exist! dex_id=%u",
                dexId));
        return operatorDetail;
    }

    void CheckOrderAmount(const TokenSymbol &symbol, const uint64_t amount, const char *pSymbolSide) {

        if (amount < (int64_t)MIN_DEX_ORDER_AMOUNT)
            throw JSONRPCError(RPC_INVALID_PARAMETER,
                strprintf("%s amount is too small, symbol=%s, amount=%llu, min_amount=%llu",
                          pSymbolSide, symbol, amount, MIN_DEX_ORDER_AMOUNT));

        CheckTokenAmount(symbol, amount);
    }

    uint64_t CalcCoinAmount(uint64_t assetAmount, const uint64_t price) {
        uint128_t coinAmount = assetAmount * (uint128_t)price / PRICE_BOOST;
        if (coinAmount > ULLONG_MAX)
            throw JSONRPCError(RPC_INVALID_PARAMETER,
                strprintf("the calculated coin amount out of range, asset_amount=%llu, price=%llu",
                          assetAmount, price));
        return (uint64_t)coinAmount;
    }

} // namespace RPC_PARAM

Object SubmitOrderTx(const CKeyID &txKeyid, const DexOperatorDetail &operatorDetail,
    shared_ptr<CDEXOrderBaseTx> &pOrderBaseTx) {

    if (!pWalletMain->HasKey(txKeyid)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "tx user address not found in wallet");
    }
    const uint256 txHash = pOrderBaseTx->GetHash();
    if (!pWalletMain->Sign(txKeyid, txHash, pOrderBaseTx->signature)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Sign failed");
    }

    shared_ptr<CBaseTx> pBaseTx = static_pointer_cast<CBaseTx>(pOrderBaseTx);
    if (pOrderBaseTx->has_operator_config) {
        CAccount operatorAccount = RPC_PARAM::GetUserAccount(*pCdMan->pAccountCache, operatorDetail.fee_receiver_regid);
        const CKeyID &operatorKeyid = operatorAccount.keyid;
        if (!pWalletMain->HasKey(operatorKeyid)) {
            CDataStream ds(SER_DISK, CLIENT_VERSION);
            ds << pBaseTx;
            throw JSONRPCError(RPC_WALLET_ERROR, strprintf("dex operator address not found in wallet! "
                "tx_raw_with_sign=%s", HexStr(ds.begin(), ds.end())));
        }
        if (!pWalletMain->Sign(operatorKeyid, txHash, pOrderBaseTx->operator_signature)) {
            throw JSONRPCError(RPC_WALLET_ERROR, "Sign failed");
        }
    }

    std::tuple<bool, string> ret = pWalletMain->CommitTx(pBaseTx.get());
    if (!std::get<0>(ret)) {
        throw JSONRPCError(RPC_WALLET_ERROR,
                           strprintf("SubmitTx failed: txid=%s, %s", pBaseTx->GetHash().GetHex(), std::get<1>(ret)));
    }

    Object obj;
    obj.push_back(Pair("txid", std::get<1>(ret)));

    return obj;
}

/*************************************************<< DEX >>**************************************************/
Value submitdexbuylimitordertx(const Array& params, bool fHelp) {
    if (fHelp || params.size() < 4 || params.size() > 8) {
        throw runtime_error(
            "submitdexbuylimitordertx \"addr\" \"coin_symbol\" \"symbol:asset_amount:unit\"  price"
                " [dex_id] [symbol:fee:unit] \"[memo]\"\n"
            "\nsubmit a dex buy limit price order tx.\n"
            "\nArguments:\n"
            "1.\"addr\": (string required) order owner address\n"
            "2.\"coin_symbol\": (string required) coin type to pay\n"
            "3.\"symbol:asset_amount:unit\",(string:numeric:string,required) the target amount to buy \n "
            "   default symbol is GVC, default unit is sawi.\n"
            "4.\"price\": (numeric, required) bidding price willing to buy\n"
            "5.\"dex_id\": (numeric, optional) Decentralized Exchange(DEX) ID, default is 0\n"
            "6.\"symbol:fee:unit\":(string:numeric:string, optional) fee paid for miner, default is GVC:10000:sawi\n"
            "7.\"memo\": (string, optional) memo\n"
            "\nResult:\n"
            "\"txid\" (string) The transaction id.\n"
            "\nExamples:\n"
            + HelpExampleCli("submitdexbuylimitordertx", "\"10-3\" \"WUSD\" \"GVC:1000000000:sawi\""
                             " 100000000 1 \"PRIVATE\"\n")
            + "\nAs json rpc call\n"
            + HelpExampleRpc("submitdexbuylimitordertx", "\"10-3\", \"WUSD\", \"GVC:1000000000:sawi\","
                             " 100000000, 1, \"PRIVATE\"\n")
        );
    }

    EnsureWalletIsUnlocked();
    int32_t validHeight = chainActive.Height();
    FeatureForkVersionEnum version = GetFeatureForkVersion(validHeight);
    const TxType txType = version  < MAJOR_VER_R3 ? DEX_LIMIT_BUY_ORDER_TX : DEX_ORDER_TX;

    const CUserID& userId          = RPC_PARAM::GetUserId(params[0], true);
    const TokenSymbol& coinSymbol  = RPC_PARAM::GetOrderCoinSymbol(params[1]);
    ComboMoney assetInfo           = RPC_PARAM::GetComboMoney(params[2], SYMB::GVC);
    uint64_t price                 = RPC_PARAM::GetPrice(params[3]);
    DexID dexId                    = RPC_PARAM::GetDexId(params, 4);
    ComboMoney cmFee               = RPC_PARAM::GetFee(params, 5, txType);
    string memo                    = RPC_PARAM::GetMemo(params, 6);

    RPC_PARAM::CheckOrderSymbols(__func__, coinSymbol, assetInfo.symbol);
    // Get account for checking balance
    CAccount txAccount = RPC_PARAM::GetUserAccount(*pCdMan->pAccountCache, userId);
    RPC_PARAM::CheckAccountBalance(txAccount, cmFee.symbol, SUB_FREE, cmFee.GetSawiAmount());
    uint64_t coinAmount = CDEXOrderBaseTx::CalcCoinAmount(assetInfo.GetSawiAmount(), price);
    RPC_PARAM::CheckAccountBalance(txAccount, coinSymbol, FREEZE, coinAmount);

    DexOperatorDetail operatorDetail = RPC_PARAM::GetDexOperator(dexId);

    shared_ptr<CDEXOrderBaseTx> pOrderBaseTx;
    if (version < MAJOR_VER_R3) {
        // TODO: check dex_id must be 0
        // TODO: check memo must be empty
        pOrderBaseTx = make_shared<CDEXBuyLimitOrderTx>(
            userId, validHeight, cmFee.symbol, cmFee.GetSawiAmount(), coinSymbol, assetInfo.symbol,
            assetInfo.GetSawiAmount(), price);
    } else {
        pOrderBaseTx =
            make_shared<CDEXOrderTx>(userId, validHeight, cmFee.symbol, cmFee.GetSawiAmount(),
                                     ORDER_LIMIT_PRICE, ORDER_BUY, coinSymbol, assetInfo.symbol,
                                     0, assetInfo.GetSawiAmount(), price, dexId, memo);
    }
    return SubmitOrderTx(txAccount.keyid, operatorDetail, pOrderBaseTx);
}

Value submitdexselllimitordertx(const Array& params, bool fHelp) {
    if (fHelp || params.size() < 4 || params.size() > 8) {
        throw runtime_error(
            "submitdexselllimitordertx \"addr\" \"coin_symbol\" \"asset\" price"
                " [dex_id] [symbol:fee:unit] \"[memo]\"\n"
            "\nArguments:\n"
            "1.\"addr\": (string required) order owner address\n"
            "2.\"coin_symbol\": (string required) coin type to pay\n"
            "3.\"asset_symbol:asset_amount:unit\",(comboMoney,required) the target amount to sell. "
            "   default symbol is GVC, default unit is sawi.\n"
            "4.\"price\": (numeric, required) bidding price willing to buy\n"
            "5.\"dex_id\": (numeric, optional) Decentralized Exchange(DEX) ID, default is 0\n"
            "6.\"symbol:fee:unit\":(string:numeric:string, optional) fee paid for miner, default is GVC:10000:sawi\n"
            "7.\"memo\": (string, optional) memo\n"
            "\nResult:\n"
            "\"txid\" (string) The transaction id.\n"
            "\nExamples:\n"
            + HelpExampleCli("submitdexselllimitordertx", "\"10-3\" \"WUSD\" \"GVC:1000000000:sawi\""
                             " 100000000 1 \"PRIVATE\"\n")
            + "\nAs json rpc call\n"
            + HelpExampleRpc("submitdexselllimitordertx", "\"10-3\", \"WUSD\", \"GVC:1000000000:sawi\","
                             " 100000000, 1, \"PRIVATE\"\n")
        );
    }

    EnsureWalletIsUnlocked();
    int32_t validHeight = chainActive.Height();
    FeatureForkVersionEnum version = GetFeatureForkVersion(validHeight);
    const TxType txType = version  < MAJOR_VER_R3 ? DEX_LIMIT_SELL_ORDER_TX : DEX_ORDER_TX;

    const CUserID& userId          = RPC_PARAM::GetUserId(params[0], true);
    const TokenSymbol& coinSymbol  = RPC_PARAM::GetOrderCoinSymbol(params[1]);
    ComboMoney assetInfo           = RPC_PARAM::GetComboMoney(params[2], SYMB::GVC);
    uint64_t price                 = RPC_PARAM::GetPrice(params[3]);
    DexID dexId                    = RPC_PARAM::GetDexId(params, 4);
    ComboMoney cmFee               = RPC_PARAM::GetFee(params, 5, txType);
    string memo                    = RPC_PARAM::GetMemo(params, 6);

    RPC_PARAM::CheckOrderSymbols(__FUNCTION__, coinSymbol, assetInfo.symbol);
    // Get account for checking balance
    CAccount txAccount = RPC_PARAM::GetUserAccount(*pCdMan->pAccountCache, userId);
    RPC_PARAM::CheckAccountBalance(txAccount, cmFee.symbol, SUB_FREE, cmFee.GetSawiAmount());
    RPC_PARAM::CheckAccountBalance(txAccount, assetInfo.symbol, FREEZE, assetInfo.GetSawiAmount());

    DexOperatorDetail operatorDetail = RPC_PARAM::GetDexOperator(dexId);

    shared_ptr<CDEXOrderBaseTx> pOrderBaseTx;
    if (version < MAJOR_VER_R3) {
        // TODO: check dex_id must be 0
        // TODO: check memo must be empty
        pOrderBaseTx = make_shared<CDEXSellLimitOrderTx>(
            userId, validHeight, cmFee.symbol, cmFee.GetSawiAmount(), coinSymbol, assetInfo.symbol,
            assetInfo.GetSawiAmount(), price);
    } else {
        pOrderBaseTx =
            make_shared<CDEXOrderTx>(userId, validHeight, cmFee.symbol, cmFee.GetSawiAmount(),
                                     ORDER_LIMIT_PRICE, ORDER_SELL, coinSymbol, assetInfo.symbol,
                                     0, assetInfo.GetSawiAmount(), price, dexId, memo);
    }

    return SubmitOrderTx(txAccount.keyid, operatorDetail, pOrderBaseTx);
}

Value submitdexbuymarketordertx(const Array& params, bool fHelp) {
     if (fHelp || params.size() < 3 || params.size() > 7) {
        throw runtime_error(
            "submitdexbuymarketordertx \"addr\" \"coin_symbol\" coin_amount \"asset_symbol\" "
                " [dex_id] [symbol:fee:unit] \"[memo]\"\n"
            "\nsubmit a dex buy market price order tx.\n"
            "\nArguments:\n"
            "1.\"addr\": (string required) order owner address\n"
            "2.\"coin_symbol:coin_amount:unit\",(comboMoney,required) the target coin amount for buying asset \n "
            "   default symbol is WUSD, default unit is sawi.\n"
            "3.\"asset_symbol\": (string required), asset type to buy\n"
            "4.\"dex_id\": (numeric, optional) Decentralized Exchange(DEX) ID, default is 0\n"
            "5.\"symbol:fee:unit\":(string:numeric:string, optional) fee paid for miner, default is GVC:10000:sawi\n"
            "6.\"memo\": (string, optional) memo\n"
            "\nResult:\n"
            "\"txid\" (string) The transaction id.\n"
            "\nExamples:\n"
            + HelpExampleCli("submitdexbuymarketordertx", "\"10-3\" \"WUSD:200000000:sawi\"  \"GVC\""
                             " 1 \"PRIVATE\"\n")
            + "\nAs json rpc call\n"
            + HelpExampleRpc("submitdexbuymarketordertx", "\"10-3\", \"WUSD:200000000:sawi\", \"GVC\","
                             " 1, \"PRIVATE\"\n")
        );
    }

    EnsureWalletIsUnlocked();
    int32_t validHeight = chainActive.Height();
    FeatureForkVersionEnum version = GetFeatureForkVersion(validHeight);
    const TxType txType = version  < MAJOR_VER_R3 ? DEX_MARKET_BUY_ORDER_TX : DEX_ORDER_TX;

    const CUserID& userId          = RPC_PARAM::GetUserId(params[0], true);
    ComboMoney coinInfo            = RPC_PARAM::GetComboMoney(params[1], SYMB::WUSD);
    const TokenSymbol& assetSymbol = RPC_PARAM::GetOrderAssetSymbol(params[2]);
    DexID dexId                    = RPC_PARAM::GetDexId(params, 3);
    ComboMoney cmFee               = RPC_PARAM::GetFee(params, 4, txType);
    string memo                    = RPC_PARAM::GetMemo(params, 5);

    RPC_PARAM::CheckOrderSymbols(__FUNCTION__, coinInfo.symbol, assetSymbol);
    // Get account for checking balance
    CAccount txAccount = RPC_PARAM::GetUserAccount(*pCdMan->pAccountCache, userId);
    RPC_PARAM::CheckAccountBalance(txAccount, cmFee.symbol, SUB_FREE, cmFee.GetSawiAmount());
    RPC_PARAM::CheckAccountBalance(txAccount, coinInfo.symbol, FREEZE, coinInfo.GetSawiAmount());

    DexOperatorDetail operatorDetail = RPC_PARAM::GetDexOperator(dexId);

    shared_ptr<CDEXOrderBaseTx> pOrderBaseTx;
    if (version < MAJOR_VER_R3) {
        // TODO: check dex_id must be 0
        // TODO: check memo must be empty
        pOrderBaseTx = make_shared<CDEXBuyMarketOrderTx>(userId, validHeight, cmFee.symbol,
                                                         cmFee.GetSawiAmount(), coinInfo.symbol,
                                                         assetSymbol, coinInfo.GetSawiAmount());
    } else {
        pOrderBaseTx = make_shared<CDEXOrderTx>(
            userId, validHeight, cmFee.symbol, cmFee.GetSawiAmount(), ORDER_MARKET_PRICE, ORDER_BUY,
            coinInfo.symbol, assetSymbol, coinInfo.GetSawiAmount(), 0, 0, dexId, memo);
    }

    return SubmitOrderTx(txAccount.keyid, operatorDetail, pOrderBaseTx);
}

Value submitdexsellmarketordertx(const Array& params, bool fHelp) {
    if (fHelp || params.size() < 3 || params.size() > 7) {
        throw runtime_error(
            "submitdexsellmarketordertx \"addr\" \"coin_symbol\" \"asset_symbol\" asset_amount "
                " [dex_id] [symbol:fee:unit] \"[memo]\"\n"
            "\nsubmit a dex sell market price order tx.\n"
            "\nArguments:\n"
            "1.\"addr\": (string required) order owner address\n"
            "2.\"coin_symbol\": (string required) coin type to pay\n"
            "3.\"asset_symbol:asset_amount:unit\",(comboMoney,required) the target amount to sell, "
                                                  "default symbol is GVC, default unit is sawi.\n"
            "4.\"dex_id\": (numeric, optional) Decentralized Exchange(DEX) ID, default is 0\n"
            "5.\"symbol:fee:unit\":(string:numeric:string, optional) fee paid for miner, default is GVC:10000:sawi\n"
            "6.\"memo\": (string, optional) memo\n"
            "\nResult:\n"
            "\"txid\" (string) The transaction id.\n"
            "\nExamples:\n"
            + HelpExampleCli("submitdexsellmarketordertx", "\"10-3\" \"WUSD\" \"GVC:200000000:sawi\""
                             " 1 \"PRIVATE\"\n")
            + "\nAs json rpc call\n"
            + HelpExampleRpc("submitdexsellmarketordertx", "\"10-3\", \"WUSD\", \"GVC:200000000:sawi\","
                             " 1, \"PRIVATE\"\n")
        );
    }

    EnsureWalletIsUnlocked();
    int32_t validHeight = chainActive.Height();
    FeatureForkVersionEnum version = GetFeatureForkVersion(validHeight);
    const TxType txType = version  < MAJOR_VER_R3 ? DEX_MARKET_SELL_ORDER_TX : DEX_ORDER_TX;

    const CUserID& userId          = RPC_PARAM::GetUserId(params[0], true);
    const TokenSymbol& coinSymbol  = RPC_PARAM::GetOrderCoinSymbol(params[1]);
    ComboMoney assetInfo           = RPC_PARAM::GetComboMoney(params[2], SYMB::GVC);
    DexID dexId                    = RPC_PARAM::GetDexId(params, 3);
    ComboMoney cmFee               = RPC_PARAM::GetFee(params, 4, txType);
    string memo                    = RPC_PARAM::GetMemo(params, 5);

    RPC_PARAM::CheckOrderSymbols(__FUNCTION__, coinSymbol, assetInfo.symbol);
    // Get account for checking balance
    CAccount txAccount = RPC_PARAM::GetUserAccount(*pCdMan->pAccountCache, userId);
    RPC_PARAM::CheckAccountBalance(txAccount, cmFee.symbol, SUB_FREE, cmFee.GetSawiAmount());
    RPC_PARAM::CheckAccountBalance(txAccount, assetInfo.symbol, FREEZE, assetInfo.GetSawiAmount());

    DexOperatorDetail operatorDetail = RPC_PARAM::GetDexOperator(dexId);

    shared_ptr<CDEXOrderBaseTx> pOrderBaseTx;
    if (version < MAJOR_VER_R3) {
        // TODO: check dex_id must be 0
        // TODO: check memo must be empty
        pOrderBaseTx = make_shared<CDEXSellMarketOrderTx>(
            userId, validHeight, cmFee.symbol, cmFee.GetSawiAmount(), coinSymbol, assetInfo.symbol,
            assetInfo.GetSawiAmount());
    } else {
        pOrderBaseTx =
            make_shared<CDEXOrderTx>(userId, validHeight, cmFee.symbol, cmFee.GetSawiAmount(),
                                     ORDER_MARKET_PRICE, ORDER_SELL, coinSymbol, assetInfo.symbol,
                                     0, assetInfo.GetSawiAmount(), 0, dexId, memo);
    }

    return SubmitOrderTx(txAccount.keyid, operatorDetail, pOrderBaseTx);
}

Value gendexoperatorordertx(const Array& params, bool fHelp) {
    if (fHelp || params.size() < 3 || params.size() > 11) {
        throw runtime_error(
            "gendexoperatorordertx \"addr\" \"order_type\" \"order_side\" \"coins\" \"assets\""
                " dex_id \"pubilc_mode\" taker_fee_ratio maker_fee_ratio [symbol:fee:unit] \"[memo]\"\n"
            "\ngenerator an operator dex order tx, support operator config, and must be signed by operator before sumiting.\n"
            "\nArguments:\n"
            "1.\"addr\": (string required) order owner address\n"
            "2.\"order_type\": (string required) order type, must be in (LIMIT_PRICE, MARKET_PRICE)\n"
            "3.\"order_side\": (string required) order side, must be in (BUY, SELL)\n"
            "4.\"symbol:coins:unit\": (string:numeric:string, required) the coins(money) of order, coins=0 if not market buy order, \n"
                                                  "default symbol is WUSD, default unit is sawi.\n"
            "5.\"symbol:assets:unit\",(string:numeric:string, required) the assets of order, assets=0 if market buy order"
                                                  "default symbol is GVC, default unit is sawi.\n"
            "6.\"price\": (numeric, required) expected price of order\n"
            "7.\"dex_id\": (numeric, required) Decentralized Exchange(DEX) ID, default is 0\n"
            "8.\"public_mode\": (string, required) indicate the order is PUBLIC or PRIVATE, defualt is PUBLIC\n"
            "9.\"taker_fee_ratio\": (numeric, required) taker fee ratio config by operator, boost 100000000\n"
            "10.\"maker_fee_ratio\": (numeric, required) maker fee ratio config by operator, boost 100000000\n"
            "11.\"symbol:fee:unit\":(string:numeric:string, optional) fee paid for miner, default is GVC:10000:sawi\n"
            "12.\"memo\": (string, optional) memo\n"
            "\nResult:\n"
            "\"txid\" (string) The transaction id.\n"
            "\nExamples:\n"
            + HelpExampleCli("gendexoperatorordertx", "\"10-3\" \"LIMIT_PRICE\" \"BUY\" \"GVC:2000000000:sawi\""
                             " \"WUSD:0\" 100000000 0 \"PUBLIC\" 80000 40000\n")
            + "\nAs json rpc call\n"
            + HelpExampleRpc("gendexoperatorordertx", "\"10-3\", \"LIMIT_PRICE\", \"BUY\", \"GVC:2000000000:sawi\","
                             " \"WUSD:0\", 100000000, 0, \"PUBLIC\", 80000, 40000\n")
        );
    }

    EnsureWalletIsUnlocked();
    int32_t validHeight = chainActive.Height();
    FeatureForkVersionEnum version = GetFeatureForkVersion(validHeight);
    const TxType txType = version  < MAJOR_VER_R3 ? DEX_MARKET_SELL_ORDER_TX : DEX_ORDER_TX;

    const CUserID &userId    = RPC_PARAM::GetUserId(params[0], true);
    OrderType orderType      = RPC_PARAM::GetOrderType(params[1]);
    OrderSide orderSide      = RPC_PARAM::GetOrderSide(params[1]);
    const ComboMoney &coins  = RPC_PARAM::GetComboMoney(params[1]);
    const ComboMoney &assets = RPC_PARAM::GetComboMoney(params[2], SYMB::GVC);
    uint64_t price           = RPC_PARAM::GetPrice(params[3]);
    DexID dexId              = RPC_PARAM::GetDexId(params[3]);
    PublicMode publicMode    = RPC_PARAM::GetOrderPublicMode(params[4]);
    uint64_t takerFeeRatio   = RPC_PARAM::GetOperatorFeeRatio(params[5]);
    uint64_t makerFeeRatio   = RPC_PARAM::GetOperatorFeeRatio(params[6]);
    ComboMoney cmFee         = RPC_PARAM::GetFee(params, 7, txType);
    string memo              = RPC_PARAM::GetMemo(params, 8);

    RPC_PARAM::CheckOrderSymbols(__func__, coins.symbol, assets.symbol);

    static_assert(MIN_DEX_ORDER_AMOUNT < INT64_MAX, "minimum dex order amount out of range");
    if (orderType == ORDER_MARKET_PRICE && orderSide == ORDER_BUY) {
        if (assets.GetSawiAmount() != 0)
            throw JSONRPCError(RPC_INVALID_PARAMS, strprintf("asset amount=%llu must be 0 when "
                "order_type=%s, order_side=%s", assets.GetSawiAmount(), kOrderTypeHelper.GetName(orderType),
                kOrderSideHelper.GetName(orderSide)));
        RPC_PARAM::CheckOrderAmount(coins.symbol, coins.GetSawiAmount(), "coin");
    } else {
        if (coins.GetSawiAmount() != 0)
            throw JSONRPCError(RPC_INVALID_PARAMS, strprintf("coin amount=%llu must be 0 when "
                "order_type=%s, order_side=%s", coins.GetSawiAmount(), kOrderTypeHelper.GetName(orderType),
                kOrderSideHelper.GetName(orderSide)));

        RPC_PARAM::CheckOrderAmount(assets.symbol, assets.GetSawiAmount(), "asset");
    }

    if (orderType == ORDER_MARKET_PRICE) {
        if (price != 0)
            throw JSONRPCError(RPC_INVALID_PARAMS, strprintf("price must be 0 when order_type=%s",
                    price, kOrderTypeHelper.GetName(orderType)));
    } else {
        // TODO: should check the price range??
        if (price <= 0)
            throw JSONRPCError(RPC_INVALID_PARAMS, strprintf("price=%llu out of range, order_type=%s",
                    price, kOrderTypeHelper.GetName(orderType)));
    }

    DexOperatorDetail operatorDetail = RPC_PARAM::GetDexOperator(dexId);

    if (!pCdMan->pAccountCache->HaveAccount(operatorDetail.fee_receiver_regid))
        throw JSONRPCError(RPC_INVALID_PARAMS, strprintf("operator account not existed! operator_regid=%s",
            operatorDetail.fee_receiver_regid.ToString()));

    // Get account for checking balance
    CAccount txAccount = RPC_PARAM::GetUserAccount(*pCdMan->pAccountCache, userId);
    RPC_PARAM::CheckAccountBalance(txAccount, cmFee.symbol, SUB_FREE, cmFee.GetSawiAmount());


    if (orderSide == ORDER_BUY) {
        uint64_t coinAmount = coins.GetSawiAmount();
        if (orderType == ORDER_LIMIT_PRICE && orderSide == ORDER_BUY)
            coinAmount = RPC_PARAM::CalcCoinAmount(assets.amount, price);
        RPC_PARAM::CheckAccountBalance(txAccount, coins.symbol, FREEZE, coinAmount);
    } else {
        assert(orderSide == ORDER_SELL);
        RPC_PARAM::CheckAccountBalance(txAccount, assets.symbol, FREEZE, assets.GetSawiAmount());
    }


    shared_ptr<CDEXOrderBaseTx> pOrderBaseTx;
    if (version < MAJOR_VER_R3) {
        throw JSONRPCError(RPC_INVALID_PARAMS, strprintf("unsupport to call %s() before height=%d",
            __func__, SysCfg().GetVer3ForkHeight()));
    }

    shared_ptr<CBaseTx> pTx = make_shared<CDEXOperatorOrderTx>(
        userId, validHeight, cmFee.symbol, cmFee.GetSawiAmount(), orderType, orderSide,
        coins.symbol, assets.symbol, coins.GetSawiAmount(), assets.GetSawiAmount(), price, dexId,
        publicMode, memo, OperatorFeeRatios(makerFeeRatio, takerFeeRatio),
        operatorDetail.fee_receiver_regid);

    CDataStream ds(SER_DISK, CLIENT_VERSION);
    ds << pTx;

    Object obj;
    obj.push_back(Pair("rawtx", HexStr(ds.begin(), ds.end())));
    return SubmitOrderTx(txAccount.keyid, operatorDetail, pOrderBaseTx);
}

Value submitdexcancelordertx(const Array& params, bool fHelp) {
    if (fHelp || params.size() < 2 || params.size() > 3) {
        throw runtime_error(
            "submitdexcancelordertx \"addr\" \"txid\" [symbol:fee:unit]\n"
            "\nsubmit a dex cancel order tx.\n"
            "\nArguments:\n"
            "1.\"addr\": (string required) order owner address\n"
            "2.\"txid\": (string required) order tx want to cancel\n"
            "3.\"symbol:fee:unit\":(string:numeric:string, optional) fee paid for miner, default is GVC:10000:sawi\n"
            "\nResult:\n"
            "\"txid\" (string) The transaction id.\n"
            "\nExamples:\n"
            + HelpExampleCli("submitdexcancelordertx", "\"WiZx6rrsBn9sHjwpvdwtMNNX2o31s3DEHH\" "
                             "\"c5287324b89793fdf7fa97b6203dfd814b8358cfa31114078ea5981916d7a8ac\" ")
            + "\nAs json rpc call\n"
            + HelpExampleRpc("submitdexcancelordertx", "\"WiZx6rrsBn9sHjwpvdwtMNNX2o31s3DEHH\", "\
                             "\"c5287324b89793fdf7fa97b6203dfd814b8358cfa31114078ea5981916d7a8ac\"")
        );
    }

    EnsureWalletIsUnlocked();

    const CUserID& userId = RPC_PARAM::GetUserId(params[0], true);
    const uint256& txid   = RPC_PARAM::GetTxid(params[1], "txid");
    ComboMoney cmFee      = RPC_PARAM::GetFee(params, 2, DEX_CANCEL_ORDER_TX);

    // Get account for checking balance
    CAccount account = RPC_PARAM::GetUserAccount(*pCdMan->pAccountCache, userId);
    RPC_PARAM::CheckAccountBalance(account, cmFee.symbol, SUB_FREE, cmFee.GetSawiAmount());

    // check active order tx
    RPC_PARAM::CheckActiveOrderExisted(*pCdMan->pDexCache, txid);

    int32_t validHeight = chainActive.Height();
    CDEXCancelOrderTx tx(userId, validHeight, cmFee.symbol, cmFee.GetSawiAmount(), txid);
    return SubmitTx(account.keyid, tx);
}

Value submitdexsettletx(const Array& params, bool fHelp) {
     if (fHelp || params.size() < 2 || params.size() > 3) {
        throw runtime_error(
            "submitdexsettletx \"addr\" \"deal_items\" [symbol:fee:unit]\n"
            "\nsubmit a dex settle tx.\n"
            "\nArguments:\n"
            "1.\"addr\": (string required) settle owner address\n"
            "2.\"deal_items\": (string required) deal items in json format\n"
            " [\n"
            "   {\n"
            "      \"buy_order_id\":\"txid\", (string, required) order txid of buyer\n"
            "      \"sell_order_id\":\"txid\", (string, required) order txid of seller\n"
            "      \"deal_price\":n (numeric, required) deal price\n"
            "      \"deal_coin_amount\":n (numeric, required) deal amount of coin\n"
            "      \"deal_asset_amount\":n (numeric, required) deal amount of asset\n"
            "   }\n"
            "       ,...\n"
            " ]\n"
            "3.\"symbol:fee:unit\":(string:numeric:string, optional) fee paid for miner, default is GVC:10000:sawi\n"
            "\nResult:\n"
            "\"txid\" (string) The transaction id.\n"
            "\nExamples:\n"
            + HelpExampleCli("submitdexsettletx", "\"WiZx6rrsBn9sHjwpvdwtMNNX2o31s3DEHH\" "
                           "\"[{\\\"buy_order_id\\\":\\\"c5287324b89793fdf7fa97b6203dfd814b8358cfa31114078ea5981916d7a8ac\\\", "
                           "\\\"sell_order_id\\\":\\\"c5287324b89793fdf7fa97b6203dfd814b8358cfa31114078ea5981916d7a8a1\\\", "
                           "\\\"deal_price\\\":100000000,"
                           "\\\"deal_coin_amount\\\":100000000,"
                           "\\\"deal_asset_amount\\\":100000000}]\" ")
            + "\nAs json rpc call\n"
            + HelpExampleRpc("submitdexsettletx", "\"WiZx6rrsBn9sHjwpvdwtMNNX2o31s3DEHH\", "\
                           "[{\"buy_order_id\":\"c5287324b89793fdf7fa97b6203dfd814b8358cfa31114078ea5981916d7a8ac\", "
                           "\"sell_order_id\":\"c5287324b89793fdf7fa97b6203dfd814b8358cfa31114078ea5981916d7a8a1\", "
                           "\"deal_price\":100000000,"
                           "\"deal_coin_amount\":100000000,"
                           "\"deal_asset_amount\":100000000}]")
        );
    }

    EnsureWalletIsUnlocked();

    const CUserID &userId = RPC_PARAM::GetUserId(params[0]);
    Array dealItemArray = params[1].get_array();
    ComboMoney fee = RPC_PARAM::GetFee(params, 2, DEX_TRADE_SETTLE_TX);

    vector<CDEXSettleTx::DealItem> dealItems;
    for (auto dealItemObj : dealItemArray) {
        CDEXSettleTx::DealItem dealItem;
        const Value& buy_order_id      = JSON::GetObjectFieldValue(dealItemObj, "buy_order_id");
        dealItem.buyOrderId            = RPC_PARAM::GetTxid(buy_order_id, "buy_order_id");
        const Value& sell_order_id     = JSON::GetObjectFieldValue(dealItemObj, "sell_order_id");
        dealItem.sellOrderId           = RPC_PARAM::GetTxid(sell_order_id.get_str(), "sell_order_id");
        const Value& deal_price        = JSON::GetObjectFieldValue(dealItemObj, "deal_price");
        dealItem.dealPrice             = RPC_PARAM::GetPrice(deal_price);
        const Value& deal_coin_amount  = JSON::GetObjectFieldValue(dealItemObj, "deal_coin_amount");
        dealItem.dealCoinAmount        = AmountToRawValue(deal_coin_amount);
        const Value& deal_asset_amount = JSON::GetObjectFieldValue(dealItemObj, "deal_asset_amount");
        dealItem.dealAssetAmount       = AmountToRawValue(deal_asset_amount);
        dealItems.push_back(dealItem);
    }

    // Get account for checking balance
    CAccount account = RPC_PARAM::GetUserAccount(*pCdMan->pAccountCache, userId);
    RPC_PARAM::CheckAccountBalance(account, fee.symbol, SUB_FREE, fee.GetSawiAmount());

    int32_t validHeight = chainActive.Height();
    CDEXSettleTx tx(userId, validHeight, fee.symbol, fee.GetSawiAmount(), dealItems);
    return SubmitTx(account.keyid, tx);
}

Value getdexorder(const Array& params, bool fHelp) {
     if (fHelp || params.size() != 1) {
        throw runtime_error(
            "getdexorder \"order_id\"\n"
            "\nget dex order detail.\n"
            "\nArguments:\n"
            "1.\"order_id\":    (string, required) order txid\n"
            "\nResult: object of order detail\n"
            "\nExamples:\n"
            + HelpExampleCli("getdexorder", "\"c5287324b89793fdf7fa97b6203dfd814b8358cfa31114078ea5981916d7a8ac\"")
            + "\nAs json rpc call\n"
            + HelpExampleRpc("getdexorder", "\"c5287324b89793fdf7fa97b6203dfd814b8358cfa31114078ea5981916d7a8ac\"")
        );
    }
    const uint256 &orderId = RPC_PARAM::GetTxid(params[0], "order_id");

    auto pDexCache = pCdMan->pDexCache;
    CDEXOrderDetail orderDetail;
    if (!pDexCache->GetActiveOrder(orderId, orderDetail))
        throw JSONRPCError(RPC_INVALID_PARAMS, strprintf("The order not exists or inactive! order_id=%s", orderId.ToString()));

    Object obj;
    DEX_DB::OrderToJson(orderId, orderDetail, obj);
    return obj;
}

extern Value getdexsysorders(const Array& params, bool fHelp) {
     if (fHelp || params.size() > 1) {
        throw runtime_error(
            "getdexsysorders [\"height\"]\n"
            "\nget dex system-generated active orders by block height.\n"
            "\nArguments:\n"
            "1.\"height\":  (numeric, optional) block height, default is current tip block height\n"
            "\nResult:\n"
            "\"height\"     (string) the specified block height.\n"
            "\"orders\"     (string) a list of system-generated DEX orders.\n"
            "\nExamples:\n"
            + HelpExampleCli("getdexsysorders", "10")
            + "\nAs json rpc call\n"
            + HelpExampleRpc("getdexsysorders", "10")
        );
    }

    int64_t tipHeight = chainActive.Height();
    int64_t height    = tipHeight;
    if (params.size() > 0)
        height = params[0].get_int64();

    if (height < 0 || height > tipHeight) {
        throw JSONRPCError(RPC_INVALID_PARAMS, strprintf("height=%d must >= 0 and <= tip_height=%d", height, tipHeight));
    }

    auto pGetter = pCdMan->pDexCache->CreateSysOrdersGetter();
    if (!pGetter->Execute(height)) {
        throw JSONRPCError(RPC_INVALID_PARAMS, strprintf("get system-generated orders error! height=%d", height));
    }

    Object obj;
    obj.push_back(Pair("height", height));
    pGetter->ToJson(obj);

    return obj;
}

extern Value getdexorders(const Array& params, bool fHelp) {
     if (fHelp || params.size() > 4) {
        throw runtime_error(
            "getdexorders [\"begin_height\"] [\"end_height\"] [\"max_count\"] [\"last_pos_info\"]\n"
            "\nget dex all active orders by block height range.\n"
            "\nArguments:\n"
            "1.\"begin_height\":    (numeric, optional) the begin block height, default is 0\n"
            "2.\"end_height\":      (numeric, optional) the end block height, default is current tip block height\n"
            "3.\"max_count\":       (numeric, optional) the max order count to get, default is 500\n"
            "4.\"last_pos_info\":   (string, optional) the last position info to get more orders, default is empty\n"
            "\nResult:\n"
            "\"begin_height\"       (numeric) the begin block height of returned orders.\n"
            "\"end_height\"         (numeric) the end block height of returned orders.\n"
            "\"has_more\"           (bool) has more orders in db.\n"
            "\"last_pos_info\"      (string) the last position info to get more orders.\n"
            "\"count\"              (numeric) the count of returned orders.\n"
            "\"orders\"             (string) a list of system-generated DEX orders.\n"
            "\nExamples:\n"
            + HelpExampleCli("getdexorders", "0 100 500")
            + "\nAs json rpc call\n"
            + HelpExampleRpc("getdexorders", "0, 100, 500")
        );
    }

    int64_t tipHeight = chainActive.Height();
    int64_t beginHeight = 0;
    if (params.size() > 0)
        beginHeight = params[0].get_int64();
    if (beginHeight < 0 || beginHeight > tipHeight) {
        throw JSONRPCError(RPC_INVALID_PARAMS, strprintf("begin_height=%d must >= 0 and <= tip_height=%d", beginHeight, tipHeight));
    }

    int64_t endHeight = tipHeight;
    if (params.size() > 1)
        endHeight = params[1].get_int64();
    if (endHeight < beginHeight || endHeight > tipHeight) {
        throw JSONRPCError(RPC_INVALID_PARAMS, strprintf("end_height=%d must >= begin_height=%d and <= tip_height=%d",
            endHeight, beginHeight, tipHeight));
    }


    int64_t maxCount = 500;
    if (params.size() > 2) {
        maxCount = params[2].get_int64();
        if (maxCount < 0)
            throw JSONRPCError(RPC_INVALID_PARAMS, strprintf("max_count=%d must >= 0", maxCount));
    }

    DEXBlockOrdersCache::KeyType lastKey;
    if (params.size() > 3) {
        string lastPosInfo = RPC_PARAM::GetBinStrFromHex(params[3], "last_pos_info");
        auto err = DEX_DB::ParseLastPos(lastPosInfo, lastKey);
        if (err)
            throw JSONRPCError(RPC_INVALID_PARAMS, strprintf("Invalid last_pos_info! %s", *err));
        uint32_t lastHeight = DEX_DB::GetHeight(lastKey);
        if (lastHeight < beginHeight || lastHeight > endHeight)
            throw JSONRPCError(RPC_INVALID_PARAMS,
                               strprintf("Invalid last_pos_info! height of last_pos_info is not in "
                                         "range(begin=%d,end=%d) ",
                                         beginHeight, endHeight));
    }

    auto pGetter = pCdMan->pDexCache->CreateOrdersGetter();
    if (!pGetter->Execute(beginHeight, endHeight, maxCount, lastKey)) {
        throw JSONRPCError(RPC_INVALID_PARAMS, strprintf("get all active orders error! begin_height=%d, end_height=%d",
            beginHeight, endHeight));
    }

    string newLastPosInfo;
    if (pGetter->has_more) {
        auto err = DEX_DB::MakeLastPos(pGetter->last_key, newLastPosInfo);
        if (err)
            throw JSONRPCError(RPC_INVALID_PARAMS, strprintf("Make new last_pos_info error! %s", *err));
    }
    Object obj;
    obj.push_back(Pair("begin_height", (int64_t)pGetter->begin_height));
    obj.push_back(Pair("end_height", (int64_t)pGetter->end_height));
    obj.push_back(Pair("has_more", pGetter->has_more));
    obj.push_back(Pair("last_pos_info", HexStr(newLastPosInfo)));
    pGetter->ToJson(obj);
    return obj;
}


void checkAccountRegId(const CUserID uid , const string field){

    if(!uid.is<CRegID>() || !uid.get<CRegID>().IsMature(chainActive.Height())){
        throw JSONRPCError(RPC_INVALID_PARAMS, strprintf("%s have not regid or regid is immature!", field));
    }
    CAccount account ;
    if(!pCdMan->pAccountCache->GetAccount(uid,account))
        throw JSONRPCError(RPC_INVALID_PARAMS, strprintf("%s is a invalid account",field));


}

Value submitdexoperatorregtx(const Array& params, bool fHelp){

    if(fHelp || params.size()< 7  || params.size()>9){
        throw runtime_error(
                "submitdexoperatorregtx  \"addr\" \"owner_uid\" \"fee_receiver_uid\" \"dex_name\" \"portal_url\" \"maker_fee_ratio\" \"taker_fee_ratio\" \"fees\" \"memo\"  "
                "\n register a dex operator\n"
                "\nArguments:\n"
                "1.\"addr\":            (string, required) the dex creator's address\n"
                "2.\"owner_uid\":       (string, required) the dexoperator 's owner account \n"
                "3.\"fee_receiver_uid\":(string, required) the dexoperator 's fee receiver account \n"
                "4.\"dex_name\":        (string, required) dex operator's name \n"
                "5.\"portal_url\":      (string, required) the dex operator's website url \n"
                "6.\"maker_fee_ratio\": (number, required) range is 0 ~ 50000000, 50000000 stand for 50% \n"
                "7.\"taker_fee_ratio\": (number, required) range is 0 ~ 50000000, 50000000 stand for 50% \n"
                "8.\"fee\":             (symbol:fee:unit, optional) tx fee,default is the min fee for the tx type  \n"
                "9 \"memo\":            (string, optional) dex memo \n"
                "\nResult:\n"
                "\"txHash\"             (string) The transaction id.\n"

                "\nExamples:\n"
                + HelpExampleCli("submitdexoperatorregtx", "0-1 0-1 0-2 wayki-dex http://www.wayki-dex.com 2000000 2000000")
                + "\nAs json rpc call\n"
                + HelpExampleRpc("submitdexoperatorregtx", "0-1 0-1 0-2 wayki-dex http://www.wayki-dex.com 2000000 2000000")

                ) ;
    }

    EnsureWalletIsUnlocked();
    const CUserID &userId = RPC_PARAM::GetUserId(params[0].get_str(),true);
    CDEXOperatorRegisterTx::Data ddata ;
    ddata.owner_uid = RPC_PARAM::GetUserId(params[1].get_str()) ;
    ddata.fee_receiver_uid = RPC_PARAM::GetUserId(params[2].get_str()) ;
    checkAccountRegId(ddata.owner_uid, "owner_uid");
    checkAccountRegId(ddata.fee_receiver_uid, "fee_receiver_uid");
    ddata.name = params[3].get_str() ;
    ddata.portal_url = params[4].get_str() ;
    ddata.maker_fee_ratio = AmountToRawValue(params[5]) ;
    ddata.taker_fee_ratio = AmountToRawValue(params[6]) ;
    ComboMoney fee  = RPC_PARAM::GetFee(params, 7, DEX_OPERATOR_REGISTER_TX);
    if(params.size()>= 9 ){
        ddata.memo = params[8].get_str() ;
    }

    if(ddata.memo.size()> MAX_COMMON_TX_MEMO_SIZE){
        throw JSONRPCError(RPC_INVALID_PARAMS,
                strprintf("memo size is too long, its size is %d ,but max memo size is %d ", ddata.memo.size(), MAX_COMMON_TX_MEMO_SIZE)) ;
    }

    // Get account for checking balance
    CAccount account = RPC_PARAM::GetUserAccount(*pCdMan->pAccountCache, userId);
    RPC_PARAM::CheckAccountBalance(account, fee.symbol, SUB_FREE, fee.GetSawiAmount());
    int32_t validHeight = chainActive.Height();

    CDEXOperatorRegisterTx tx(userId, validHeight, fee.symbol, fee.GetSawiAmount(), ddata);
    return SubmitTx(account.keyid, tx);
}

Value submitdexoperatorupdatetx(const Array& params, bool fHelp){

    if(fHelp ||params.size()< 4 || params.size() > 5 ){
        throw runtime_error(
                "submitdexoperatorupdatetx  \"tx_uid\" \"dex_id\" \"update_field\" \"value\" \"fee\" \n"
                "\n register a dex operator\n"
                "\nArguments:\n"
                "1.\"tx_uid\":          (string, required) the tx sender, must be the dexoperaor's owner regid\n"
                "2.\"dex_id\":          (number, required) dex operator's id \n"
                "3.\"update_field\":    (nuber, required) the dexoperator field to update\n"
                "                       1: fee_receiver_regid\n"
                "                       2: dex_name\n"
                "                       3: portal_url\n"
                "                       4: maker_fee_ratio\n"
                "                       5: taker_fee_ratio\n"
                "                       6: owner_regid\n"
                "                       7: memo\n"
                "4.\"value\":           (string, required) updated value \n"
                "5.\"fee\":             (symbol:fee:unit, optional) tx fee,default is the min fee for the tx type  \n"
                "\nResult:\n"
                "\"txHash\"             (string) The transaction id.\n"
                "\nExamples:\n"
                + HelpExampleCli("submitdexoperatorupdatetx", "0-1 1 1 0-3")
                + "\nAs json rpc call\n"
                + HelpExampleRpc("submitdexoperatorupdatetx", "0-1 1 1 0-3")

                ) ;
    }

    EnsureWalletIsUnlocked();
    const CUserID &userId = RPC_PARAM::GetUserId(params[0].get_str(),true);
    CDEXOperatorUpdateData updateData ;
    updateData.dexId = CDEXOperatorUpdateData::UpdateField (params[1].get_int()) ;
    updateData.field = CDEXOperatorUpdateData::UpdateField(params[2].get_int());
    string valueStr = params[3].get_str();
    switch (updateData.field){
        case CDEXOperatorUpdateData::UpdateField::FEE_RECEIVER_UID :
        case CDEXOperatorUpdateData::UpdateField::OWNER_UID :{
            CUserID uid = RPC_PARAM::GetUserId(valueStr);
            if(!uid.is<CRegID>()) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "owner_uid or fee_receiver_uid must be or has regid, "
                                                          "and regid must be muture");
            }
            updateData.value = uid;
            break;
        }
        case CDEXOperatorUpdateData::UpdateField::NAME :
        case CDEXOperatorUpdateData::UpdateField::PORTAL_URL :
        case CDEXOperatorUpdateData::UpdateField::MEMO :
            updateData.value = valueStr;
            break;
        case CDEXOperatorUpdateData::UpdateField::MAKER_FEE_RATIO:
        case CDEXOperatorUpdateData::UpdateField::TAKER_FEE_RATIO:{
            uint64_t uint64V;
            if (!ParseUint64(valueStr, uint64V))
                throw JSONRPCError(RPC_INVALID_PARAMS, strprintf("invalid fee ratio=%s as uint64_t type",
                                                                 valueStr));
            updateData.value = uint64V;
            break;
        }

        default:
            throw JSONRPCError(RPC_INVALID_PARAMETER, "the dex update field code is error,its range is [1,7]");
    }

    string errmsg ;
    string errcode ;
    if(!updateData.Check(errmsg,errcode,chainActive.Height())){
        throw JSONRPCError(RPC_INVALID_PARAMS, errmsg);
    }
    ComboMoney fee = RPC_PARAM::GetFee(params,4, DEX_OPERATOR_UPDATE_TX) ;

    // Get account for checking balance
    CAccount account = RPC_PARAM::GetUserAccount(*pCdMan->pAccountCache, userId);
    RPC_PARAM::CheckAccountBalance(account, fee.symbol, SUB_FREE, fee.GetSawiAmount());
    int32_t validHeight = chainActive.Height();

    CDEXOperatorUpdateTx tx(userId, validHeight, fee.symbol, fee.GetSawiAmount(), updateData);
    return SubmitTx(account.keyid, tx);

}


extern Value getdexoperator(const Array& params, bool fHelp) {
     if (fHelp || params.size() != 1) {
        throw runtime_error(
            "getdexoperator dex_id\n"
            "\nget dex operator by dex_id.\n"
            "\nArguments:\n"
            "1.\"dex_id\":  (numeric, required) dex id\n"
            "\nResult: dex_operator detail\n"
            "\nExamples:\n"
            + HelpExampleCli("getdexoperator", "10")
            + "\nAs json rpc call\n"
            + HelpExampleRpc("getdexoperator", "10")
        );
    }

    uint32_t dexOrderId = params[0].get_int();
    DexOperatorDetail dexOperator;
    if (!pCdMan->pDexCache->GetDexOperator(dexOrderId, dexOperator))
        throw JSONRPCError(RPC_INVALID_PARAMS, strprintf("dex operator does not exist! dex_id=%lu", dexOrderId));

    Object obj = DexOperatorToJson(*pCdMan->pAccountCache, dexOperator);
    obj.insert(obj.begin(), Pair("id", (uint64_t)dexOrderId));
    return obj;
}

extern Value getdexoperatorbyowner(const Array& params, bool fHelp) {
    if (fHelp || params.size() != 1) {
        throw runtime_error(
            "getdexoperatorbyowner owner_addr\n"
            "\nget dex operator by dex operator owner.\n"
            "\nArguments:\n"
            "1.\"owner_addr\":  (string, required) owner address\n"
            "\nResult: dex_operator detail\n"
            "\nExamples:\n"
            + HelpExampleCli("getdexoperatorbyowner", "10-1")
            + "\nAs json rpc call\n"
            + HelpExampleRpc("getdexoperatorbyowner", "10-1")
        );
    }

    const CUserID &userId = RPC_PARAM::GetUserId(params[0]);

    CAccount account = RPC_PARAM::GetUserAccount(*pCdMan->pAccountCache, userId);
    if (!account.IsRegistered())
        throw JSONRPCError(RPC_INVALID_PARAMS, strprintf("account not registered! uid=%s", userId.ToDebugString()));

    DexOperatorDetail dexOperator;
    uint32_t dexOrderId = 0;
    if (!pCdMan->pDexCache->GetDexOperatorByOwner(account.regid, dexOrderId, dexOperator))
        throw JSONRPCError(RPC_INVALID_PARAMS, strprintf("the owner account dos not have a dex operator! uid=%s", userId.ToDebugString()));

    Object obj = DexOperatorToJson(*pCdMan->pAccountCache, dexOperator);
    obj.insert(obj.begin(), Pair("id", (uint64_t)dexOrderId));
    return obj;
}

extern Value getdexorderfee(const Array& params, bool fHelp) {
     if (fHelp || params.size() < 1 || params.size() > 1) {
        throw runtime_error(
            "getdexorderfee \"addr\"\n"
            "\nget dex order fee by account.\n"
            "\nArguments:\n"
            "1.\"addr\":    (string, required) account address\n"
            "\nResult: dex order fee info\n"
            "\nExamples:\n"
            + HelpExampleCli("getdexorderfee", "10-1")
            + "\nAs json rpc call\n"
            + HelpExampleRpc("getdexorderfee", "10-1")
        );
    }

    const CUserID& userId   = RPC_PARAM::GetUserId(params[0], true);

    CAccount account = RPC_PARAM::GetUserAccount(*pCdMan->pAccountCache, userId);

    const TokenSymbol& assetSymbol = RPC_PARAM::GetAssetIssueSymbol(params[0]);

    int32_t height = chainActive.Height();
    uint64_t minFee = 0;
    uint64_t defaultMinFee = 0;
    bool isInit = false;
    Object obj;
    Array symbolArray;
    Array txArray;

    for (auto txType : DEX_ORDER_TX_SET) {
        for (auto symbol : kFeeSymbolSet) {
            if (!GetTxMinFee(txType, height, symbol, minFee))
                throw JSONRPCError(RPC_INTERNAL_ERROR, strprintf("get default min fee of tx failed! "
                    "tx=%s, height=%d, symbol=%s", GetTxTypeName(txType), height, symbol));
            if (!isInit) {
                defaultMinFee = minFee;
            } else {
                if (defaultMinFee != minFee)
                    throw JSONRPCError(RPC_INTERNAL_ERROR, strprintf("the default min fee of tx is not same as others! "
                        "tx=%s, height=%d, symbol=%s, min_fee=%llu, other_min_fee=%llu",
                        GetTxTypeName(txType), height, symbol, minFee, defaultMinFee));
            }
        }
        txArray.push_back(GetTxTypeName(txType));
    }
    for (auto symbol : kFeeSymbolSet)
        symbolArray.push_back(symbol);

    uint64_t actualMinFee = defaultMinFee;
    auto token = account.GetToken(SYMB::GVC);
    if (token.staked_amount > 0) {
        actualMinFee = std::max(std::min(COIN * COIN / token.staked_amount, actualMinFee), (uint64_t)1);
    }

    Object accountObj;
    accountObj.push_back(Pair("addr",  account.keyid.ToAddress()));
    accountObj.push_back(Pair("regid", account.regid.ToString()));
    accountObj.push_back(Pair("nickid", account.nickid.ToString()));

    obj.push_back(Pair("block_height", height));
    obj.push_back(Pair("staked_gvc_amount", token.staked_amount));
    obj.push_back(Pair("actual_min_fee", actualMinFee));
    obj.push_back(Pair("default_min_fee", defaultMinFee));
    obj.push_back(Pair("min_fee_for_pubkey", defaultMinFee * 2));
    obj.push_back(Pair("symbols", symbolArray));
    obj.push_back(Pair("transactions", txArray));
    obj.push_back(Pair("account", accountObj));

    return obj;
}
