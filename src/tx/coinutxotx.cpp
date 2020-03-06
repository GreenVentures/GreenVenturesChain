// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2017-2019 The GreenVenturesChain Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#include "coinutxotx.h"
#include "main.h"
#include <string>
#include <cstdarg>

bool GetUtxoTxFromChain(TxID &txid, std::shared_ptr<CBaseTx> &pBaseTx) {
    if (!SysCfg().IsTxIndex())
        return false;

    CDiskTxPos txPos;
    if (pCdMan->pBlockCache->ReadTxIndex(txid, txPos)) {
        LOCK(cs_main);
        CAutoFile file(OpenBlockFile(txPos, true), SER_DISK, CLIENT_VERSION);
        CBlockHeader header;

        try {
            file >> header;
            fseek(file, txPos.nTxOffset, SEEK_CUR);
            file >> pBaseTx;

        } catch (std::exception &e) {
            throw runtime_error(tfm::format("%s : Deserialize or I/O error - %s", __func__, e.what()).c_str());
        }
    }
    return true;
}

inline bool CheckUtxoOutCondition( const CTxExecuteContext &context, const bool isPrevUtxoOut,
                                const CUserID &prevUtxoTxUid, const CUserID &txUid,
                                const CUtxoInput &input, CUtxoCondStorageBean &cond) {
    CValidationState &state = *context.pState;

    switch (cond.sp_utxo_cond->cond_type) {
        case UtxoCondType::OP2SA : {
            CSingleAddressCondOut& theCond = dynamic_cast< CSingleAddressCondOut& > (*cond.sp_utxo_cond);

            if(isPrevUtxoOut) {
                if (theCond.uid != txUid)
                    return state.DoS(100, ERRORMSG("CCoinUtxoTransferTx::CheckTx, uid mismatches error!"), REJECT_INVALID,
                                    "uid-mismatches-err");
            } else {
                if (theCond.uid.IsEmpty())
                    return state.DoS(100, ERRORMSG("CCoinUtxoTransferTx::CheckTx, uid empty error!"), REJECT_INVALID,
                                    "uid-empty-err");
            }
            break;
        }

        case UtxoCondType::OP2MA : {
            CMultiSignAddressCondOut& theCond = dynamic_cast< CMultiSignAddressCondOut& > (*cond.sp_utxo_cond);

            if (isPrevUtxoOut) { //theCond is the previous UTXO output
                bool found = false;
                for (auto inputCond : input.conds) {
                    if (inputCond.sp_utxo_cond->cond_type == UtxoCondType::IP2MA) {
                        found = true;
                        CMultiSignAddressCondIn& p2maCondIn = dynamic_cast< CMultiSignAddressCondIn& > (*inputCond.sp_utxo_cond);
                        if (p2maCondIn.m > p2maCondIn.n) {
                            return state.DoS(100, ERRORMSG("CCoinUtxoTransferTx::CheckTx, cond multisig m > n error!"), REJECT_INVALID,
                                            "cond-multsig-m-larger-than-n-err");
                        }
                        if (p2maCondIn.m > 20 || p2maCondIn.n > 20) { //FIXME: replace 20 w/ sysparam
                            return state.DoS(100, ERRORMSG("CCoinUtxoTransferTx::CheckTx, cond multisig m/n too large!"), REJECT_INVALID,
                                            "cond-multsig-mn-too-large-err");
                        }
                        if (p2maCondIn.uids.size() != p2maCondIn.n) {
                             return state.DoS(100, ERRORMSG("CCoinUtxoTransferTx::CheckTx, cond multisig uids size mismatch!"), REJECT_INVALID,
                                            "cond-multsig-uids-size-mismatch-err");
                        }
                        if ((uint160) theCond.uid.get<CKeyID>() != p2maCondIn.GetRedeemScriptHash()) {
                            return state.DoS(100, ERRORMSG("CCoinUtxoTransferTx::CheckTx, cond multisig addr mismatch error!"), REJECT_INVALID,
                                            "cond-multsig-addr-mismatch-err");
                        }
                        if (!p2maCondIn.VerifyMultiSig(input.prev_utxo_txid, input.prev_utxo_vout_index, txUid)) {
                            return state.DoS(100, ERRORMSG("CCoinUtxoTransferTx::CheckTx, cond multisig verify failed!"), REJECT_INVALID,
                                            "cond-multsig-verify-fail");
                        }
                        break;
                    }
                }

                if (!found) {
                     return state.DoS(100, ERRORMSG("CCoinUtxoTransferTx::CheckTx, cond multisign missing error!"), REJECT_INVALID,
                                    "cond-multsign-missing-err");
                }
            } else {
                if (theCond.uid.IsEmpty()) {
                    return state.DoS(100, ERRORMSG("CCoinUtxoTransferTx::CheckTx, uid empty error!"), REJECT_INVALID,
                                    "uid-empty-err");
                }
            }
            break;
        }

        case UtxoCondType::OP2PH : {
            CPasswordHashLockCondOut& theCond = dynamic_cast< CPasswordHashLockCondOut& > (*cond.sp_utxo_cond);

            if (isPrevUtxoOut) {
                bool found = false;
                for (auto inputCond : input.conds) {
                    if (cond.sp_utxo_cond->cond_type == UtxoCondType::IP2PH) {
                        found = true;
                        CPasswordHashLockCondIn& p2phCondIn = dynamic_cast< CPasswordHashLockCondIn& > (*inputCond.sp_utxo_cond);

                        if (p2phCondIn.password.size() > 256) { //FIXME: sysparam
                             return state.DoS(100, ERRORMSG("CCoinUtxoTransferTx::CheckTx, secret size too large error!"), REJECT_INVALID,
                                            "secret-size-toolarge-err");
                        }
                        if (theCond.password_proof_required) { //check if existing password ownership proof
                            string text = strprintf("%s%s%s%s%d", p2phCondIn.password,
                                                    prevUtxoTxUid.ToString(), txUid.ToString(),
                                                    input.prev_utxo_txid.ToString(), input.prev_utxo_vout_index);

                            uint256 hash = Hash(text);
                            uint256 proof = uint256();
                            CRegIDKey regIdKey(txUid.get<CRegID>());
                            auto proofKey = std::make_tuple(input.prev_utxo_txid, CFixedUInt16(input.prev_utxo_vout_index), regIdKey);
                            if (context.pCw->txUtxoCache.GetUtxoPasswordProof(proofKey, proof))
                                return state.DoS(100, ERRORMSG("CCoinUtxoTransferTx::CheckTx, password proof not existing!"), REJECT_INVALID,
                                                "password-proof-not-exist-err");

                            if (hash != proof)
                                return state.DoS(100, ERRORMSG("CCoinUtxoTransferTx::CheckTx, password proof not match!"), REJECT_INVALID,
                                            "password-proof-not-match-err");

                        }
                        // further check if password_hash matches the hash of (TxUid,Password)
                        string text = strprintf("%s%s", prevUtxoTxUid.ToString(), p2phCondIn.password);
                        uint256 hash = Hash(text);
                        if (theCond.password_hash != hash)
                            return state.DoS(100, ERRORMSG("CCoinUtxoTransferTx::CheckTx, secret mismatches error!"), REJECT_INVALID,
                                            "secret-mismatches-err");
                        break;
                    } else
                        continue;
                }
                if (!found)
                     return state.DoS(100, ERRORMSG("CCoinUtxoTransferTx::CheckTx, cond mismatches error!"), REJECT_INVALID,
                                    "cond-mismatches-err");
            } else { //output cond
                if (theCond.password_hash == uint256())
                    return state.DoS(100, ERRORMSG("CCoinUtxoTransferTx::CheckTx, empty hash lock error!"), REJECT_INVALID,
                                    "empty-hash-lock-err");
            }
            break;
        }
        case UtxoCondType::OCLAIM_LOCK : {
            CClaimLockCondOut& theCond = dynamic_cast< CClaimLockCondOut& > (*cond.sp_utxo_cond);

            if (isPrevUtxoOut) {
                if ((uint64_t) context.height <= theCond.height)
                    return state.DoS(100, ERRORMSG("CCoinUtxoTransferTx::CheckTx, too early to claim error!"), REJECT_INVALID,
                                    "too-early-to-claim-err");
            } else { //output cond
                if (theCond.height == 0)
                    return state.DoS(100, ERRORMSG("CCoinUtxoTransferTx::CheckTx, claim lock empty error!"), REJECT_INVALID,
                                    "claim-lock-empty-err");
            }
            break;
        }
        case UtxoCondType::ORECLAIM_LOCK : {
            CReClaimLockCondOut& theCond = dynamic_cast< CReClaimLockCondOut& > (*cond.sp_utxo_cond);

            if (isPrevUtxoOut) {
                if (prevUtxoTxUid == txUid) { // for reclaiming the coins
                    if (theCond.height == 0 || (uint64_t) context.height <= theCond.height)
                        return state.DoS(100, ERRORMSG("CCoinUtxoTransferTx::CheckTx, too early to reclaim error!"), REJECT_INVALID,
                                        "too-early-to-claim-err");
                }
            } else { //output cond
                if (theCond.height == 0)
                    return state.DoS(100, ERRORMSG("CCoinUtxoTransferTx::CheckTx, reclaim lock empty error!"), REJECT_INVALID,
                                    "reclaim-lock-empty-err");
            }
            break;
        }
        default: {
            string strInOut = isPrevUtxoOut ? "input" : "output";
            return state.DoS(100, ERRORMSG("CCoinUtxoTransferTx::CheckTx, %s cond type error!", strInOut), REJECT_INVALID,
                            "cond-type-err");
        }
    }
    return true;
}

bool CCoinUtxoTransferTx::CheckTx(CTxExecuteContext &context) {
    IMPLEMENT_DEFINE_CW_STATE;
    IMPLEMENT_DISABLE_TX_PRE_STABLE_COIN_RELEASE;
    IMPLEMENT_CHECK_TX_MEMO;
    IMPLEMENT_CHECK_TX_REGID_OR_PUBKEY(txUid);
    if (!CheckFee(context)) return false;

    if ((txUid.is<CPubKey>()) && !txUid.get<CPubKey>().IsFullyValid())
        return state.DoS(100, ERRORMSG("CCoinUtxoTransferTx::CheckTx, public key is invalid"), REJECT_INVALID,
                        "bad-publickey");

    CAccount srcAccount;
    if (!cw.accountCache.GetAccount(txUid, srcAccount)) //unrecorded account not allowed to participate
        return state.DoS(100, ERRORMSG("CCoinUtxoTransferTx::CheckTx, read account failed"), REJECT_INVALID,
                        "bad-getaccount");

    if (vins.size() > 100) //FIXME: need to use sysparam to replace 100
        return state.DoS(100, ERRORMSG("CCoinUtxoTransferTx::CheckTx, vins size > 100 error"), REJECT_INVALID,
                        "vins-size-too-large");

    if (vouts.size() > 100) //FIXME: need to use sysparam to replace 100
        return state.DoS(100, ERRORMSG("CCoinUtxoTransferTx::CheckTx, vouts size > 100 error"), REJECT_INVALID,
                        "vouts-size-too-large");

    if (vins.size() == 0 && vouts.size() == 0)
        return state.DoS(100, ERRORMSG("CCoinUtxoTransferTx::CheckTx, empty utxo error"), REJECT_INVALID,
                        "utxo-empty-err");

    uint64_t minFee;
    if (!GetTxMinFee(nTxType, context.height, fee_symbol, minFee)) { assert(false); }
    uint64_t minerMinFees = (2 * vins.size() + vouts.size()) * minFee;
    if (llFees < minerMinFees)
        return state.DoS(100, ERRORMSG("CCoinUtxoTransferTx::CheckTx, tx fee too small!"), REJECT_INVALID,
                        "bad-tx-fee-toosmall");

    uint64_t totalInAmount = 0;
    uint64_t totalOutAmount = 0;
    for (auto input : vins) {
        //load prevUtxoTx from blockchain
        std::shared_ptr<CCoinUtxoTransferTx> pPrevUtxoTx;
        std::shared_ptr<CBaseTx> pBaseTx = pPrevUtxoTx;
        if (!GetUtxoTxFromChain(input.prev_utxo_txid, pBaseTx))
            return state.DoS(100, ERRORMSG("CCoinUtxoTransferTx::CheckTx, failed to load prev utxo from chain!"), REJECT_INVALID,
                            "failed-to-load-prev-utxo-err");

        if ((uint16_t) pPrevUtxoTx->vouts.size() < input.prev_utxo_vout_index + 1)
            return state.DoS(100, ERRORMSG("CCoinUtxoTransferTx::CheckTx, prev utxo index OOR error!"), REJECT_INVALID,
                            "prev-utxo-index-OOR-err");

        //enumerate the prev tx out conditions to check if current input meets
        //the output conditions of the previous Tx
        for (auto cond : pPrevUtxoTx->vouts[input.prev_utxo_vout_index].conds)
            CheckUtxoOutCondition(context, true, pPrevUtxoTx->txUid, txUid, input, cond);

        totalInAmount += pPrevUtxoTx->vouts[input.prev_utxo_vout_index].coin_amount;
    }

    for (auto output : vouts) {
        if (output.coin_amount == 0)
            return state.DoS(100, ERRORMSG("CCoinUtxoTransferTx::CheckTx, zeror output amount error!"), REJECT_INVALID,
                            "zero-output-amount-err");

        //check each cond's validity
        for (auto cond : output.conds)
            CheckUtxoOutCondition(context, false, CUserID(), txUid, CUtxoInput(), cond);

        totalOutAmount += output.coin_amount;
    }

    uint64_t accountBalance = srcAccount.GetBalance(coin_symbol, BalanceType::FREE_VALUE);
    if (accountBalance + totalInAmount < totalOutAmount + llFees) {
        return state.DoS(100, ERRORMSG("CCoinUtxoTransferTx::CheckTx, account balance coin_amount insufficient!"), REJECT_INVALID,
                        "insufficient-account-coin-amount");
    }

    CPubKey pubKey = (txUid.is<CPubKey>() ? txUid.get<CPubKey>() : srcAccount.owner_pubkey);
    IMPLEMENT_CHECK_TX_SIGNATURE(pubKey);

    return true;
}

/**
 * only deal with account balance states change...nothing on UTXO
 */
bool CCoinUtxoTransferTx::ExecuteTx(CTxExecuteContext &context) {
    CCacheWrapper &cw       = *context.pCw;
    CValidationState &state = *context.pState;

    CAccount srcAccount;
    if (!cw.accountCache.GetAccount(txUid, srcAccount))
        return state.DoS(100, ERRORMSG("CCoinUtxoTransferTx::ExecuteTx, read txUid %s account info error",
                        txUid.ToString()), READ_ACCOUNT_FAIL, "bad-read-accountdb");

    if (!GenerateRegID(context, srcAccount)) {
        return false;
    }

    vector<CReceipt> receipts;

    uint64_t totalInAmount = 0;
    uint64_t totalOutAmount = 0;
    for (auto input : vins) {
        if (!context.pCw->txUtxoCache.GetUtxoTx(std::make_pair(input.prev_utxo_txid, CFixedUInt16(input.prev_utxo_vout_index))))
            return state.DoS(100, ERRORMSG("CCoinUtxoTransferTx::CheckTx, prev utxo already spent error!"), REJECT_INVALID,
                            "double-spend-prev-utxo-err");

        //load prevUtxoTx from blockchain
        std::shared_ptr<CCoinUtxoTransferTx> pPrevUtxoTx;
        std::shared_ptr<CBaseTx> pBaseTx = pPrevUtxoTx;
        if (!GetUtxoTxFromChain(input.prev_utxo_txid, pBaseTx))
            return state.DoS(100, ERRORMSG("CCoinUtxoTransferTx::CheckTx, failed to load prev utxo from chain!"), REJECT_INVALID,
                            "failed-to-load-prev-utxo-err");

        totalInAmount += pPrevUtxoTx->vouts[input.prev_utxo_vout_index].coin_amount;

        if (!context.pCw->txUtxoCache.DelUtoxTx(std::make_pair(input.prev_utxo_txid, CFixedUInt16(input.prev_utxo_vout_index))))
            return state.DoS(100, ERRORMSG("CCoinUtxoTransferTx::CheckTx, del prev utxo error!"), REJECT_INVALID,
                            "del-prev-utxo-err");

        uint256 proof = uint256();
        CRegIDKey regIdKey(txUid.get<CRegID>());
        auto proofKey = std::make_tuple(input.prev_utxo_txid, CFixedUInt16(input.prev_utxo_vout_index), regIdKey);
        if (context.pCw->txUtxoCache.GetUtxoPasswordProof(proofKey, proof)) {
            context.pCw->txUtxoCache.DelUtoxPasswordProof(proofKey);
        }
    }

    for (size_t i = 0; i < vouts.size(); i++) {
        CUtxoOutput output = vouts[i];
        totalOutAmount += output.coin_amount;

        if (!context.pCw->txUtxoCache.SetUtxoTx(std::make_pair(GetHash(), CFixedUInt16(i))))
            return state.DoS(100, ERRORMSG("CCoinUtxoTransferTx::CheckTx, set utxo error!"), REJECT_INVALID, "set-utxo-err");
    }

    uint64_t accountBalance = srcAccount.GetBalance(coin_symbol, BalanceType::FREE_VALUE);
    if (accountBalance + totalInAmount < totalOutAmount + llFees) {
        return state.DoS(100, ERRORMSG("CCoinUtxoTransferTx::CheckTx, account balance coin_amount insufficient!"), REJECT_INVALID,
                        "insufficient-account-coin-amount");
    }
    int diff = totalInAmount - totalOutAmount - llFees;
    if (diff < 0) {
        if (!srcAccount.OperateBalance(coin_symbol, SUB_FREE, abs(diff))) {
            return state.DoS(100, ERRORMSG("CCoinUtxoTransferTx::ExecuteTx, failed to deduct coin_amount in txUid %s account",
                            txUid.ToString()), UPDATE_ACCOUNT_FAIL, "insufficient-fund-utxo");
        }
        receipts.emplace_back(txUid, CNullID(), coin_symbol, abs(diff), ReceiptCode::TRANSFER_UTXO_COINS);
    } else if (diff > 0) {
        if (!srcAccount.OperateBalance(coin_symbol, ADD_FREE, diff)) {
            return state.DoS(100, ERRORMSG("CCoinUtxoTransferTx::ExecuteTx, failed to add coin_amount in txUid %s account",
                            txUid.ToString()), UPDATE_ACCOUNT_FAIL, "insufficient-fund-utxo");
        }
        receipts.emplace_back(CNullID(), txUid, coin_symbol, abs(diff), ReceiptCode::TRANSFER_UTXO_COINS);
    }

    if (!cw.accountCache.SaveAccount(srcAccount))
        return state.DoS(100, ERRORMSG("CCoinUtxoTransferTx::ExecuteTx, write source addr %s account info error",
                        txUid.ToString()), UPDATE_ACCOUNT_FAIL, "bad-read-accountdb");

    if (!receipts.empty() && !cw.txReceiptCache.SetTxReceipts(GetHash(), receipts))
        return state.DoS(100, ERRORMSG("CCDPStakeTx::ExecuteTx, set tx receipts failed!! txid=%s",
                        GetHash().ToString()), REJECT_INVALID, "set-tx-receipt-failed");

    return true;
}


////////////////////////////////////////
/// class CCoinUtxoPasswordProofTx
////////////////////////////////////////
bool CCoinUtxoPasswordProofTx::CheckTx(CTxExecuteContext &context) {
    IMPLEMENT_DEFINE_CW_STATE;
    IMPLEMENT_DISABLE_TX_PRE_STABLE_COIN_RELEASE;
    IMPLEMENT_CHECK_TX_REGID_OR_PUBKEY(txUid);
    if (!CheckFee(context)) return false;

    if ((txUid.is<CPubKey>()) && !txUid.get<CPubKey>().IsFullyValid())
        return state.DoS(100, ERRORMSG("CCoinUtxoTransferTx::CheckTx, public key is invalid"), REJECT_INVALID,
                        "bad-publickey");

    CAccount srcAccount;
    if (!cw.accountCache.GetAccount(txUid, srcAccount)) //unrecorded account not allowed to participate
        return state.DoS(100, ERRORMSG("CCoinUtxoTransferTx::CheckTx, read account failed"), REJECT_INVALID,
                        "bad-getaccount");

    uint64_t minFee;
    if (!GetTxMinFee(nTxType, context.height, fee_symbol, minFee)) { assert(false); }
    if (llFees < minFee)
        return state.DoS(100, ERRORMSG("CCoinUtxoTransferTx::CheckTx, tx fee too small!"), REJECT_INVALID,
                        "bad-tx-fee-toosmall");

    if (utxo_txid.IsEmpty())
        return state.DoS(100, ERRORMSG("CCoinUtxoTransferTx::CheckTx, utxo txid empty error!"), REJECT_INVALID,
                        "uxto-txid-empty-err");

    if (password_proof.IsEmpty())
        return state.DoS(100, ERRORMSG("CCoinUtxoTransferTx::CheckTx, utxo password proof empty error!"), REJECT_INVALID,
                        "utxo-password-proof-empty-err");


    CPubKey pubKey = (txUid.is<CPubKey>() ? txUid.get<CPubKey>() : srcAccount.owner_pubkey);
    IMPLEMENT_CHECK_TX_SIGNATURE(pubKey);
    return true;
}

bool CCoinUtxoPasswordProofTx::ExecuteTx(CTxExecuteContext &context) {
    CCacheWrapper &cw       = *context.pCw;
    CValidationState &state = *context.pState;

    CAccount srcAccount;
    if (!cw.accountCache.GetAccount(txUid, srcAccount))
        return state.DoS(100, ERRORMSG("CCoinUtxoTransferTx::ExecuteTx, read txUid %s account info error",
                        txUid.ToString()), READ_ACCOUNT_FAIL, "bad-read-accountdb");

    if (!GenerateRegID(context, srcAccount))
        return false;

    CRegIDKey regIdKey(txUid.get<CRegID>());
    if (!cw.txUtxoCache.SetUtxoPasswordProof(std::make_tuple(utxo_txid, CFixedUInt16(utxo_vout_index), regIdKey), password_proof))
        return state.DoS(100, ERRORMSG("CCoinUtxoTransferTx::ExecuteTx, bad saving utxo proof",
                        txUid.ToString()), READ_ACCOUNT_FAIL, "bad-save-utxo-passwordproof");

    return true;
}
