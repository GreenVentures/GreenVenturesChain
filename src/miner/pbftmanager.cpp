// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2017-2019 The GreenVenturesChain Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#include "pbftmanager.h"
#include "miner/pbftcontext.h"
#include "persistence/cachewrapper.h"
#include "p2p/protocol.h"
#include "miner/miner.h"
#include "wallet/wallet.h"

CPBFTMan pbftMan;
extern CPBFTContext pbftContext;
extern CWallet *pWalletMain;
extern CCacheDBManager *pCdMan;

uint32_t GetFinalBlockMinerCount() {
    uint32_t totalBpCount =  pCdMan->pDelegateCache->GetActivedDelegateNum() ;
    return totalBpCount - totalBpCount/3 ;
}

CBlockIndex* CPBFTMan::GetLocalFinIndex(){

    if(!localFinIndex) {
        return chainActive[0];
    }
    return localFinIndex ;
}


CBlockIndex* CPBFTMan::GetGlobalFinIndex(){

    if(!globalFinIndex) {
        return chainActive[0] ;
    }
    return globalFinIndex ;
}

uint256 CPBFTMan::GetGlobalFinBlockHash() {
    if(!globalFinIndex) {
        if(globalFinHash != uint256())
            return globalFinHash;
        {
            LOCK(cs_main);
            std::pair<int32_t ,uint256> globalfinblock = std::make_pair(0,uint256());
            if(pCdMan->pBlockCache->ReadGlobalFinBlock(globalfinblock)){
                globalFinHash = globalfinblock.second ;
            } else if(chainActive[0] != nullptr){
                globalFinHash = chainActive[0]->GetBlockHash() ;
            }
            return globalFinHash ;
        }

    }
    return globalFinIndex->GetBlockHash() ;

}

bool CPBFTMan::SetLocalFinTimeout(){
    LOCK(cs_finblock);
    localFinIndex = chainActive[0];
    return true ;
}
bool CPBFTMan::UpdateLocalFinBlock(const uint32_t height) {
    {
        LOCK(cs_finblock);
        CBlockIndex* oldFinblock = GetLocalFinIndex();
        if(oldFinblock != nullptr && (uint32_t)oldFinblock->height >= height)
            return false;
        CBlockIndex* pTemp = chainActive[height];
        if(pTemp== nullptr)
            return false ;

        localFinIndex = pTemp;
        localFinLastUpdate = GetTime();
        return true ;
    }

}

bool CPBFTMan::UpdateGlobalFinBlock(const uint32_t height) {
    {
        LOCK(cs_finblock);
        CBlockIndex* oldGlobalFinblock = GetGlobalFinIndex();
        CBlockIndex* localFinblock = GetLocalFinIndex() ;

        if(localFinblock== nullptr ||height > (uint32_t)localFinblock->height)
            return false ;
        if(oldGlobalFinblock != nullptr && (uint32_t)oldGlobalFinblock->height >= height)
            return false;

        if(oldGlobalFinblock != nullptr ){
            CBlockIndex* chainBlock = chainActive[oldGlobalFinblock->height] ;
            if(chainBlock != nullptr && chainBlock->GetBlockHash() != oldGlobalFinblock->GetBlockHash()){
                return ERRORMSG("Global finality block changed");
            }
        }

        CBlockIndex* pTemp = chainActive[height];
        if(pTemp== nullptr)
            return false ;
        globalFinIndex = pTemp;
        globalFinHash = pTemp->GetBlockHash() ;
        pCdMan->pBlockCache->WriteGlobalFinBlock(pTemp->height, pTemp->GetBlockHash()) ;
        return true ;
    }

}

bool CPBFTMan::UpdateLocalFinBlock(const CBlockIndex* pIndex){


    if(pIndex == nullptr|| pIndex->height==0)
        return false ;
    int32_t height = pIndex->height;

    uint32_t needConfirmCount = GetFinalBlockMinerCount() ;

    while(height > GetLocalFinIndex()->height&& height>0 &&height > pIndex->height-10){

        CBlockIndex* pTemp = chainActive[height] ;

        set<CBlockConfirmMessage> messageSet ;
        set<CRegID> miners;

        if(pbftContext.confirmMessageMan.GetMessagesByBlockHash(pTemp->GetBlockHash(), messageSet)
           && pbftContext.GetMinerListByBlockHash(pTemp->pprev->GetBlockHash(),miners)){

            if(messageSet.size() >= needConfirmCount){
                uint32_t count =0;
                for(auto msg: messageSet){
                    if(miners.count(msg.miner))
                        count++ ;
                    if(count >= needConfirmCount){
                        return UpdateLocalFinBlock( height) ;
                    }
                }
            }

        }

        height--;

    }
    return false ;
}

bool CPBFTMan::UpdateLocalFinBlock(const CBlockConfirmMessage& msg, const uint32_t messageCount){

    uint32_t needConfirmCount = GetFinalBlockMinerCount() ;
    if( needConfirmCount > messageCount)
        return false ;

    CBlockIndex* fi = GetLocalFinIndex();

    if(fi == nullptr ||(uint32_t)fi->height >= msg.height)
        return false;

    CBlockIndex* pIndex = chainActive[msg.height] ;
    if(pIndex == nullptr || pIndex->pprev== nullptr)
        return false;

    if(pIndex->GetBlockHash() != msg.blockHash)
        return false;

    set<CBlockConfirmMessage> messageSet ;
    set<CRegID> miners;


    if (pbftContext.confirmMessageMan.GetMessagesByBlockHash(pIndex->GetBlockHash(), messageSet)
       && pbftContext.GetMinerListByBlockHash(pIndex->pprev->GetBlockHash(),miners)) {
        if (messageSet.size() >= needConfirmCount){
            uint32_t count =0;
            for (auto msg: messageSet){
                if (miners.count(msg.miner))
                    count++ ;

                if (count >= needConfirmCount)
                    return UpdateLocalFinBlock(pIndex->height) ;
            }
        }

    }
    return false;
}

bool CPBFTMan::UpdateGlobalFinBlock(const CBlockIndex* pIndex){

    if (pIndex == nullptr|| pIndex->height==0)
        return false ;

    int32_t height = pIndex->height;
    uint32_t needConfirmCount = GetFinalBlockMinerCount() ;

    while (height > GetGlobalFinIndex()->height&& height>0 &&height > pIndex->height-50) {
        CBlockIndex* pTemp = chainActive[height] ;

        set<CBlockFinalityMessage> messageSet ;
        set<CRegID> miners;

        if (pbftContext.finalityMessageMan.GetMessagesByBlockHash(pTemp->GetBlockHash(), messageSet)
           && pbftContext.GetMinerListByBlockHash(pTemp->pprev->GetBlockHash(),miners)){

            if (messageSet.size() >= needConfirmCount){
                uint32_t count =0;
                for (auto msg: messageSet){
                    if (miners.count(msg.miner))
                        count++ ;

                    if (count >= needConfirmCount)
                        return UpdateGlobalFinBlock( height) ;
                }
            }

        }

        height--;

    }
    return false ;
}


int64_t  CPBFTMan::GetLocalFinLastUpdate() const {
    return localFinLastUpdate ;
}

bool CPBFTMan::UpdateGlobalFinBlock(const CBlockFinalityMessage& msg, const uint32_t messageCount ){

    uint32_t needConfirmCount = GetFinalBlockMinerCount() ;
    if(needConfirmCount > messageCount)
        return false ;

    CBlockIndex* fi = GetGlobalFinIndex();

    if (fi == nullptr || (uint32_t) fi->height >= msg.height)
        return false;

    CBlockIndex* pIndex = chainActive[msg.height] ;
    if (pIndex == nullptr || pIndex->pprev== nullptr)
        return false;

    if (pIndex->GetBlockHash() != msg.blockHash)
        return false;

    set<CBlockFinalityMessage> messageSet ;
    set<CRegID> miners;

    if (pbftContext.finalityMessageMan.GetMessagesByBlockHash(pIndex->GetBlockHash(), messageSet)
       && pbftContext.GetMinerListByBlockHash(pIndex->pprev->GetBlockHash(),miners)) {
        if (messageSet.size() >= needConfirmCount){
            uint32_t count = 0;
            for (auto msg: messageSet){
                if (miners.count(msg.miner))
                    count++ ;

                if (count >= needConfirmCount)
                    return UpdateGlobalFinBlock(pIndex->height) ;
            }
        }
    }
    return false;
}

static bool PbftFindMiner(CRegID delegate, Miner &miner){
    {
        LOCK(cs_main);
        if (!pCdMan->pAccountCache->GetAccount(delegate, miner.account)) {
            LogPrint(BCLog::MINER, "PbftFindMiner() : fail to get miner account! regid=%s\n",
                     miner.delegate.regid.ToString());
            return false;
        }
    }

    {
        LOCK(pWalletMain->cs_wallet);
        if (miner.account.miner_pubkey.IsValid() && pWalletMain->GetKey(miner.account.keyid, miner.key, true)) {
            return true;
        } else if (!pWalletMain->GetKey(miner.account.keyid, miner.key)) {
            return false;
        }
    }

    return true;
}

bool BroadcastBlockFinality(const CBlockIndex* block){


    if(!SysCfg().GetBoolArg("-genblock", false))
        return false ;

    if(IsInitialBlockDownload())
        return false ;

    CPBFTMessageMan<CBlockFinalityMessage>& msgMan = pbftContext.finalityMessageMan;

    if(msgMan.IsBroadcastedBlock(block->GetBlockHash()))
        return true ;

    //查找上一个区块执行过后的矿工列表
    set<CRegID> delegates;

    if(block->pprev == nullptr)
        return false ;
    pbftContext.GetMinerListByBlockHash(block->pprev->GetBlockHash(), delegates);

    uint256 preHash = block->pprev == nullptr? uint256(): block->pprev->GetBlockHash();


    CBlockFinalityMessage msg(block->height, block->GetBlockHash(), preHash);

    {

        for(auto delegate: delegates){

            Miner miner ;
            if(!PbftFindMiner(delegate, miner))
                continue ;
            msg.miner = miner.account.regid ;
            vector<unsigned char > vSign ;
            uint256 messageHash = msg.GetHash();

            miner.key.Sign(messageHash, vSign);
            msg.SetSignature(vSign);

            {
                LOCK(cs_vNodes);
                for (auto pNode : vNodes) {
                    pNode->PushBlockFinalityMessage(msg) ;
                }
            }

            msgMan.SaveMessageByBlock(msg.blockHash, msg);

        }
    }

    msgMan.SaveBroadcastedBlock(block->GetBlockHash());
    return true ;

}
bool BroadcastBlockConfirm(const CBlockIndex* block) {

    if(!SysCfg().GetBoolArg("-genblock", false))
        return false ;

    //当矿工的区块落后较大时不发送确认消息， 防止消息过多，淹没网络
    if(GetTime() - block->GetBlockTime() > 60)
        return false;

    if(IsInitialBlockDownload())
        return false ;

    CPBFTMessageMan<CBlockConfirmMessage>& msgMan = pbftContext.confirmMessageMan ;

    if(msgMan.IsBroadcastedBlock(block->GetBlockHash()))
        return true ;

    //查找上一个区块执行过后的矿工列表
    set<CRegID> delegates;

    if(block->pprev == nullptr)
        return false ;
    pbftContext.GetMinerListByBlockHash(block->pprev->GetBlockHash(), delegates);

    uint256 preHash = block->pprev == nullptr? uint256(): block->pprev->GetBlockHash();
    CBlockConfirmMessage msg(block->height, block->GetBlockHash(), preHash);

    {

        for(auto delegate: delegates){
            Miner miner ;
            if(!PbftFindMiner(delegate, miner))
                continue ;
            msg.miner = miner.account.regid ;
            vector<unsigned char > vSign ;
            uint256 messageHash = msg.GetHash() ;

            miner.key.Sign(messageHash, vSign);
            msg.SetSignature(vSign);

            {
                LOCK(cs_vNodes);
                for (auto pNode : vNodes) {
                    pNode->PushBlockConfirmMessage(msg) ;
                }
            }
            msgMan.SaveMessageByBlock(msg.blockHash,msg);

        }
    }

    msgMan.SaveBroadcastedBlock(block->GetBlockHash());
    return true ;
}

bool CheckPBFTMessageSignaturer(const CPBFTMessage& msg) {

    //查找上一个区块执行过后的矿工列表
    set<CRegID> delegates;

    if(pbftContext.GetMinerListByBlockHash(msg.preBlockHash, delegates)) {
        if(delegates.count(msg.miner) > 0)
            return true ;
    }
    return false ;
}

bool CheckPBFTMessage(const int32_t msgType ,const CPBFTMessage& msg){

    //check height

    CBlockIndex* localFinBlock = pbftMan.GetLocalFinIndex() ;
    if(msg.height - chainActive.Height()>500 || (localFinBlock && msg.height < (uint32_t)localFinBlock->height) ) {
        return ERRORMSG("checkPBftMessage():: messagesHeight is out range");
    }

    //check message type ;
    if(msg.msgType != msgType )
        return ERRORMSG("checkPbftMessage(), msgType is illegal") ;

    //if block received,check whether on chainActive
    CBlockIndex* pIndex = chainActive[msg.height] ;
    if(pIndex != nullptr &&pIndex->GetBlockHash() != msg.blockHash){
        return ERRORMSG("checkPbftMessage(): block not on chainActive") ;
    }

    //check signature
    CAccount account ;
    {
        LOCK(cs_main) ;
        if(!pCdMan->pAccountCache->GetAccount(msg.miner, account)) {
            return ERRORMSG("checkPBftMessage() : the signature creator is not found!");
        }
    }
    uint256 messageHash = msg.GetHash();
    if (!VerifySignature(messageHash, msg.vSignature, account.owner_pubkey)) {
        if (!VerifySignature(messageHash, msg.vSignature, account.miner_pubkey))
            return ERRORMSG("checkPBftMessage() : verify signature error");
    }

    return true ;

}

bool RelayBlockConfirmMessage(const CBlockConfirmMessage& msg){

    LOCK(cs_vNodes) ;
    for(auto node:vNodes){
        node->PushBlockConfirmMessage(msg);
    }
    return true ;
}

bool RelayBlockFinalityMessage(const CBlockFinalityMessage& msg){

    LOCK(cs_vNodes);
    for(auto node:vNodes){
        node->PushBlockFinalityMessage(msg);
    }
    return true ;
}


