// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2017-2019 The GreenVenturesChain Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CONFIG_CDPPARAMS_H
#define CONFIG_CDPPARAMS_H

#include <cstdint>
#include <unordered_map>
#include <string>
#include <cstdint>
#include <tuple>
#include "const.h"

using namespace std;

enum SysParamType : uint8_t {
    NULL_SYS_PARAM_TYPE                     = 0,
    MEDIAN_PRICE_SLIDE_WINDOW_BLOCKCOUNT    = 1,
    PRICE_FEED_BCOIN_STAKE_AMOUNT_MIN       = 2,
    PRICE_FEED_CONTINUOUS_DEVIATE_TIMES_MAX = 3,
    PRICE_FEED_DEVIATE_RATIO_MAX            = 4,
    PRICE_FEED_DEVIATE_PENALTY              = 5,
    DEX_DEAL_FEE_RATIO                      = 7,
    ASSET_ISSUE_FEE                         = 19,
    ASSET_UPDATE_FEE                        = 20,
    DEX_OPERATOR_REGISTER_FEE               = 21,
    DEX_OPERATOR_UPDATE_FEE                 = 22,
    PROPOSAL_EXPIRE_BLOCK_COUNT             = 23,
    TOTAL_DELEGATE_COUNT                    = 24,
    TRANSFER_SCOIN_RESERVE_FEE_RATIO        = 25,
    ASSET_RISK_FEE_RATIO                    = 26,
    DEX_OPERATOR_RISK_FEE_RATIO             = 27

};

static const unordered_map<string, SysParamType> paramNameToSysParamTypeMap = {
        {"MEDIAN_PRICE_SLIDE_WINDOW_BLOCKCOUNT",           MEDIAN_PRICE_SLIDE_WINDOW_BLOCKCOUNT    },
        {"PRICE_FEED_BCOIN_STAKE_AMOUNT_MIN",              PRICE_FEED_BCOIN_STAKE_AMOUNT_MIN       },
        {"PRICE_FEED_CONTINUOUS_DEVIATE_TIMES_MAX",        PRICE_FEED_CONTINUOUS_DEVIATE_TIMES_MAX },
        {"PRICE_FEED_DEVIATE_RATIO_MAX",                   PRICE_FEED_DEVIATE_RATIO_MAX            },
        {"PRICE_FEED_DEVIATE_PENALTY",                     PRICE_FEED_DEVIATE_PENALTY              },
        {"DEX_DEAL_FEE_RATIO",                             DEX_DEAL_FEE_RATIO                      },
        {"ASSET_ISSUE_FEE",                                ASSET_ISSUE_FEE                         },
        {"ASSET_UPDATE_FEE",                               ASSET_UPDATE_FEE                        },
        {"DEX_OPERATOR_REGISTER_FEE",                      DEX_OPERATOR_REGISTER_FEE               },
        {"DEX_OPERATOR_UPDATE_FEE",                        DEX_OPERATOR_UPDATE_FEE                 },
        {"PROPOSAL_EXPIRE_BLOCK_COUNT",                    PROPOSAL_EXPIRE_BLOCK_COUNT             },
        {"TOTAL_DELEGATE_COUNT",                           TOTAL_DELEGATE_COUNT                    },
        {"TRANSFER_SCOIN_RESERVE_FEE_RATIO",               TRANSFER_SCOIN_RESERVE_FEE_RATIO        },
        {"ASSET_RISK_FEE_RATIO",                           ASSET_RISK_FEE_RATIO                    },
        {"DEX_OPERATOR_RISK_FEE_RATIO",                    DEX_OPERATOR_RISK_FEE_RATIO             },
};

struct SysParamTypeHash {
    size_t operator()(const SysParamType &type) const noexcept {
        return std::hash<uint8_t>{}(type);
    }
};

static const unordered_map<SysParamType, std::tuple< uint64_t,string >, SysParamTypeHash> SysParamTable = {
        { MEDIAN_PRICE_SLIDE_WINDOW_BLOCKCOUNT,     make_tuple( 11,           "MEDIAN_PRICE_SLIDE_WINDOW_BLOCKCOUNT")    },
        { PRICE_FEED_BCOIN_STAKE_AMOUNT_MIN,        make_tuple( 210000,       "PRICE_FEED_BCOIN_STAKE_AMOUNT_MIN")       },  // 1%: min 210K bcoins staked to be a price feeder for miner
        { PRICE_FEED_CONTINUOUS_DEVIATE_TIMES_MAX,  make_tuple( 10,           "PRICE_FEED_CONTINUOUS_DEVIATE_TIMES_MAX") },  // after 10 times continuous deviate limit penetration all deposit be deducted
        { PRICE_FEED_DEVIATE_RATIO_MAX,             make_tuple( 3000,         "PRICE_FEED_DEVIATE_RATIO_MAX")            },  // must be < 30% * 10000, otherwise penalized
        { PRICE_FEED_DEVIATE_PENALTY,               make_tuple( 1000,         "PRICE_FEED_DEVIATE_PENALTY")              },  // deduct 1000 staked bcoins as penalty
        { DEX_DEAL_FEE_RATIO,                       make_tuple( 40000,        "DEX_DEAL_FEE_RATIO")                      },  // 0.04% * 100000000
        { ASSET_ISSUE_FEE,                          make_tuple( 550 * COIN,   "ASSET_ISSUE_FEE")                         },  // asset issuance fee = 550 GVC
        { ASSET_UPDATE_FEE,                         make_tuple( 110 * COIN,   "ASSET_UPDATE_FEE")                        },  // asset update fee = 110 GVC
        { DEX_OPERATOR_REGISTER_FEE,                make_tuple( 1100 * COIN,  "DEX_OPERATOR_REGISTER_FEE")               }, // dex operator register fee = 1100 GVC
        { DEX_OPERATOR_UPDATE_FEE,                  make_tuple( 110 * COIN,   "DEX_OPERATOR_UPDATE_FEE")                 },  // dex operator update fee = 110 GVC
        { PROPOSAL_EXPIRE_BLOCK_COUNT,              make_tuple( 1200,         "PROPOSAL_EXPIRE_BLOCK_COUNT")             },   //
        { TOTAL_DELEGATE_COUNT,                     make_tuple( 11,           "TOTAL_DELEGATE_COUNT")                    },
        { TRANSFER_SCOIN_RESERVE_FEE_RATIO,         make_tuple( 0,            "TRANSFER_SCOIN_RESERVE_FEE_RATIO")        },  // WUSD friction fee to risk reserve
        { ASSET_RISK_FEE_RATIO,                     make_tuple( 4000,         "ASSET_RISK_FEE_RATIO")                    },
        { DEX_OPERATOR_RISK_FEE_RATIO,              make_tuple( 4000,         "DEX_OPERATOR_RISK_FEE_RATIO")             }

};

static const unordered_map<SysParamType, std::pair<uint64_t, uint64_t>, SysParamTypeHash> sysParamScopeTable = {
        { MEDIAN_PRICE_SLIDE_WINDOW_BLOCKCOUNT,      RANGE(0,0)        },
        { PRICE_FEED_BCOIN_STAKE_AMOUNT_MIN,         RANGE(0,0)        },  // 1%: min 210K bcoins staked to be a price feeder for miner
        { PRICE_FEED_CONTINUOUS_DEVIATE_TIMES_MAX,   RANGE(0,0)        },  // after 10 times continuous deviate limit penetration all deposit be deducted
        { PRICE_FEED_DEVIATE_RATIO_MAX,              RANGE(0,0)        },  // must be < 30% * 10000, otherwise penalized
        { PRICE_FEED_DEVIATE_PENALTY,                RANGE(0,0)        },  // deduct 1000 staked bcoins as penalty
        { DEX_DEAL_FEE_RATIO,                        RANGE(0,0)        },  // 0.04% * 100000000
        { ASSET_ISSUE_FEE,                           RANGE(0,0)        },  // asset issuance fee = 550 GVC
        { ASSET_UPDATE_FEE,                          RANGE(0,0)        },  // asset update fee = 110 GVC
        { DEX_OPERATOR_REGISTER_FEE,                 RANGE(0,0)        },  // dex operator register fee = 1100 GVC
        { DEX_OPERATOR_UPDATE_FEE,                   RANGE(0,0)        },  // dex operator update fee = 110 GVC
        { PROPOSAL_EXPIRE_BLOCK_COUNT,               RANGE(0,0)        },  //
        { TOTAL_DELEGATE_COUNT,                      RANGE(0,0)        },
        { TRANSFER_SCOIN_RESERVE_FEE_RATIO,          RANGE(0,0)        },  // WUSD friction fee to risk reserve
        { ASSET_RISK_FEE_RATIO,                      RANGE(0,10000)    },
        { DEX_OPERATOR_RISK_FEE_RATIO,               RANGE(0,10000)    }

};

inline string CheckSysParamValue(const SysParamType paramType, uint64_t value){
    if(sysParamScopeTable.count(paramType) == 0)
        return strprintf("check param scope error:don't find param type (%d)", paramType);
    auto itr = sysParamScopeTable.find(paramType) ;

    auto min = std::get<0>(itr->second);
    auto max = std::get<1>(itr->second);
    if(min ==0 && max == 0)
        return EMPTY_STRING;
    if( value < min || value >max)
        return strprintf("check param scope error: the scope "
                         "is [%d,%d],but the value you submited is %d", min,max,value );
    return EMPTY_STRING;

}

inline SysParamType  GetSysParamType(const string  paramName){
    auto itr = paramNameToSysParamTypeMap.find(paramName);
    return itr == paramNameToSysParamTypeMap.end()? NULL_SYS_PARAM_TYPE: itr->second;

}


#endif
