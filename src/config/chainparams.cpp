// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2017-2019 The GreenVenturesChain Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#include "chainparams.h"

#include "assert.h"
#include "entities/key.h"
#include "commons/util/util.h"
#include "configuration.h"
#include "main.h"
#include "tx/blockrewardtx.h"
#include "tx/delegatetx.h"
#include "tx/coinrewardtx.h"

#include <boost/assign/list_of.hpp>
#include <boost/algorithm/string/case_conv.hpp> // for to_lower()
#include <boost/algorithm/string/predicate.hpp> // for startswith() and endswith()
#include <boost/filesystem.hpp>

#include <string>
#include <iostream>
#include <memory>

using namespace boost::assign;
using namespace std;

map<string, string> CBaseParams::m_mapArgs;
map<string, vector<string> > CBaseParams::m_mapMultiArgs;

class CMainParams: public CBaseParams {
public:
    CMainParams() {
        // The message start string is designed to be unlikely to occur in normal data.
        // The characters are rarely used upper ASCII, not valid as UTF-8, and produce
        // a large 4-byte int at any alignment.
        memcpy(pchMessageStart, IniCfg().GetMagicNumber(MAIN_NET), sizeof(pchMessageStart));
        vAlertPubKey                       = ParseHex(IniCfg().GetAlertPkey(MAIN_NET));
        nDefaultPort                       = IniCfg().GetDefaultPort(MAIN_NET);
        nRPCPort                           = IniCfg().GetRPCPort(MAIN_NET);
        strDataDir                         = "main";
        nBlockIntervalPreStableCoinRelease = BLOCK_INTERVAL_PRE_STABLE_COIN_RELEASE;
        nBlockIntervalStableCoinRelease    = BLOCK_INTERVAL_STABLE_COIN_RELEASE;
        nContinuousCountBeforeFork         = CONTINUOUS_BLOCK_COUNT_BEFORE_FORK;
        nContinuousCountAfterFork          = CONTINUOUS_BLOCK_COUNT_AFTER_FORK;
        nFeatureForkHeight                 = IniCfg().GetFeatureForkHeight(MAIN_NET);
        nStableCoinGenesisHeight           = IniCfg().GetStableCoinGenesisHeight(MAIN_NET);
        nVer3ForkHeight                    = IniCfg().GetVer3ForkHeight(MAIN_NET);
        assert(CreateGenesisBlockRewardTx(genesis.vptx, MAIN_NET));
        assert(CreateGenesisDelegateTx(genesis.vptx, MAIN_NET));
        genesis.SetPrevBlockHash(uint256());
        genesis.SetMerkleRootHash(genesis.BuildMerkleTree());

        genesis.SetVersion(INIT_BLOCK_VERSION);
        genesis.SetTime(IniCfg().GetStartTimeInit(MAIN_NET));
        genesis.SetNonce(IniCfg().GetGenesisBlockNonce(MAIN_NET));
        genesis.SetFuelRate(INIT_FUEL_RATES);
        genesis.SetHeight(0);
        genesis.ClearSignature();
        genesisBlockHash = genesis.GetHash();

        // cout << "GetGenesisBlockHash: " << IniCfg().GetGenesisBlockHash(MAIN_NET).GetHex()
        //     << "\nacutal blockhash: " << genesisBlockHash.GetHex() << "\r\n";

        assert(genesisBlockHash == IniCfg().GetGenesisBlockHash(MAIN_NET));
        // assert(genesis.GetMerkleRootHash() == IniCfg().GetMerkleRootHash());

        vSeeds.push_back(CDNSSeedData("seed1.waykichain.net", "n1.waykichain.net"));
        vSeeds.push_back(CDNSSeedData("seed2.waykichain.net", "n2.waykichain.net"));

        base58Prefixes[PUBKEY_ADDRESS] = IniCfg().GetAddressPrefix(MAIN_NET, PUBKEY_ADDRESS);
        base58Prefixes[SCRIPT_ADDRESS] = IniCfg().GetAddressPrefix(MAIN_NET, SCRIPT_ADDRESS);
        base58Prefixes[SECRET_KEY]     = IniCfg().GetAddressPrefix(MAIN_NET, SECRET_KEY);
        base58Prefixes[EXT_PUBLIC_KEY] = IniCfg().GetAddressPrefix(MAIN_NET, EXT_PUBLIC_KEY);
        base58Prefixes[EXT_SECRET_KEY] = IniCfg().GetAddressPrefix(MAIN_NET, EXT_SECRET_KEY);

        // Convert the pnSeeds array into usable address objects.
        for (unsigned int i = 0; i < IniCfg().GetSeedNodeIP().size(); i++) {
            // It'll only connect to one or two seed nodes because once it connects,
            // it'll get a pile of addresses with newer timestamps.
            // Seed nodes are given a random 'last seen time' of between one and two
            // weeks ago.
            const int64_t nOneWeek = 7 * 24 * 60 * 60;
            struct in_addr ip;
            memcpy(&ip, &IniCfg().GetSeedNodeIP()[i], sizeof(ip));
            CAddress addr(CService(ip, GetDefaultPort()));
            addr.nTime = GetTime() - GetRand(nOneWeek) - nOneWeek;
            vFixedSeeds.push_back(addr);
        }
    }

    virtual const CBlock& GenesisBlock() const { return genesis; }
    virtual NET_TYPE NetworkID() const { return MAIN_NET; }
    virtual bool InitializeConfig() { return CBaseParams::InitializeConfig(); }
    virtual uint32_t GetBlockMaxNonce() const { return 1000; }
    virtual const vector<CAddress>& FixedSeeds() const { return vFixedSeeds; }
    virtual bool IsInFixedSeeds(CAddress& addr) {
        vector<CAddress>::iterator iterAddr = find(vFixedSeeds.begin(), vFixedSeeds.end(), addr);
        return iterAddr != vFixedSeeds.end();
    }

protected:
    CBlock genesis;
    vector<CAddress> vFixedSeeds;
};

class CTestNetParams: public CMainParams {
public:
    CTestNetParams() {
        // The message start string is designed to be unlikely to occur in normal data.
        // The characters are rarely used upper ASCII, not valid as UTF-8, and produce
        // a large 4-byte int at any alignment.
        memcpy(pchMessageStart, IniCfg().GetMagicNumber(TEST_NET), sizeof(pchMessageStart));
        vAlertPubKey             = ParseHex(IniCfg().GetAlertPkey(TEST_NET));
        nDefaultPort             = IniCfg().GetDefaultPort(TEST_NET);
        nRPCPort                 = IniCfg().GetRPCPort(TEST_NET);
        strDataDir               = "testnet";
        nFeatureForkHeight       = IniCfg().GetFeatureForkHeight(TEST_NET);
        nStableCoinGenesisHeight = IniCfg().GetStableCoinGenesisHeight(TEST_NET);
        nVer3ForkHeight          = IniCfg().GetVer3ForkHeight(TEST_NET);
        // Modify the testnet genesis block so the timestamp is valid for a later start.
        genesis.SetTime(IniCfg().GetStartTimeInit(TEST_NET));
        genesis.SetNonce(IniCfg().GetGenesisBlockNonce(TEST_NET));
        genesis.vptx.clear();
        assert(CreateGenesisBlockRewardTx(genesis.vptx, TEST_NET));
        assert(CreateGenesisDelegateTx(genesis.vptx, TEST_NET));
        genesis.SetMerkleRootHash(genesis.BuildMerkleTree());
        genesisBlockHash = genesis.GetHash();
        for (auto& item : vFixedSeeds)
            item.SetPort(GetDefaultPort());

        // cout << "GetGenesisBlockHash: " << IniCfg().GetGenesisBlockHash(TEST_NET).GetHex()
        //     << "\nacutal blockhash: " << genesisBlockHash.GetHex() << "\r\n";

        assert(genesisBlockHash == IniCfg().GetGenesisBlockHash(TEST_NET));
        vSeeds.push_back(CDNSSeedData("seed1.waykitest.net", "n1.waykitest.net"));
        vSeeds.push_back(CDNSSeedData("seed2.waykitest.net", "n2.waykitest.net"));

        base58Prefixes[PUBKEY_ADDRESS] = IniCfg().GetAddressPrefix(TEST_NET, PUBKEY_ADDRESS);
        base58Prefixes[SCRIPT_ADDRESS] = IniCfg().GetAddressPrefix(TEST_NET, SCRIPT_ADDRESS);
        base58Prefixes[SECRET_KEY]     = IniCfg().GetAddressPrefix(TEST_NET, SECRET_KEY);
        base58Prefixes[EXT_PUBLIC_KEY] = IniCfg().GetAddressPrefix(TEST_NET, EXT_PUBLIC_KEY);
        base58Prefixes[EXT_SECRET_KEY] = IniCfg().GetAddressPrefix(TEST_NET, EXT_SECRET_KEY);
    }

    virtual NET_TYPE NetworkID() const { return TEST_NET; }

    virtual bool InitializeConfig() {
        CMainParams::InitializeConfig();

        nStableCoinGenesisHeight = GetArg("-stablecoingenesisheight", IniCfg().GetStableCoinGenesisHeight(TEST_NET));
        nFeatureForkHeight       = std::max<uint32_t>(nStableCoinGenesisHeight + 1,
                                                GetArg("-featureforkheight", IniCfg().GetFeatureForkHeight(TEST_NET)));

        nVer3ForkHeight          = std::max<uint32_t>(nFeatureForkHeight + 1,
                                                GetArg("-ver3forkheight", IniCfg().GetVer3ForkHeight(TEST_NET)));

        fServer = true;

        return true;
    }

    virtual uint32_t GetBlockMaxNonce() const { return 1000; }
};

//
// Regression test
//
class CRegTestParams: public CTestNetParams {
public:
    CRegTestParams() {
        memcpy(pchMessageStart, IniCfg().GetMagicNumber(REGTEST_NET), sizeof(pchMessageStart));
        nDefaultPort             = IniCfg().GetDefaultPort(REGTEST_NET);
        strDataDir               = "regtest";
        nFeatureForkHeight       = IniCfg().GetFeatureForkHeight(REGTEST_NET);
        nStableCoinGenesisHeight = IniCfg().GetStableCoinGenesisHeight(REGTEST_NET);
        nVer3ForkHeight          = IniCfg().GetVer3ForkHeight(REGTEST_NET);
        genesis.SetTime(IniCfg().GetStartTimeInit(REGTEST_NET));
        genesis.SetNonce(IniCfg().GetGenesisBlockNonce(REGTEST_NET));
        genesis.vptx.clear();
        assert(CreateGenesisBlockRewardTx(genesis.vptx, REGTEST_NET));
        assert(CreateGenesisDelegateTx(genesis.vptx, REGTEST_NET));
        genesis.SetMerkleRootHash(genesis.BuildMerkleTree());
        genesisBlockHash = genesis.GetHash();
        assert(genesisBlockHash == IniCfg().GetGenesisBlockHash(REGTEST_NET));

        vFixedSeeds.clear();
        vSeeds.clear();  // Regtest mode doesn't have any DNS seeds.
    }

    virtual bool RequireRPCPassword() const { return false; }

    virtual NET_TYPE NetworkID() const { return REGTEST_NET; }

    virtual bool InitializeConfig() {
        CTestNetParams::InitializeConfig();

        nBlockIntervalPreStableCoinRelease =
            GetArg("-blockintervalprestablecoinrelease", BLOCK_INTERVAL_PRE_STABLE_COIN_RELEASE);
        nBlockIntervalStableCoinRelease = GetArg("-blockintervalstablecoinrelease", BLOCK_INTERVAL_STABLE_COIN_RELEASE);
        nStableCoinGenesisHeight = GetArg("-stablecoingenesisheight", IniCfg().GetStableCoinGenesisHeight(REGTEST_NET));
        nFeatureForkHeight       = std::max<uint32_t>(
            nStableCoinGenesisHeight + 1, GetArg("-featureforkheight", IniCfg().GetFeatureForkHeight(REGTEST_NET)));

        nVer3ForkHeight          = std::max<uint32_t>(nFeatureForkHeight + 1,
                                                GetArg("-ver3forkheight", IniCfg().GetVer3ForkHeight(REGTEST_NET)));
        fServer = true;

        return true;
    }
};

const vector<string>& CBaseParams::GetMultiArgs(const string& strArg) { return m_mapMultiArgs[strArg]; }
int CBaseParams::GetArgsSize() { return m_mapArgs.size(); }
int CBaseParams::GetMultiArgsSize() { return m_mapMultiArgs.size(); }

string CBaseParams::GetArg(const string& strArg, const string& strDefault) {
    if (m_mapArgs.count(strArg))
        return m_mapArgs[strArg];
    return strDefault;
}

int64_t CBaseParams::GetArg(const string& strArg, int64_t nDefault) {
    if (m_mapArgs.count(strArg))
        return atoi64(m_mapArgs[strArg]);
    return nDefault;
}

bool CBaseParams::GetBoolArg(const string& strArg, bool fDefault) {
    if (m_mapArgs.count(strArg)) {
        if (m_mapArgs[strArg].empty())
            return true;
        return (atoi(m_mapArgs[strArg]) != 0);
    }
    return fDefault;
}

bool CBaseParams::SoftSetArg(const string& strArg, const string& strValue) {
    if (m_mapArgs.count(strArg))
        return false;
    m_mapArgs[strArg] = strValue;
    return true;
}

bool CBaseParams::SoftSetArgCover(const string& strArg, const string& strValue) {
    m_mapArgs[strArg] = strValue;
    return true;
}

void CBaseParams::EraseArg(const string& strArgKey) {
    m_mapArgs.erase(strArgKey);
}

bool CBaseParams::SoftSetBoolArg(const string& strArg, bool fValue) {
    if (fValue)
        return SoftSetArg(strArg, string("1"));
    else
        return SoftSetArg(strArg, string("0"));
}

bool CBaseParams::IsArgCount(const string& strArg) {
    if (m_mapArgs.count(strArg)) {
        return true;
    }
    return false;
}

CBaseParams& SysCfg() {
    static shared_ptr<CBaseParams> pParams;

    if (!pParams.get()) {
        string netType = CBaseParams::GetArg("-nettype", "main");
        std::transform(netType.begin(), netType.end(), netType.begin(), ::tolower);

        if (netType == "main") {  // MAIN_NET
            pParams = std::make_shared<CMainParams>();
        } else if (netType == "test") {  // TEST_NET
            pParams = std::make_shared<CTestNetParams>();
        } else if (netType == "regtest") {  // REGTEST_NET
            pParams = std::make_shared<CRegTestParams>();
        } else {
            throw runtime_error("Given nettype not in (main|test|regtest) \n");
        }
    }

    assert(pParams.get());
    return *pParams.get();
}

//write for mainnet code
const CBaseParams &SysParamsMain() {
    static std::shared_ptr<CBaseParams> pParams;
    pParams = std::make_shared<CMainParams>();
    assert(pParams != NULL);
    return *pParams.get();
}

//write for testNet code
const CBaseParams &SysParamsTest() {
    static std::shared_ptr<CBaseParams> pParams;
    pParams = std::make_shared<CTestNetParams>();
    assert(pParams != NULL);
    return *pParams.get();
}

//write for RegTestNet code
const CBaseParams &SysParamsReg() {
    static std::shared_ptr<CBaseParams> pParams;
    pParams = std::make_shared<CRegTestParams>();
    assert(pParams != NULL);
    return *pParams.get();
}

void CBaseParams::ParseParameters(int argc, const char* const argv[]) {
    m_mapArgs.clear();
    m_mapMultiArgs.clear();
    for (int i = 1; i < argc; i++) {
        string str(argv[i]);
        string strValue;
        size_t is_index = str.find('=');
        if (is_index != string::npos) {
            strValue = str.substr(is_index + 1);
            str = str.substr(0, is_index);
        }
#ifdef WIN32
        boost::to_lower(str);
        if (boost::algorithm::starts_with(str, "/"))
            str = "-" + str.substr(1);
#endif
        if (str[0] != '-')
            break;

        m_mapArgs[str] = strValue;
        m_mapMultiArgs[str].push_back(strValue);
    }

    for (auto& entry : m_mapArgs) {
        string name = entry.first;

        //  interpret --foo as -foo (as long as both are not set)
        if (name.find("--") == 0) {
            string singleDash(name.begin() + 1, name.end());
            if (m_mapArgs.count(singleDash) == 0)
                m_mapArgs[singleDash] = entry.second;
            name = singleDash;
        }
    }
}

bool CBaseParams::CreateGenesisBlockRewardTx(vector<std::shared_ptr<CBaseTx> >& vptx, NET_TYPE type) {
    vector<string> vInitPubKey = IniCfg().GetInitPubKey(type);
    for (size_t i = 0; i < vInitPubKey.size(); ++i) {
        uint64_t reward = 0;
        if (i > 0) {
            reward = IniCfg().GetCoinInitValue() * COIN;
        }

        auto pRewardTx      = std::make_shared<CBlockRewardTx>(ParseHex(vInitPubKey[i]), reward, 0);
        pRewardTx->nVersion = INIT_TX_VERSION;
        vptx.push_back(pRewardTx);
    }

    return true;
};

bool CBaseParams::CreateGenesisDelegateTx(vector<std::shared_ptr<CBaseTx> > &vptx, NET_TYPE type) {
    vector<string> vDelegatePubKey = IniCfg().GetDelegatePubKey(type);
    vector<string> vInitPubKey = IniCfg().GetInitPubKey(type);
    vector<CCandidateVote> votes;
    uint64_t bcoinsToVote = IniCfg().GetCoinInitValue() * COIN  / 100;

    for (size_t i = 0; i < vDelegatePubKey.size(); ++i) {
        CUserID voteId(CPubKey(ParseHex(vDelegatePubKey[i])));
        CCandidateVote vote(ADD_BCOIN, voteId, bcoinsToVote);
        votes.push_back(vote);
    }

    CRegID regId(0, 1);
    auto pDelegateTx       = std::make_shared<CDelegateVoteTx>(regId, votes, 10000, 0);
    pDelegateTx->signature = ParseHex(IniCfg().GetDelegateSignature(type));
    pDelegateTx->nVersion  = INIT_TX_VERSION;

    vptx.push_back(pDelegateTx);

    return true;
}

bool CBaseParams::CreateFundCoinRewardTx(vector<std::shared_ptr<CBaseTx> >& vptx, NET_TYPE type) {
    // Stablecoin Global Reserve Account with its initial reseve creation
    auto pTx      = std::make_shared<CCoinRewardTx>(CNullID(), nStableCoinGenesisHeight, SYMB::WUSD,
                                               FUND_COIN_GENESIS_INITIAL_RESERVE_AMOUNT * COIN);
    pTx->nVersion = INIT_TX_VERSION;
    vptx.push_back(pTx);

    // FundCoin Genesis Account with the total FundCoin release creation
    pTx = std::make_shared<CCoinRewardTx>(CPubKey(ParseHex(IniCfg().GetInitFcoinOwnerPubKey(type))),
                                          nStableCoinGenesisHeight, SYMB::WGRT,
                                          FUND_COIN_GENESIS_TOTAL_RELEASE_AMOUNT * COIN);
    vptx.push_back(pTx);

    // DEX Order Matching Service Account
    pTx = std::make_shared<CCoinRewardTx>(CPubKey(ParseHex(IniCfg().GetDexMatchServicePubKey(type))),
                                          nStableCoinGenesisHeight, SYMB::WGRT, 0);
    vptx.push_back(pTx);

    return true;
}

bool CBaseParams::InitializeParams(int argc, const char* const argv[]) {
    ParseParameters(argc, argv);
    if (!boost::filesystem::is_directory(GetDataDir(false))) {
        fprintf(stderr, "Error: Specified data directory \"%s\" does not exist.\n",
                CBaseParams::m_mapArgs["-datadir"].c_str());
        return false;
    }

    try {
        ReadConfigFile(CBaseParams::m_mapArgs, CBaseParams::m_mapMultiArgs);
    } catch (std::exception &e) {
        fprintf(stderr, "Error: reading configuration file: %s\n", e.what());
        return false;
    }

    return true;
}

CBaseParams::CBaseParams() {
    fImporting              = false;
    fReindex                = false;
    fBenchmark              = false;
    fTxIndex                = false;
    fLogFailures            = false;
    nTxCacheHeight          = 500;
    nTimeBestReceived       = 0;
    nCacheSize              = 300 << 10;  // 300K bytes
    nDefaultPort            = 0;
    fServer                 = 0;
    fServer                 = 0;
    nRPCPort                = 0;
    nMaxForkTime            = 24 * 60 * 60;  // 86400 seconds
}

int32_t CBaseParams::GetMaxForkHeight(int32_t currBlockHeight) const {
    uint32_t interval = GetBlockInterval(currBlockHeight);
    if (interval != 0)
        return nMaxForkTime / (uint32_t)interval;

    return 0 ;
}
