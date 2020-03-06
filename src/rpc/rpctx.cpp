// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2017-2018 GreenVenturesChain Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "commons/base58.h"
#include "rpc/core/rpcserver.h"
#include "rpc/core/rpccommons.h"
#include "init.h"
#include "net.h"
#include "netbase.h"
#include "commons/util/util.h"
#include "wallet/wallet.h"
#include "wallet/walletdb.h"
#include "persistence/blockdb.h"
#include "persistence/txdb.h"
#include "config/configuration.h"
#include "miner/miner.h"
#include "main.h"
#include "tx/coinstaketx.h"
#include "tx/nickidregtx.h"
#include "tx/accountregtx.h"
#include "tx/mulsigtx.h"
#include "tx/dextx.h"
#include "tx/txserializer.h"
#include "config/scoin.h"
#include <boost/assign/list_of.hpp>
#include "commons/json/json_spirit_utils.h"
#include "commons/json/json_spirit_value.h"
#include "commons/json/json_spirit_reader.h"
#include "vm/wasm/types/name.hpp"

#define revert(height) ((height<<24) | (height << 8 & 0xff0000) |  (height>>8 & 0xff00) | (height >> 24))

using namespace std;
using namespace boost;
using namespace boost::assign;
using namespace json_spirit;

Value gettxdetail(const Array& params, bool fHelp) {
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "gettxdetail \"txid\"\n"
            "\nget the transaction detail by given transaction hash.\n"
            "\nArguments:\n"
            "1.\"txid\":    (string, required) The hash of transaction.\n"
            "\nResult an object of the transaction detail\n"
            "\nResult:\n"
            "\n\"txid\"\n"
            "\nExamples:\n"
            + HelpExampleCli("gettxdetail","\"c5287324b89793fdf7fa97b6203dfd814b8358cfa31114078ea5981916d7a8ac\"")
            + "\nAs json rpc call\n"
            + HelpExampleRpc("gettxdetail","\"c5287324b89793fdf7fa97b6203dfd814b8358cfa31114078ea5981916d7a8ac\""));

    return GetTxDetailJSON(uint256S(params[0].get_str()));
}

/* Deprecated for common usages but still required for cold mining account registration */
Value submitaccountregistertx(const Array& params, bool fHelp) {
    if (fHelp || params.size() == 0)
        throw runtime_error("submitaccountregistertx \"addr\" [\"fee\"]\n"
            "\nregister account to acquire its regid\n"
            "\nArguments:\n"
            "1.\"addr\":    (string, required)\n"
            "2.\"fee\":     (numeric, optional)\n"
            "\nResult:\n"
            "\"txid\":      (string) The transaction id.\n"
            "\nExamples:\n"
            + HelpExampleCli("submitaccountregistertx", "\"wTtCsc5X9S5XAy1oDuFiEAfEwf8bZHur1W\" 10000")
            + "\nAs json rpc call\n"
            + HelpExampleRpc("submitaccountregistertx", "\"wTtCsc5X9S5XAy1oDuFiEAfEwf8bZHur1W\", 10000"));

    EnsureWalletIsUnlocked();

    const CUserID& txUid = RPC_PARAM::GetUserId(params[0], true);
    ComboMoney fee       = RPC_PARAM::GetFee(params, 1, ACCOUNT_REGISTER_TX);
    int32_t validHeight  = chainActive.Height();

    CAccount account = RPC_PARAM::GetUserAccount(*pCdMan->pAccountCache, txUid);
    RPC_PARAM::CheckAccountBalance(account, SYMB::GVC, SUB_FREE, fee.GetSawiAmount());

    if (account.HaveOwnerPubKey())
        throw JSONRPCError(RPC_WALLET_ERROR, "Account was already registered");

    CPubKey pubkey;
    if (!pWalletMain->GetPubKey(account.keyid, pubkey))
        throw JSONRPCError(RPC_WALLET_ERROR, "Key not found in local wallet");

    CUserID minerUid = CNullID();
    CPubKey minerPubKey;
    if (pWalletMain->GetPubKey(account.keyid, minerPubKey, true) && minerPubKey.IsFullyValid()) {
        minerUid = minerPubKey;
    }

    CAccountRegisterTx tx;
    tx.txUid        = pubkey;
    tx.minerUid     = minerUid;
    tx.llFees       = fee.GetSawiAmount();
    tx.valid_height = validHeight;

    return SubmitTx(account.keyid, tx);
}

Value submitnickidregistertx(const Array& params, bool fHelp) {
    if (fHelp || params.size() < 2)
        throw runtime_error("submitnickidregistertx \"addr or regid\" \"nickid\" [\"fee\"]\n"
                            "\nregister account to acquire its regid\n"
                            "\nArguments:\n"
                            "1.\"addr or regid\":    (string, required)\n"
                            "2.\"nickid\":  (string, required) 12 chars in 12345abcdefghijklmnopqrstuvwxyz\n"
                            "3.\"fee\":     (combomoney, optional)\n"
                            "\nResult:\n"
                            "\"txid\":      (string) The transaction id.\n"
                            "\nExamples:\n"
                            + HelpExampleCli("submitnickidregistertx", "\"wTtCsc5X9S5XAy1oDuFiEAfEwf8bZHur1W\" \"ccssddxx1122\" 1000000")
                            + "\nAs json rpc call\n"
                            + HelpExampleRpc("submitnickidregistertx", "\"wTtCsc5X9S5XAy1oDuFiEAfEwf8bZHur1W\", 1000000"));


    EnsureWalletIsUnlocked();

    const CUserID& txUid = RPC_PARAM::GetUserId(params[0], true);
    string nickid        = params[1].get_str() ;
    ComboMoney fee          = RPC_PARAM::GetFee(params, 2, NICKID_REGISTER_TX);
    int32_t validHeight  = chainActive.Height();

    CAccount account = RPC_PARAM::GetUserAccount(*pCdMan->pAccountCache, txUid);
    RPC_PARAM::CheckAccountBalance(account, fee.symbol, SUB_FREE, fee.GetSawiAmount());
    if(!account.nickid.IsEmpty()){
        throw JSONRPCError(RPC_WALLET_ERROR,"the account have nickid already!");
    }

    if(nickid.find(".") != nickid.npos){
        throw JSONRPCError(RPC_WALLET_ERROR, "nickid can't contain char dot ");
    }

    try{
        if(wasm::name(nickid).value == 0) {
            throw JSONRPCError(RPC_WALLET_ERROR, "nickid's format is error");
        }
    }catch (const wasm_chain::exception& e ){
        throw JSONRPCError(RPC_WALLET_ERROR, e.to_detail_string());
    }catch(...){
        throw;
    }


    std::shared_ptr<CNickIdRegisterTx> pBaseTx = std::make_shared<CNickIdRegisterTx>(txUid, nickid, fee.GetSawiAmount(), fee.symbol, validHeight);

    return SubmitTx(account.keyid, *pBaseTx);
}

Value submitutxospendtx(const Array& params, bool fHelp) {

    if (fHelp || (params.size() != 5 && params.size() != 6))
        throw runtime_error(
                "submitutxospendtx \"prior_utxo_txid\" \"prior_utxo_secret\" \"symbol:coin:unit\" \"symbol:fee:unit\" \"utxo_info\" [\"memo\"]\n"
                "\nSend coins to a given address.\n" +
                HelpRequiringPassphrase() +
                "\nArguments:\n"
                "1.\"prior_utxo_txid\":     (string, required) The utxo txid you want to spend\n"
                "2.\"prior_utxo_secret\":   (string, required) The utxo secret you want to spend\n"
                "3.\"symbol:coin:unit\":    (symbol:amount:unit, required) transferred coins\n"
                "4.\"symbol:fee:unit\":     (symbol:amount:unit, required) fee paid to miner, default is GVC:10000:sawi\n"
                "5.\"utxo_info\"            (json,required) the info of utxo\n"
                "{\n"
                "    "
                "}"
                "6.\"memo\":                (string, optional)\n"
                "\nResult:\n"
                "\"txid\"                   (string) The transaction id.\n"
                "\nExamples:\n" +
                HelpExampleCli("submitutxospendtx",
                               "\"wLKf2NqwtHk3BfzK5wMDfbKYN1SC3weyR4\" \"wNDue1jHcgRSioSDL4o1AzXz3D72gCMkP6\" "
                               "\"GVC:1000000:sawi\" \"GVC:10000:sawi\" \"{}\" \"Hello, GreenVenturesChain!\"") +
                "\nAs json rpc call\n" +
                HelpExampleRpc("submitutxospendtx",
                               "\"wLKf2NqwtHk3BfzK5wMDfbKYN1SC3weyR4\", \"wNDue1jHcgRSioSDL4o1AzXz3D72gCMkP6\", "
                               "\"GVC:1000000:sawi\", \"GVC:10000:sawi\", \"{}\", \"Hello, GreenVenturesChain!\""));

    // EnsureWalletIsUnlocked();

    // CUserID sendUserId = RPC_PARAM::GetUserId(params[0], true);
    // uint256 prior_utxo_txid = uint256S(params[1].get_str()) ;
    // string prior_utxo_secret = params[2].get_str() ;
    // ComboMoney cmFee   = RPC_PARAM::GetFee(params, 3, UCOIN_TRANSFER_TX);
    // Object utxoInfo = params[4].get_obj() ;
    // string memo = "";
    // if(params.size()>5){
    //     memo = params[5].get_str();
    // }


    // Value lock_durationValue ;
    // Value secret_hashValue ;
    // Value collect_timeoutValue ;

    // uint256 secret_hash = uint256();
    // uint64_t collect_timeout = 0 ;
    // uint64_t lock_duration = 0;

    // if(JSON::GetObjectFieldValue(utxoInfo, "lock_duration",lock_durationValue)){
    //     lock_duration = lock_durationValue.get_int();
    // }


    // if(JSON::GetObjectFieldValue(utxoInfo, "secret_hash",secret_hashValue)){
    //     secret_hash = uint256S(secret_hashValue.get_str()) ;
    // }


    // if(JSON::GetObjectFieldValue(utxoInfo, "collect_timeout", collect_timeoutValue)){
    //     collect_timeout = collect_timeoutValue.get_int() ;
    // }


    // CAccount account = RPC_PARAM::GetUserAccount(*pCdMan->pAccountCache, sendUserId);
    // RPC_PARAM::CheckAccountBalance(account, cmFee.symbol, SUB_FREE, cmFee.GetSawiAmount());
    // if(!account.nickid.IsEmpty()){
    //     throw JSONRPCError(RPC_WALLET_ERROR,"the account have nickid already!");
    // }


    // HTLCCondition hc ;
    // hc.secret_hash = secret_hash;
    // hc.collect_timeout = collect_timeout ;
    // if(hc.secret_hash != uint256() || hc.collect_timeout != 0)
    //     hc.is_null = false ;


    // UTXOEntity entity ;
    // entity.lock_duration = lock_duration ;
    // entity.htlc_cond = hc ;
    // entity.is_null     = false ;

    // auto pBaseTx = std::make_shared<CCoinUtxoTransferTx>(sendUserId,chainActive.Height(),
    //                                              cmFee.symbol, cmFee.GetSawiAmount(),entity, memo);
    // pBaseTx->prior_utxo_secret = prior_utxo_secret ;
    // pBaseTx->prior_utxo_txid   = prior_utxo_txid ;

    // return SubmitTx(account.keyid, *pBaseTx);
    Object obj;
    return obj;

}


Value submitcreateutxotx(const Array& params, bool fHelp ){

    if (fHelp || (params.size() != 5 && params.size() != 6))
        throw runtime_error(
                "submitcreateutxotx \"from\" \"to\" \"symbol:coin:unit\" \"symbol:fee:unit\" \"utxo_info\" [\"memo\"]\n"
                "\nSend coins to a given address.\n" +
                HelpRequiringPassphrase() +
                "\nArguments:\n"
                "1.\"from\":                (string, required) The address where coins are sent from\n"
                "2.\"to\":                  (string, required) The address where coins are received\n"
                "3.\"symbol:coin:unit\":    (symbol:amount:unit, required) transferred coins\n"
                "4.\"symbol:fee:unit\":     (symbol:amount:unit, required) fee paid to miner, default is GVC:10000:sawi\n"
                "5.\"utxo_info\"            (json,required) the info of utxo\n"
                "{\n"
                "    "
                "}"
                "6.\"memo\":                (string, optional)\n"
                "\nResult:\n"
                "\"txid\"                   (string) The transaction id.\n"
                "\nExamples:\n" +
                HelpExampleCli("submitcreateutxotx",
                               "\"wLKf2NqwtHk3BfzK5wMDfbKYN1SC3weyR4\" \"wNDue1jHcgRSioSDL4o1AzXz3D72gCMkP6\" "
                               "\"GVC:1000000:sawi\" \"GVC:10000:sawi\" \"{}\" \"Hello, GreenVenturesChain!\"") +
                "\nAs json rpc call\n" +
                HelpExampleRpc("submitcreateutxotx",
                               "\"wLKf2NqwtHk3BfzK5wMDfbKYN1SC3weyR4\", \"wNDue1jHcgRSioSDL4o1AzXz3D72gCMkP6\", "
                               "\"GVC:1000000:sawi\", \"GVC:10000:sawi\", \"{}\", \"Hello, GreenVenturesChain!\""));

    // EnsureWalletIsUnlocked();

    // CUserID sendUserId = RPC_PARAM::GetUserId(params[0], true);
    // CUserID recvUserId = RPC_PARAM::GetUserId(params[1]);
    // ComboMoney cmCoin  = RPC_PARAM::GetComboMoney(params[2], SYMB::GVC);
    // ComboMoney cmFee   = RPC_PARAM::GetFee(params, 3, UCOIN_TRANSFER_TX);
    // Object utxoInfo = params[4].get_obj() ;
    // string memo = "";
    // if(params.size()>5){
    //     memo = params[5].get_str();
    // }


    // Value lock_durationValue ;
    // Value secret_hashValue ;
    // Value collect_timeoutValue ;

    // uint256 secret_hash = uint256();
    // uint64_t collect_timeout = 0 ;
    // uint64_t lock_duration = 0;

    // if(JSON::GetObjectFieldValue(utxoInfo, "lock_duration",lock_durationValue)){
    //     lock_duration = lock_durationValue.get_int();
    // }


    // if(JSON::GetObjectFieldValue(utxoInfo, "secret_hash",secret_hashValue)){
    //     secret_hash = uint256S(secret_hashValue.get_str()) ;
    // }


    // if(JSON::GetObjectFieldValue(utxoInfo, "collect_timeout", collect_timeoutValue)){
    //     collect_timeout = collect_timeoutValue.get_int() ;
    // }


    // CAccount account = RPC_PARAM::GetUserAccount(*pCdMan->pAccountCache, sendUserId);
    // RPC_PARAM::CheckAccountBalance(account, cmFee.symbol, SUB_FREE, cmFee.GetSawiAmount());
    // if(!account.nickid.IsEmpty()){
    //     throw JSONRPCError(RPC_WALLET_ERROR,"the account have nickid already!");
    // }


    // HTLCCondition hc ;
    // hc.secret_hash = secret_hash;
    // hc.collect_timeout = collect_timeout ;
    // if(hc.secret_hash != uint256() || hc.collect_timeout != 0)
    //     hc.is_null = false ;


    // UTXOEntity entity ;
    // entity.coin_symbol = cmCoin.symbol ;
    // entity.coin_amount = cmCoin.GetSawiAmount() ;
    // entity.to_uid      = recvUserId ;
    // entity.lock_duration = lock_duration ;
    // entity.htlc_cond = hc ;
    // entity.is_null     = false ;

    // auto pBaseTx = std::make_shared<CCoinUtxoTransferTx>(sendUserId,chainActive.Height(),
    //         cmFee.symbol, cmFee.GetSawiAmount(),entity, memo);


    // return SubmitTx(account.keyid, *pBaseTx);
    Object obj;
    return obj;
}



Value submitcontractdeploytx(const Array& params, bool fHelp) {
    if (fHelp || params.size() < 3 || params.size() > 5) {
        throw runtime_error("submitcontractdeploytx \"addr\" \"filepath\" \"fee\" [\"height\"] [\"contract_memo\"]\n"
            "\ncreate a transaction of registering a contract\n"
            "\nArguments:\n"
            "1.\"addr\":            (string, required) contract owner address from this wallet\n"
            "2.\"filepath\":        (string, required) the file path of the app script\n"
            "3.\"fee\":             (numeric, required) pay to miner (the larger the size of script, the bigger fees are required)\n"
            "4.\"height\":          (numeric, optional) valid height, when not specified, the tip block height in chainActive will be used\n"
            "5.\"contract_memo\":   (string, optional) contract memo\n"
            "\nResult:\n"
            "\"txid\":              (string)\n"
            "\nExamples:\n"
            + HelpExampleCli("submitcontractdeploytx",
                "\"WiZx6rrsBn9sHjwpvdwtMNNX2o31s3DEHH\" \"/tmp/lua/myapp.lua\" 100000000 10000 \"Hello, GreenVenturesChain!\"") +
                "\nAs json rpc call\n"
            + HelpExampleRpc("submitcontractdeploytx",
                "WiZx6rrsBn9sHjwpvdwtMNNX2o31s3DEHH, \"/tmp/lua/myapp.lua\", 100000000, 10000, \"Hello, GreenVenturesChain!\""));
    }

    RPCTypeCheck(params, list_of(str_type)(str_type)(str_type)(int_type)(str_type));

    EnsureWalletIsUnlocked();

    const CUserID& txUid  = RPC_PARAM::GetUserId(params[0]);
    string contractScript = RPC_PARAM::GetLuaContractScript(params[1]);
    ComboMoney fee           = RPC_PARAM::GetFee(params, 2, LCONTRACT_DEPLOY_TX);
    int32_t validHegiht   = params.size() > 3 ? params[3].get_int() : chainActive.Height();
    string memo           = params.size() > 4 ? params[4].get_str() : "";

    if (memo.size() > MAX_CONTRACT_MEMO_SIZE)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Contract memo is too large");

    if (!txUid.is<CRegID>())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Regid does not exist or immature");

    CAccount account = RPC_PARAM::GetUserAccount(*pCdMan->pAccountCache, txUid);
    RPC_PARAM::CheckAccountBalance(account, SYMB::GVC, SUB_FREE, fee.GetSawiAmount());

    CLuaContractDeployTx tx;
    tx.txUid        = txUid;
    tx.contract     = CLuaContract(contractScript, memo);
    tx.llFees       = fee.GetSawiAmount();
    tx.nRunStep     = tx.contract.GetContractSize();
    tx.valid_height = validHegiht;

    return SubmitTx(account.keyid, tx);
}

Value submitcontractcalltx(const Array& params, bool fHelp) {
    if (fHelp || params.size() < 5 || params.size() > 6) {
        throw runtime_error(
            "submitcontractcalltx \"sender_addr\" \"contract_regid\" \"arguments\" \"amount\" \"fee\" [\"height\"]\n"
            "\ncreate contract invocation transaction\n"
            "\nArguments:\n"
            "1.\"sender_addr\":     (string, required) tx sender's base58 addr\n"
            "2.\"contract_regid\":  (string, required) contract regid\n"
            "3.\"arguments\":       (string, required) contract arguments (Hex encode required)\n"
            "4.\"amount\":          (numeric, required) amount of GVC to be sent to the contract account\n"
            "5.\"fee\":             (numeric, required) pay to miner\n"
            "6.\"height\":          (numberic, optional) valid height\n"
            "\nResult:\n"
            "\"txid\":              (string)\n"
            "\nExamples:\n" +
            HelpExampleCli("submitcontractcalltx",
                           "\"wQWKaN4n7cr1HLqXY3eX65rdQMAL5R34k6\" \"100-1\" \"01020304\" 10000 10000 100") +
            "\nAs json rpc call\n" +
            HelpExampleRpc("submitcontractcalltx",
                           "\"wQWKaN4n7cr1HLqXY3eX65rdQMAL5R34k6\", \"100-1\", \"01020304\", 10000, 10000, 100"));
    }

    RPCTypeCheck(params, list_of(str_type)(str_type)(str_type)(int_type)(str_type)(int_type));

    EnsureWalletIsUnlocked();

    const CUserID& txUid  = RPC_PARAM::GetUserId(params[0], true);
    const CUserID& appUid = RPC_PARAM::GetUserId(params[1]);

    CRegID appRegId;
    if (!pCdMan->pAccountCache->GetRegId(appUid, appRegId)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid contract regid");
    }

    if (!pCdMan->pContractCache->HaveContract(appRegId)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Failed to acquire contract");
    }

    string arguments = ParseHexStr(params[2].get_str());
    if (arguments.size() >= MAX_CONTRACT_ARGUMENT_SIZE) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Arguments's size is out of range");
    }

    int64_t amount      = AmountToRawValue(params[3]);
    ComboMoney fee         = RPC_PARAM::GetFee(params, 4, LCONTRACT_INVOKE_TX);
    int32_t validHegiht = (params.size() > 5) ? params[5].get_int() : chainActive.Height();

    CAccount account = RPC_PARAM::GetUserAccount(*pCdMan->pAccountCache, txUid);
    RPC_PARAM::CheckAccountBalance(account, SYMB::GVC, SUB_FREE, amount);
    RPC_PARAM::CheckAccountBalance(account, SYMB::GVC, SUB_FREE, fee.GetSawiAmount());

    CLuaContractInvokeTx tx;
    tx.nTxType      = LCONTRACT_INVOKE_TX;
    tx.txUid        = txUid;
    tx.app_uid      = appUid;
    tx.coin_amount  = amount;
    tx.llFees       = fee.GetSawiAmount();
    tx.arguments    = arguments;
    tx.valid_height = validHegiht;

    return SubmitTx(account.keyid, tx);
}

Value submitdelegatevotetx(const Array& params, bool fHelp) {
    if (fHelp || params.size() < 3 || params.size() > 4) {
        throw runtime_error(
            "submitdelegatevotetx \"sendaddr\" \"votes\" \"fee\" [\"height\"] \n"
            "\ncreate a delegate vote transaction\n"
            "\nArguments:\n"
            "1.\"sendaddr\": (string required) The address from which votes are sent to other "
            "delegate addresses\n"
            "2. \"votes\"    (string, required) A json array of votes to delegate candidates\n"
            " [\n"
            "   {\n"
            "      \"delegate\":\"address\", (string, required) The delegate address where votes "
            "are received\n"
            "      \"votes\": n (numeric, required) votes, increase votes when positive or reduce "
            "votes when negative\n"
            "   }\n"
            "       ,...\n"
            " ]\n"
            "3.\"fee\": (comboMoney string or numberic, required) pay fee to miner\n"
            "4.\"height\": (numeric optional) valid height. When not supplied, the tip block "
            "height in chainActive will be used.\n"
            "\nResult:\n"
            "\"txid\": (string)\n"
            "\nExamples:\n" +
            HelpExampleCli("submitdelegatevotetx",
                           "\"wQquTWgzNzLtjUV4Du57p9YAEGdKvgXs9t\" "
                           "\"[{\\\"delegate\\\":\\\"wNDue1jHcgRSioSDL4o1AzXz3D72gCMkP6\\\", "
                           "\\\"votes\\\":100000000}]\" 10000") +
            "\nAs json rpc call\n" +
            HelpExampleRpc("submitdelegatevotetx",
                           "\"wQquTWgzNzLtjUV4Du57p9YAEGdKvgXs9t\", "
                           "[{\"delegate\":\"wNDue1jHcgRSioSDL4o1AzXz3D72gCMkP6\", "
                           "\"votes\":100000000}], 10000"));
    }

    EnsureWalletIsUnlocked();

    const CUserID& txUid = RPC_PARAM::GetUserId(params[0], true);
    ComboMoney fee          = RPC_PARAM::GetFee(params, 2, DELEGATE_VOTE_TX);
    int32_t validHegiht  = params.size() > 3 ? params[3].get_int() : chainActive.Height();

    CAccount account = RPC_PARAM::GetUserAccount(*pCdMan->pAccountCache, txUid);
    RPC_PARAM::CheckAccountBalance(account, SYMB::GVC, SUB_FREE, fee.GetSawiAmount());

    CDelegateVoteTx delegateVoteTx;
    delegateVoteTx.txUid        = txUid;
    delegateVoteTx.llFees       = fee.GetSawiAmount();
    delegateVoteTx.valid_height = validHegiht;

    Array arrVotes = params[1].get_array();
    for (auto objVote : arrVotes) {
        const Value& delegateAddr  = find_value(objVote.get_obj(), "delegate");
        const Value& delegateVotes = find_value(objVote.get_obj(), "votes");
        if (delegateAddr.type() == null_type || delegateVotes == null_type) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Vote fund address error or fund value error");
        }
        auto delegateUid = RPC_PARAM::ParseUserIdByAddr(delegateAddr);
        CAccount delegateAcct;
        if (!pCdMan->pAccountCache->GetAccount(delegateUid, delegateAcct)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Delegate address does not exist");
        }
        if (!delegateAcct.HaveOwnerPubKey()) {
            throw JSONRPCError(RPC_WALLET_ERROR, "Delegate address is unregistered");
        }

        VoteType voteType    = (delegateVotes.get_int64() > 0) ? VoteType::ADD_BCOIN : VoteType::MINUS_BCOIN;
        CUserID candidateUid = CUserID(delegateAcct.regid);
        uint64_t bcoins      = (uint64_t)abs(delegateVotes.get_int64());

        CCandidateVote candidateVote(voteType, candidateUid, bcoins);
        delegateVoteTx.candidateVotes.push_back(candidateVote);
    }

    return SubmitTx(account.keyid, delegateVoteTx);
}


Value submitucontractdeploytx(const Array& params, bool fHelp) {
    if (fHelp || params.size() < 3 || params.size() > 5) {
        throw runtime_error(
            "submitucontractdeploytx \"addr\" \"filepath\" \"symbol:fee:unit\" [\"height\"] [\"contract_memo\"]\n"
            "\ncreate a transaction of registering a universal contract\n"
            "\nArguments:\n"
            "1.\"addr\":            (string, required) contract owner address from this wallet\n"
            "2.\"filepath\":        (string, required) the file path of the app script\n"
            "3.\"symbol:fee:unit\": (symbol:amount:unit, required) fee paid to miner, default is GVC:100000000:sawi\n"
            "4.\"height\":          (numeric, optional) valid height, when not specified, the tip block height in "
            "chainActive will be used\n"
            "5.\"contract_memo\":   (string, optional) contract memo\n"
            "\nResult:\n"
            "\"txid\":              (string)\n"
            "\nExamples:\n" +
            HelpExampleCli("submitucontractdeploytx",
                           "\"WiZx6rrsBn9sHjwpvdwtMNNX2o31s3DEHH\" \"/tmp/lua/myapp.lua\" \"GVC:100000000:sawi\" "
                           "10000 \"Hello, GreenVenturesChain!\"") +
            "\nAs json rpc call\n" +
            HelpExampleRpc("submitucontractdeploytx",
                           "WiZx6rrsBn9sHjwpvdwtMNNX2o31s3DEHH, \"/tmp/lua/myapp.lua\", \"GVC:100000000:sawi\", "
                           "10000, \"Hello, GreenVenturesChain!\""));
    }

    RPCTypeCheck(params, list_of(str_type)(str_type)(str_type)(int_type)(str_type));

    EnsureWalletIsUnlocked();

    const CUserID& txUid  = RPC_PARAM::GetUserId(params[0]);
    string contractScript = RPC_PARAM::GetLuaContractScript(params[1]); // TODO: support universal contract script
    ComboMoney cmFee      = RPC_PARAM::GetFee(params, 2, UCONTRACT_DEPLOY_TX);
    int32_t validHegiht   = params.size() > 3 ? params[3].get_int() : chainActive.Height();
    string memo           = params.size() > 4 ? params[4].get_str() : "";

    if (!txUid.is<CRegID>())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Regid does not exist or immature");

    if (memo.size() > MAX_CONTRACT_MEMO_SIZE)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Contract memo is too large");

    CAccount account = RPC_PARAM::GetUserAccount(*pCdMan->pAccountCache, txUid);
    RPC_PARAM::CheckAccountBalance(account, cmFee.symbol, SUB_FREE, cmFee.GetSawiAmount());

    CUniversalContractDeployTx tx;
    tx.txUid        = txUid;
    tx.contract     = CUniversalContract(contractScript, memo);
    tx.fee_symbol   = cmFee.symbol;
    tx.llFees       = cmFee.GetSawiAmount();
    tx.nRunStep     = tx.contract.GetContractSize();
    tx.valid_height = validHegiht;

    return SubmitTx(account.keyid, tx);
}

Value submitucontractcalltx(const Array& params, bool fHelp) {
    if (fHelp || params.size() < 5 || params.size() > 6) {
        throw runtime_error(
            "submitucontractcalltx \"sender_addr\" \"contract_regid\" \"arguments\" \"symbol:coin:unit\" "
            "\"symbol:fee:unit\" [\"height\"]\n"
            "\ncreate contract invocation transaction\n"
            "\nArguments:\n"
            "1.\"sender_addr\":     (string, required) tx sender's base58 addr\n"
            "2.\"contract_regid\":  (string, required) contract regid\n"
            "3.\"arguments\":       (string, required) contract arguments (Hex encode required)\n"
            "4.\"symbol:coin:unit\":(symbol:amount:unit, required) transferred coins\n"
            "5.\"symbol:fee:unit\": (symbol:amount:unit, required) fee paid to miner, default is GVC:10000:sawi\n"
            "6.\"height\":          (numberic, optional) valid height\n"
            "\nResult:\n"
            "\"txid\":              (string)\n"
            "\nExamples:\n" +
            HelpExampleCli("submitucontractcalltx",
                           "\"wQWKaN4n7cr1HLqXY3eX65rdQMAL5R34k6\" \"100-1\" \"01020304\" \"GVC:10000:sawi\" "
                           "\"GVC:10000:sawi\" 100") +
            "\nAs json rpc call\n" +
            HelpExampleRpc("submitucontractcalltx",
                           "\"wQWKaN4n7cr1HLqXY3eX65rdQMAL5R34k6\", \"100-1\", \"01020304\", \"GVC:10000:sawi\", "
                           "\"GVC:10000:sawi\", 100"));
    }

    RPCTypeCheck(params, list_of(str_type)(str_type)(str_type)(str_type)(str_type)(int_type));

    EnsureWalletIsUnlocked();

    const CUserID& txUid  = RPC_PARAM::GetUserId(params[0], true);
    const CUserID& appUid = RPC_PARAM::GetUserId(params[1]);

    CRegID appRegId;
    if (!pCdMan->pAccountCache->GetRegId(appUid, appRegId)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid contract regid");
    }

    if (!pCdMan->pContractCache->HaveContract(appRegId)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Failed to acquire contract");
    }

    string arguments = ParseHexStr(params[2].get_str());
    if (arguments.size() >= MAX_CONTRACT_ARGUMENT_SIZE) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Arguments's size is out of range");
    }

    ComboMoney cmCoin   = RPC_PARAM::GetComboMoney(params[3], SYMB::GVC);
    ComboMoney cmFee    = RPC_PARAM::GetFee(params, 4, UCONTRACT_INVOKE_TX);
    int32_t validHegiht = (params.size() > 5) ? params[5].get_int() : chainActive.Height();

    CAccount account = RPC_PARAM::GetUserAccount(*pCdMan->pAccountCache, txUid);
    RPC_PARAM::CheckAccountBalance(account, cmCoin.symbol, SUB_FREE, cmCoin.GetSawiAmount());
    RPC_PARAM::CheckAccountBalance(account, cmFee.symbol, SUB_FREE, cmFee.GetSawiAmount());

    CUniversalContractInvokeTx tx;
    tx.nTxType      = UCONTRACT_INVOKE_TX;
    tx.txUid        = txUid;
    tx.app_uid      = appUid;
    tx.coin_symbol  = cmCoin.symbol;
    tx.coin_amount  = cmCoin.GetSawiAmount();
    tx.fee_symbol   = cmFee.symbol;
    tx.llFees       = cmFee.GetSawiAmount();
    tx.arguments    = arguments;
    tx.valid_height = validHegiht;

    return SubmitTx(account.keyid, tx);
}

Value listaddr(const Array& params, bool fHelp) {
    if (fHelp || params.size() != 0) {
        throw runtime_error(
            "listaddr\n"
            "\nreturn Array containing address, balance, haveminerkey, regid information.\n"
            "\nArguments:\n"
            "\nResult:\n"
            "\nExamples:\n" +
            HelpExampleCli("listaddr", "") + "\nAs json rpc call\n" + HelpExampleRpc("listaddr", ""));
    }

    Array retArray;
    assert(pWalletMain != nullptr);
    {
        set<CKeyID> setKeyId;
        pWalletMain->GetKeys(setKeyId);
        if (setKeyId.size() == 0) {
            return retArray;
        }

        for (const auto &keyid : setKeyId) {
            CUserID userId(keyid);
            CAccount account;
            pCdMan->pAccountCache->GetAccount(userId, account);
            CKeyCombi keyCombi;
            pWalletMain->GetKeyCombi(keyid, keyCombi);

            Object obj;
            obj.push_back(Pair("addr",          keyid.ToAddress()));
            obj.push_back(Pair("regid",         account.regid.ToString()));
            obj.push_back(Pair("regid_mature",  account.regid.IsMature(chainActive.Height())));
            obj.push_back(Pair("received_votes",account.received_votes));

            Object tokenMapObj;
            for (auto tokenPair : account.tokens) {
                Object tokenObj;
                const CAccountToken& token = tokenPair.second;
                tokenObj.push_back(Pair("free_amount",      token.free_amount));
                tokenObj.push_back(Pair("staked_amount",    token.staked_amount));
                tokenObj.push_back(Pair("frozen_amount",    token.frozen_amount));
                tokenObj.push_back(Pair("voted_amount",     token.voted_amount));

                tokenMapObj.push_back(Pair(tokenPair.first, tokenObj));
            }

            obj.push_back(Pair("tokens",        tokenMapObj));
            obj.push_back(Pair("hasminerkey",   keyCombi.HaveMinerKey()));

            retArray.push_back(obj);
        }
    }

    return retArray;
}

Value listtx(const Array& params, bool fHelp) {
if (fHelp || params.size() > 2) {
        throw runtime_error("listtx\n"
                "\nget all confirmed transactions and all unconfirmed transactions from wallet.\n"
                "\nArguments:\n"
                "1. count          (numeric, optional, default=10) The number of transactions to return\n"
                "2. from           (numeric, optional, default=0) The number of transactions to skip\n"
                "\nResult:\n"
                "\nExamples:\n"
                "\nList the most recent 10 transactions in the system\n"
                + HelpExampleCli("listtx", "") +
                "\nList transactions 100 to 120\n"
                + HelpExampleCli("listtx", "20 100")
            );
    }

    Object retObj;
    int32_t nDefCount = 10;
    int32_t nFrom = 0;
    if (params.size() > 0) {
        nDefCount = params[0].get_int();
    }

    if (params.size() > 1) {
        nFrom = params[1].get_int();
    }
    assert(pWalletMain != nullptr);

    Array confirmedTxArray;
    int32_t nCount = 0;
    map<int32_t, uint256, std::greater<int32_t> > blockInfoMap;
    for (auto const &wtx : pWalletMain->mapInBlockTx) {
        auto it = mapBlockIndex.find(wtx.first);
        if (it != mapBlockIndex.end())
            blockInfoMap.insert(make_pair(it->second->height, wtx.first));
    }
    bool bUpLimited = false;
    for (auto const &blockInfo : blockInfoMap) {
        CAccountTx accountTx = pWalletMain->mapInBlockTx[blockInfo.second];
        for (auto const & item : accountTx.mapAccountTx) {
            if (nFrom-- > 0)
                continue;
            if (++nCount > nDefCount) {
                bUpLimited = true;
                break;
            }
            confirmedTxArray.push_back(item.first.GetHex());
        }
        if (bUpLimited) {
            break;
        }
    }
    retObj.push_back(Pair("confirmed_tx", confirmedTxArray));

    Array unconfirmedTxArray;
    for (auto const &tx : pWalletMain->unconfirmedTx) {
        unconfirmedTxArray.push_back(tx.first.GetHex());
    }

    retObj.push_back(Pair("unconfirmed_tx", unconfirmedTxArray));

    return retObj;
}

Value getaccountinfo(const Array& params, bool fHelp) {
    if (fHelp || params.size() != 1) {
        throw runtime_error(
            "getaccountinfo \"addr\"\n"
            "\nget account information\n"
            "\nArguments:\n"
            "1.\"addr\": (string, required) account base58 address"
            "Returns account details.\n"
            "\nResult:\n"
            "{\n"
            "  \"address\": \"xxxxx\",       (string) the address\n"
            "  \"keyid\": \"xxxxx\",         (string) the keyid referred to the address\n"
            "  \"nickid\": \"xxxxx\",        (string) the nickid referred to the address\n"
            "  \"regid_mature\": true|false,   (bool) the nickid is mature or not\n"
            "  \"regid\": \"xxxxx\",         (string) the regid referred to the address\n"
            "  \"regid_mature\": true|false,   (bool) the regid is mature or not\n"
            "  \"owner_pubkey\": \"xxxxx\",  (string) the public key referred to the address\n"
            "  \"miner_pubkey\": \"xxxxx\",  (string) the miner publick key referred to the address\n"
            "  \"tokens\": {},             (object) tokens object all the address owned\n"
            "  \"received_votes\": xxxxx,  (numeric) received votes in total\n"
            "  \"vote_list\": [],       (array) votes to others\n"
            "  \"position\": \"xxxxx\",      (string) in wallet if the address never involved in transaction, otherwise, in block\n"
            "  \"cdp_list\": [],           (array) cdp list\n"
            "}\n"
            "\nExamples:\n" +
            HelpExampleCli("getaccountinfo", "\"WT52jPi8DhHUC85MPYK8y8Ajs8J7CshgaB\"") +
            "\nAs json rpc call\n" +
            HelpExampleRpc("getaccountinfo", "\"WT52jPi8DhHUC85MPYK8y8Ajs8J7CshgaB\""));
    }

    RPCTypeCheck(params, list_of(str_type));
    CKeyID keyid = RPC_PARAM::GetKeyId(params[0]);
    CUserID userId = keyid;
    Object obj;
    CAccount account;
    if (pCdMan->pAccountCache->GetAccount(userId, account)) {
        if (!account.owner_pubkey.IsValid()) {
            CPubKey pubKey;
            CPubKey minerPubKey;
            if (pWalletMain->GetPubKey(keyid, pubKey)) {
                pWalletMain->GetPubKey(keyid, minerPubKey, true);
                account.owner_pubkey = pubKey;
                account.keyid        = pubKey.GetKeyId();
                if (pubKey != minerPubKey && !account.miner_pubkey.IsValid()) {
                    account.miner_pubkey = minerPubKey;
                }
            }
        }
        obj = account.ToJsonObj();
        obj.push_back(Pair("registered", true));

        // TODO: multi stable coin
        uint64_t bcoinMedianPrice = pCdMan->pPriceFeedCache->GetMedianPrice(CoinPricePair(SYMB::GVC, SYMB::USD));
        Array cdps;
        vector<CUserCDP> userCdps;
        if (pCdMan->pCdpCache->GetCDPList(account.regid, userCdps)) {
            for (auto& cdp : userCdps) {
                cdps.push_back(cdp.ToJson(bcoinMedianPrice));
            }
        }
        obj.push_back(Pair("cdp_list", cdps));

    }  else {
         obj.push_back(Pair("registered", false));
    }

    CPubKey pubKey;
    CPubKey minerPubKey;
    if (pWalletMain->GetPubKey(keyid, pubKey)) {
        pWalletMain->GetPubKey(keyid, minerPubKey, true);
        account.owner_pubkey = pubKey;
        account.keyid        = pubKey.GetKeyId();
        if (minerPubKey != pubKey)
            account.miner_pubkey = minerPubKey;

        obj = account.ToJsonObj();
        obj.push_back(Pair("in_wallet", true));

    } else {
        obj.push_back(Pair("in_wallet", false));
    }

    return obj;
}

static Value TestDisconnectBlock(int32_t number) {
    CBlock block;
    Object obj;

    CValidationState state;
    if (number >= chainActive.Height()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid number");
    }
    if (number > 0) {
        do {
            CBlockIndex * pTipIndex = chainActive.Tip();
            if (!DisconnectBlockFromTip(state))
                return false;
            chainMostWork.SetTip(pTipIndex->pprev);
            if (!EraseBlockIndexFromSet(pTipIndex))
                return false;
            if (!pCdMan->pBlockIndexDb->EraseBlockIndex(pTipIndex->GetBlockHash()))
                return false;
            mapBlockIndex.erase(pTipIndex->GetBlockHash());
        } while (--number);
    }

    obj.push_back(
        Pair("tip", strprintf("hash:%s hight:%s", chainActive.Tip()->GetBlockHash().ToString(), chainActive.Height())));
    return obj;
}

Value disconnectblock(const Array& params, bool fHelp) {
    if (fHelp || params.size() != 1) {
        throw runtime_error("disconnectblock \"numbers\"\n"
                "\ndisconnect block\n"
                "\nArguments:\n"
                "1. \"numbers \"  (numeric, required) the block numbers.\n"
                "\nResult:\n"
                "\"disconnect result\"  (bool) \n"
                "\nExamples:\n"
                + HelpExampleCli("disconnectblock", "\"1\"")
                + "\nAs json rpc call\n"
                + HelpExampleRpc("disconnectblock", "\"1\""));
    }
    int32_t number = params[0].get_int();

    Value te = TestDisconnectBlock(number);

    return te;
}

Value listcontracts(const Array& params, bool fHelp) {
    if (fHelp || params.size() != 1) {
        throw runtime_error(
            "listcontracts \"show detail\"\n"
            "\nget the list of all contracts\n"
            "\nArguments:\n"
            "1. show detail  (boolean, required) show contract in detail if true.\n"
            "\nReturn an object contains all contracts\n"
            "\nResult:\n"
            "\nExamples:\n" +
            HelpExampleCli("listcontracts", "true") + "\nAs json rpc call\n" + HelpExampleRpc("listcontracts", "true"));
    }

    bool showDetail = params[0].get_bool();

    map<CRegIDKey, CUniversalContract> contracts;
    if (!pCdMan->pContractCache->GetContracts(contracts)) {
        throw JSONRPCError(RPC_DATABASE_ERROR, "Failed to acquire contracts from db.");
    }

    Object obj;
    Array contractArray;
    for (const auto &item : contracts) {
        Object contractObject;
        const CUniversalContract &contract = item.second;
        contractObject.push_back(Pair("contract_regid", item.first.regid.ToString()));
        contractObject.push_back(Pair("memo",           contract.memo));

        if (showDetail) {
            contractObject.push_back(Pair("vm_type",    contract.vm_type));
            contractObject.push_back(Pair("upgradable", contract.upgradable));
            contractObject.push_back(Pair("code",       HexStr(contract.code)));
            contractObject.push_back(Pair("abi",        contract.abi));
        }

        contractArray.push_back(contractObject);
    }

    obj.push_back(Pair("count",     contracts.size()));
    obj.push_back(Pair("contracts", contractArray));

    return obj;
}

Value getcontractinfo(const Array& params, bool fHelp) {
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "getcontractinfo \"contract regid\"\n"
            "\nget contract information.\n"
            "\nArguments:\n"
            "1. \"contract regid\"    (string, required) the contract regid.\n"
            "\nReturn an object contains contract information\n"
            "\nExamples:\n" +
            HelpExampleCli("getcontractinfo", "1-1") + "\nAs json rpc call\n" +
            HelpExampleRpc("getcontractinfo", "1-1"));

    CRegID regid(params[0].get_str());
    if (regid.IsEmpty() || !pCdMan->pContractCache->HaveContract(regid)) {
        throw JSONRPCError(RPC_INVALID_PARAMS, "Invalid contract regid.");
    }

    CUniversalContract contract;
    if (!pCdMan->pContractCache->GetContract(regid, contract)) {
        throw JSONRPCError(RPC_DATABASE_ERROR, "Failed to acquire contract from db.");
    }

    Object obj;
    obj.push_back(Pair("contract_regid",    regid.ToString()));
    obj.push_back(Pair("vm_type",           contract.vm_type));
    obj.push_back(Pair("upgradable",        contract.upgradable));
    obj.push_back(Pair("code",              HexStr(contract.code)));
    obj.push_back(Pair("memo",              contract.memo));
    obj.push_back(Pair("abi",               contract.abi));

    return obj;
}

Value listtxcache(const Array& params, bool fHelp) {
    if (fHelp || params.size() != 0) {
        throw runtime_error("listtxcache\n"
                "\nget all transactions in cache\n"
                "\nArguments:\n"
                "\nResult:\n"
                "\"txcache\"  (string)\n"
                "\nExamples:\n" + HelpExampleCli("listtxcache", "")+ HelpExampleRpc("listtxcache", ""));
    }

    return pCdMan->pTxCache->ToJsonObj();
}

Value reloadtxcache(const Array& params, bool fHelp) {
    if (fHelp || params.size() != 0) {
        throw runtime_error("reloadtxcache \n"
            "\nreload transactions catch\n"
            "\nArguments:\n"
            "\nResult:\n"
            "\nExamples:\n"
            + HelpExampleCli("reloadtxcache", "")
            + HelpExampleRpc("reloadtxcache", ""));
    }
    pCdMan->pTxCache->Clear();
    CBlockIndex *pIndex = chainActive.Tip();
    if (chainActive.Height() - SysCfg().GetTxCacheHeight() >= 0) {
        pIndex = chainActive[(chainActive.Height() - SysCfg().GetTxCacheHeight())];
    } else {
        pIndex = chainActive.Genesis();
    }

    CBlock block;
    do {
        if (!ReadBlockFromDisk(pIndex, block))
            return ERRORMSG("reloadtxcache() : *** ReadBlockFromDisk failed at %d, hash=%s",
                pIndex->height, pIndex->GetBlockHash().ToString());

        pCdMan->pTxCache->AddBlockTx(block);
        pIndex = chainActive.Next(pIndex);
    } while (nullptr != pIndex);

    Object obj;
    obj.push_back(Pair("info", "reload tx cache succeed"));
    return obj;
}

Value getcontractdata(const Array& params, bool fHelp) {
    if (fHelp || (params.size() != 2 && params.size() != 3)) {
        throw runtime_error(
            "getcontractdata \"contract regid\" \"key\" [hexadecimal]\n"
            "\nget contract data with key\n"
            "\nArguments:\n"
            "1.\"contract regid\":      (string, required) contract regid\n"
            "2.\"key\":                 (string, required)\n"
            "3.\"hexadecimal format\":  (boolean, optional) in hexadecimal if true, otherwise in plaintext, default to "
            "false\n"
            "\nResult:\n"
            "\nExamples:\n" +
            HelpExampleCli("getcontractdata", "\"1304166-1\" \"key\" true") + "\nAs json rpc call\n" +
            HelpExampleRpc("getcontractdata", "\"1304166-1\", \"key\", true"));
    }

    CRegID regId(params[0].get_str());
    if (regId.IsEmpty()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid contract regid");
    }

    bool hexadecimal = params.size() > 2 ? params[2].get_bool() : false;
    string key;
    if (hexadecimal) {
        vector<uint8_t> hexKey = ParseHex(params[1].get_str());
        key                          = string(hexKey.begin(), hexKey.end());
    } else {
        key = params[1].get_str();
    }
    string value;
    if (!pCdMan->pContractCache->GetContractData(regId, key, value)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Failed to acquire contract data");
    }

    Object obj;
    obj.push_back(Pair("contract_regid",    regId.ToString()));
    obj.push_back(Pair("key",               hexadecimal ? HexStr(key) : key));
    obj.push_back(Pair("value",             hexadecimal ? HexStr(value) : value));

    return obj;
}

Value saveblocktofile(const Array& params, bool fHelp) {
    if (fHelp || params.size() != 2) {
        throw runtime_error(
            "saveblocktofile \"blockhash\" \"filepath\"\n"
            "\n save the given block info to the given file\n"
            "\nArguments:\n"
            "1.\"blockhash\": (string, required)\n"
            "2.\"filepath\": (string, required)\n"
            "\nResult:\n"
            "\nExamples:\n" +
            HelpExampleCli("saveblocktofile",
                           "\"c78d162b40625cc8b088fa88302e0e4f08aba0d1c92612e9dd14e77108cbc11a\" \"block.log\"") +
            "\nAs json rpc call\n" +
            HelpExampleRpc("saveblocktofile",
                           "\"c78d162b40625cc8b088fa88302e0e4f08aba0d1c92612e9dd14e77108cbc11a\", \"block.log\""));
    }
    string strblockhash = params[0].get_str();
    uint256 blockHash(uint256S(params[0].get_str()));
    if(0 == mapBlockIndex.count(blockHash)) {
        throw JSONRPCError(RPC_MISC_ERROR, "block hash is not exist!");
    }
    CBlockIndex *pIndex = mapBlockIndex[blockHash];
    CBlock blockInfo;
    if (!pIndex || !ReadBlockFromDisk(pIndex, blockInfo))
        throw runtime_error(_("Failed to read block"));
    assert(strblockhash == blockInfo.GetHash().ToString());
    string file = params[1].get_str();
    try {
        FILE* fp = fopen(file.c_str(), "wb+");
        CAutoFile fileout = CAutoFile(fp, SER_DISK, CLIENT_VERSION);
        if (!fileout)
            throw JSONRPCError(RPC_MISC_ERROR, "open file:" + strblockhash + "failed!");
        if(chainActive.Contains(pIndex))
            fileout << pIndex->height;
        fileout << blockInfo;
        fflush(fileout);
    } catch (std::exception &e) {
        throw JSONRPCError(RPC_MISC_ERROR, "save block to file error");
    }
    return "save succeed";
}

Value submittxraw(const Array& params, bool fHelp) {
    if (fHelp || params.size() != 1) {
        throw runtime_error(
            "submittxraw \"rawtx\" \n"
            "\nsubmit raw transaction (hex format)\n"
            "\nArguments:\n"
            "1.\"rawtx\":   (string, required) The raw transaction\n"
            "\nExamples:\n" +
            HelpExampleCli("submittxraw",
                           "\"0b01848908020001145e3550cfae2422dce90a778b0954409b1c6ccc3a045749434382dbea93000457494343c"
                           "d10004630440220458e2239348a9442d05503137ec84b84d69c7141b3618a88c50c16f76d9655ad02206dd20806"
                           "87cffad42f7293522568fc36850d4e3b81fa9ad860d1490cf0225cf8\"") +
            "\nAs json rpc call\n" +
            HelpExampleRpc("submittxraw",
                           "\"0b01848908020001145e3550cfae2422dce90a778b0954409b1c6ccc3a045749434382dbea93000457494343c"
                           "d10004630440220458e2239348a9442d05503137ec84b84d69c7141b3618a88c50c16f76d9655ad02206dd20806"
                           "87cffad42f7293522568fc36850d4e3b81fa9ad860d1490cf0225cf8\""));
    }

    vector<uint8_t> vch(ParseHex(params[0].get_str()));
    if (vch.size() > MAX_RPC_SIG_STR_LEN) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "The rawtx is too long.");
    }

    CDataStream stream(vch, SER_DISK, CLIENT_VERSION);

    std::shared_ptr<CBaseTx> tx;
    stream >> tx;
    std::tuple<bool, string> ret;
    ret = pWalletMain->CommitTx((CBaseTx *) tx.get());
    if (!std::get<0>(ret))
        throw JSONRPCError(RPC_WALLET_ERROR, "Submittxraw error: " + std::get<1>(ret));

    Object obj;
    obj.push_back(Pair("txid", std::get<1>(ret)));
    return obj;
}

class CTxMultiSigner {
public:
    struct SigningItem {
        CUserID           uid;
        UnsignedCharArray *pSignature;
    };

    struct Signedtem {
        CKeyID keyid;
        UnsignedCharArray *pSignature;
    };

private:
    CBaseTx &tx;
    set<CKeyID> &user_keyids;
public:
    vector<Signedtem> signed_list;

    CTxMultiSigner(CBaseTx &txIn, set<CKeyID> &userKeyidsIn)
        : tx(txIn), user_keyids(userKeyidsIn) {}

    void Sign(vector<SigningItem> &signingList) {

        vector<CKeyID> signingKeyids(signingList.size());
        for (size_t i = 0; i < signingList.size(); i++) {
            signingKeyids[i] = RPC_PARAM::GetUserKeyId(signingList[i].uid);
        }

        for (auto& keyid : user_keyids) {
            bool found = false;
            for (size_t i = 0; i < signingList.size(); i++) {
                if (signingKeyids[i] == keyid) {
                    signed_list.push_back({signingKeyids[i], signingList[i].pSignature});
                    found = true;
                }
            }
            if (!found) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("the user address=%s is not in the signing list",
                    keyid.ToAddress()));
            }
        }

        const uint256 &txHash = tx.GetHash();
        for (auto signedItem : signed_list) {
            if (!pWalletMain->Sign(signedItem.keyid, txHash,
                                    *signedItem.pSignature)) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Sign failed! addr=%s",
                    signedItem.keyid.ToString()));
            }
        }
    }
};

Value signtxraw(const Array& params, bool fHelp) {
    if (fHelp || params.size() != 2) {
        throw runtime_error(
            "signtxraw \"str\" \"addr\"\n"
            "\nsignature transaction\n"
            "\nArguments:\n"
            "1.\"str\": (string, required) Hex-format string, no longer than 65K in binary bytes\n"
            "2.\"addr\": (string, required) A json array of GVC addresses\n"
            "[\n"
            "  \"address\"  (string) GVC address\n"
            "  ...,\n"
            "]\n"
            "\nExamples:\n" +
            HelpExampleCli("signtxraw",
                           "\"0701ed7f0300030000010000020002000bcd10858c200200\" "
                           "\"[\\\"wKwPHfCJfUYZyjJoa6uCVdgbVJkhEnguMw\\\", "
                           "\\\"wQT2mY1onRGoERTk4bgAoAEaUjPLhLsrY4\\\", "
                           "\\\"wNw1Rr8cHPerXXGt6yxEkAPHDXmzMiQBn4\\\"]\"") +
            "\nAs json rpc call\n" +
            HelpExampleRpc("signtxraw",
                           "\"0701ed7f0300030000010000020002000bcd10858c200200\", "
                           "\"[\\\"wKwPHfCJfUYZyjJoa6uCVdgbVJkhEnguMw\\\", "
                           "\\\"wQT2mY1onRGoERTk4bgAoAEaUjPLhLsrY4\\\", "
                           "\\\"wNw1Rr8cHPerXXGt6yxEkAPHDXmzMiQBn4\\\"]\""));
    }

    vector<uint8_t> vch(ParseHex(params[0].get_str()));
    if (vch.size() > MAX_RPC_SIG_STR_LEN) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "The sig str is too long");
    }

    CDataStream stream(vch, SER_DISK, CLIENT_VERSION);
    std::shared_ptr<CBaseTx> pBaseTx;
    stream >> pBaseTx;
    if (!pBaseTx.get()) {
        return Value::null;
    }

    const Array& addresses = params[1].get_array();
    if (pBaseTx.get()->nTxType != UCOIN_TRANSFER_MTX && addresses.size() != 1) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "To many addresses provided");
    }

    set<CKeyID> users;
    for (uint32_t i = 0; i < addresses.size(); i++) {
        CUserID uid = RPC_PARAM::ParseUserIdByAddr(addresses[i]);
        users.insert(RPC_PARAM::GetUserKeyId(uid));
    }

    if (users.empty()) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No valid address provided");
    }

    CTxMultiSigner signer(*pBaseTx, users);
    switch (pBaseTx.get()->nTxType) {
        case BLOCK_REWARD_TX:
        case UCOIN_REWARD_TX:
        case UCOIN_BLOCK_REWARD_TX: {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Reward transation is forbidden");
        }
        case UCOIN_TRANSFER_MTX: {
            CMulsigTx *pTx = dynamic_cast<CMulsigTx*>(pBaseTx.get());
            vector<CTxMultiSigner::SigningItem> signingList;
            for (auto& item : pTx->signaturePairs) {
                signingList.push_back({CUserID(item.regid), &item.signature});
            }
            signer.Sign(signingList);
            break;
        }

        case DEX_OPERATOR_ORDER_TX: {
            dex::CDEXOperatorOrderTx *pTx = dynamic_cast<dex::CDEXOperatorOrderTx*>(pBaseTx.get());
            vector<CTxMultiSigner::SigningItem> signingList = {
                {pTx->txUid, &pTx->signature},
                {pTx->operator_uid, &pTx->operator_signature},
            };
            signer.Sign(signingList);
            break;
        }
        default: {
            vector<CTxMultiSigner::SigningItem> signingList = {
                {pBaseTx->txUid, &pBaseTx->signature}
            };
            signer.Sign(signingList);
        }
    }

    Array signatureArray;
    for (auto item : signer.signed_list) {
        Object itemObj;
        itemObj.push_back(Pair("addr", item.keyid.ToAddress()));
        itemObj.push_back(Pair("signature", HexStr(*item.pSignature)));
        signatureArray.push_back(itemObj);
    }

    CDataStream ds(SER_DISK, CLIENT_VERSION);
    ds << pBaseTx;

    Object obj;
    obj.push_back(Pair("rawtx", HexStr(ds.begin(), ds.end())));
    obj.push_back(Pair("signed_list", signatureArray));
    return obj;
}

Value decodemulsigscript(const Array& params, bool fHelp) {
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "decodemulsigscript \"hex\"\n"
            "\nDecode a hex-encoded script.\n"
            "\nArguments:\n"
            "1. \"hex\"     (string) the hex encoded mulsig script\n"
            "\nResult:\n"
            "{\n"
            "  \"type\":\"type\", (string) The transaction type\n"
            "  \"reqSigs\": n,    (numeric) The required signatures\n"
            "  \"addr\",\"address\" (string) mulsig script address\n"
            "  \"addresses\": [   (json array of string)\n"
            "     \"address\"     (string) bitcoin address\n"
            "     ,...\n"
            "  ]\n"
            "}\n"
            "\nExamples:\n" +
            HelpExampleCli("decodemulsigscript", "\"hexstring\"") +
            HelpExampleRpc("decodemulsigscript", "\"hexstring\""));

    RPCTypeCheck(params, list_of(str_type));

    vector<uint8_t> multiScript = ParseHex(params[0].get_str());
    if (multiScript.empty() || multiScript.size() > MAX_MULSIG_SCRIPT_SIZE) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid script size");
    }

    CDataStream ds(multiScript, SER_DISK, CLIENT_VERSION);
    CMulsigScript script;
    try {
        ds >> script;
    } catch (std::exception& e) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid script content");
    }

    CKeyID scriptId           = script.GetID();
    int8_t required           = (int8_t)script.GetRequired();
    std::set<CPubKey> pubKeys = script.GetPubKeys();

    Array addressArray;
    for (const auto& pubKey : pubKeys) {
        addressArray.push_back(pubKey.GetKeyId().ToAddress());
    }

    Object obj;
    obj.push_back(Pair("type", "mulsig"));
    obj.push_back(Pair("req_sigs", required));
    obj.push_back(Pair("addr", scriptId.ToAddress()));
    obj.push_back(Pair("addresses", addressArray));

    return obj;
}

Value decodetxraw(const Array& params, bool fHelp) {
    if (fHelp || params.size() != 1) {
        throw runtime_error(
            "decodetxraw \"hexstring\"\n"
            "\ndecode transaction\n"
            "\nArguments:\n"
            "1.\"str\": (string, required) hexstring\n"
            "\nExamples:\n" +
            HelpExampleCli("decodetxraw",
                           "\"03015f020001025a0164cd10004630440220664de5ec373f44d2756a23d5267ab25f2"
                           "2af6162d166b1cca6c76631701cbeb5022041959ff75f7c7dd39c1f9f6ef9a237a6ea46"
                           "7d02d2d2c3db62a1addaa8009ccd\"") +
            "\nAs json rpc call\n" +
            HelpExampleRpc("decodetxraw",
                           "\"03015f020001025a0164cd10004630440220664de5ec373f44d2756a23d5267ab25f2"
                           "2af6162d166b1cca6c76631701cbeb5022041959ff75f7c7dd39c1f9f6ef9a237a6ea46"
                           "7d02d2d2c3db62a1addaa8009ccd\""));
    }
    Object obj;
    vector<uint8_t> vch(ParseHex(params[0].get_str()));
    CDataStream stream(vch, SER_DISK, CLIENT_VERSION);
    std::shared_ptr<CBaseTx> pBaseTx;
    stream >> pBaseTx;
    if (!pBaseTx.get()) {
        return obj;
    }
    obj = pBaseTx->ToJson(*pCdMan->pAccountCache);
    return obj;
}

Value getcontractaccountinfo(const Array& params, bool fHelp) {
    if (fHelp || (params.size() != 2 && params.size() != 3)) {
        throw runtime_error(
            "getcontractaccountinfo \"contract regid\" \"account address or regid\""
            "\nget contract account info\n"
            "\nArguments:\n"
            "1.\"contract regid\":              (string, required) contract regid\n"
            "2.\"account address or regid\":    (string, required) contract account address or its regid\n"
            "3.\"minconf\"                      (numeric, optional, default=1) Only include contract transactions "
            "confirmed\n"
            "\nExamples:\n" +
            HelpExampleCli("getcontractaccountinfo", "\"452974-3\" \"WUZBQZZqyWgJLvEEsHrXL5vg5qaUwgfjco\"") +
            "\nAs json rpc call\n" +
            HelpExampleRpc("getcontractaccountinfo", "\"452974-3\", \"WUZBQZZqyWgJLvEEsHrXL5vg5qaUwgfjco\""));
    }

    string strAppRegId = params[0].get_str();
    if (!CRegID::IsSimpleRegIdStr(strAppRegId))
        throw runtime_error("getcontractaccountinfo: invalid contract regid: " + strAppRegId);

    CRegID appRegId(strAppRegId);
    string acctKey;
    if (CRegID::IsSimpleRegIdStr(params[1].get_str())) {
        CRegID acctRegId(params[1].get_str());
        CUserID acctUserId(acctRegId);
        acctKey = RegIDToAddress(acctUserId);
    } else { //in gvc address format
        acctKey = params[1].get_str();
    }

    std::shared_ptr<CAppUserAccount> appUserAccount = std::make_shared<CAppUserAccount>();
    if (params.size() == 3 && params[2].get_int() == 0) {
        if (!mempool.cw->contractCache.GetContractAccount(appRegId, acctKey, *appUserAccount.get())) {
            appUserAccount = std::make_shared<CAppUserAccount>(acctKey);
        }
    } else {
        if (!pCdMan->pContractCache->GetContractAccount(appRegId, acctKey, *appUserAccount.get())) {
            appUserAccount = std::make_shared<CAppUserAccount>(acctKey);
        }
    }
    appUserAccount.get()->AutoMergeFreezeToFree(chainActive.Height());

    return Value(appUserAccount.get()->ToJson());
}

Value listcontractassets(const Array& params, bool fHelp) {
    if (fHelp || params.size() != 1) {
        throw runtime_error("listcontractassets regid\n"
            "\nreturn Array containing address, asset information.\n"
            "\nArguments: regid: Contract RegId\n"
            "\nResult:\n"
            "\nExamples:\n"
            + HelpExampleCli("listcontractassets", "1-1")
            + "\nAs json rpc call\n"
            + HelpExampleRpc("listcontractassets", "1-1"));
    }

    if (!CRegID::IsSimpleRegIdStr(params[0].get_str()))
        throw runtime_error("in listcontractassets :regid is invalid!\n");

    CRegID script(params[0].get_str());

    Array retArray;
    assert(pWalletMain != nullptr);
    {
        set<CKeyID> setKeyId;
        pWalletMain->GetKeys(setKeyId);
        if (setKeyId.size() == 0)
            return retArray;

        CContractDBCache contractScriptTemp(*pCdMan->pContractCache);

        for (const auto &keyid : setKeyId) {

            string key = keyid.ToAddress();

            std::shared_ptr<CAppUserAccount> tem = std::make_shared<CAppUserAccount>();
            if (!contractScriptTemp.GetContractAccount(script, key, *tem.get())) {
                tem = std::make_shared<CAppUserAccount>(key);
            }
            tem.get()->AutoMergeFreezeToFree(chainActive.Height());

            Object obj;
            obj.push_back(Pair("addr", key));
            obj.push_back(Pair("asset", (double) tem.get()->GetBcoins() / (double) COIN));
            retArray.push_back(obj);
        }
    }

    return retArray;
}


Value gethash(const Array& params, bool fHelp) {
    if (fHelp || params.size() != 1) {
        throw runtime_error("gethash  \"str\"\n"
            "\nget the hash of given str\n"
            "\nArguments:\n"
            "1.\"str\": (string, required) \n"
            "\nresult an object \n"
            "\nExamples:\n"
            + HelpExampleCli("gethash", "\"0000001000005zQPcC1YpFMtwxiH787pSXanUECoGsxUq3KZieJxVG\"")
            + "\nAs json rpc call\n"
            + HelpExampleRpc("gethash", "\"0000001000005zQPcC1YpFMtwxiH787pSXanUECoGsxUq3KZieJxVG\""));
    }

    string str = params[0].get_str();
    vector<uint8_t> vTemp;
    vTemp.assign(str.c_str(), str.c_str() + str.length());
    uint256 strhash = Hash(vTemp.begin(), vTemp.end());
    Object obj;
    obj.push_back(Pair("txid", strhash.ToString()));
    return obj;

}

Value validateaddr(const Array& params, bool fHelp) {
    if (fHelp || params.size() != 1) {
        throw runtime_error(
            "validateaddr \"address\"\n"
            "\ncheck whether address is valid or not\n"
            "\nArguments:\n"
            "1.\"address\"      (string, required)\n"
            "\nResult:\n"
            "\nExamples:\n" +
            HelpExampleCli("validateaddr", "\"wNw1Rr8cHPerXXGt6yxEkAPHDXmzMiQBn4\"") + "\nAs json rpc call\n" +
            HelpExampleRpc("validateaddr", "\"wNw1Rr8cHPerXXGt6yxEkAPHDXmzMiQBn4\""));
    }

    Object obj;
    CKeyID keyid = RPC_PARAM::GetKeyId(params[0]);
    obj.push_back(Pair("is_valid", true));
    obj.push_back(Pair("addr", keyid.ToAddress()));

    return obj;
}

Value gettotalcoins(const Array& params, bool fHelp) {
    if (fHelp || params.size() != 0) {
        throw runtime_error(
            "gettotalcoins \n"
            "\nget the total number of circulating coins excluding those locked for votes\n"
            "\nand the total number of registered addresses\n"
            "\nArguments:\n"
            "\nResult:\n"
            "\nExamples:\n" +
            HelpExampleCli("gettotalcoins", "") + "\nAs json rpc call\n" + HelpExampleRpc("gettotalcoins", ""));
    }

    Object stats = pCdMan->pAccountCache->GetAccountDBStats();
    return stats;
}

Value listdelegates(const Array& params, bool fHelp) {
    if (fHelp || params.size() > 1) {
        throw runtime_error(
            "listdelegates \n"
            "\nreturns the specified number delegates by reversed order voting number.\n"
            "\nArguments:\n"
            "1. number           (number, optional) the number of the delegates, default to all delegates.\n"
            "\nResult:\n"
            "\nExamples:\n" +
            HelpExampleCli("listdelegates", "11") + "\nAs json rpc call\n" + HelpExampleRpc("listdelegates", "11"));
    }

    uint32_t defaultDelegateNum = pCdMan->pDelegateCache->GetActivedDelegateNum() ;

    int32_t delegateNum = (params.size() == 1) ? params[0].get_int() : defaultDelegateNum; //IniCfg().GetTotalDelegateNum();
    if (delegateNum < 1 || delegateNum > 11) {
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                           strprintf("Delegate number not between 1 and %u", defaultDelegateNum));
    }

    VoteDelegateVector delegates;
    if (!pCdMan->pDelegateCache->GetActiveDelegates(delegates)) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "get active delegates failed");
    }

    Object obj;
    Array delegateArray;

    CAccount account;
    for (const auto& delegate : delegates) {
        if (!pCdMan->pAccountCache->GetAccount(delegate.regid, account)) {
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Failed to get account info");
        }
        Object accountObj = account.ToJsonObj();
        accountObj.push_back(Pair("active_votes", delegate.votes));
        delegateArray.push_back(accountObj);
    }

    obj.push_back(Pair("delegates", delegateArray));

    return obj;
}
