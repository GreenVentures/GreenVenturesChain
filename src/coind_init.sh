./coin -datadir=/home/xiaoyu/src/data/ importprivkey Y5F2GraTdQqMbYrV6MG78Kbg4QE8p4B2DyxMdLMH7HmDNtiNmcbM
./coin -datadir=/home/xiaoyu/src/data/ importprivkey Y7HWKeTHFnCxyTMtCEE6tVkqBzXoN1Yjxcx5Rs8j2dsSSvPxvF7p
./coin -datadir=/home/xiaoyu/src/data/ importprivkey Y871eB5Xiss2ugKWQRb4nmMhKTnmXAEyUqBimTCupogzoSTVCSU9
./coin -datadir=/home/xiaoyu/src/data/ importprivkey Y9cAUsEhfsihbePnCYYCETpN1PVovqTMX4kauKRsZ9ERdz1uumeK
./coin -datadir=/home/xiaoyu/src/data/ importprivkey Y4unEjiFk1YJQi1jaT3deY4t9Hm1eSk9usCam35LcN85cUA2QmZ5
./coin -datadir=/home/xiaoyu/src/data/ importprivkey Y5XKsR95ymf2pEyuhDPLtuvioHRo6ogDDNnaf4YU91ABvLb68QBU
./coin -datadir=/home/xiaoyu/src/data/ importprivkey Y7diE8BXuwTkjSzgdZMnKNhzYGrU8oSk31anJ1mwipSCcnPakzTA
./coin -datadir=/home/xiaoyu/src/data/ importprivkey YCjoCrtGEvMPZDLzBoY9GP3r7pqWa5mgzUxqAsVub6xnUVBwQHxE
./coin -datadir=/home/xiaoyu/src/data/ importprivkey Y6bKBN4ZKBNHJZpQpqE7y7TC1QpdT32YtAjw4Me9Bvgo47b5ivPY
./coin -datadir=/home/xiaoyu/src/data/ importprivkey Y8G5MwTFVsqj1FvkqFDEENzUBn4yu4Ds83HkeSYP9SkjLba7xQFX
./coin -datadir=/home/xiaoyu/src/data/ importprivkey YAq1NTUKiYPhV9wq3xBNCxYZfjGPMtZpEPA4sEoXPU1pppdjSAka
./coin -datadir=/home/xiaoyu/src/data/ importprivkey Y6J4aK6Wcs4A3Ex4HXdfjJ6ZsHpNZfjaS4B9w7xqEnmFEYMqQd13
./coin -datadir=/home/xiaoyu/src/data/ importprivkey Y8wZxK1UDo5oZmZ9hwhXpNb3Q9WRuy61Dy39C4m2GTsFWyfjMnXk
./coin -datadir=/home/xiaoyu/src/data/ importprivkey Y6tGgVufC3o7BP83Q9y83Rk8D9KDwFUjTCYshghzXTAzCCCMCUeR


## send money to miners
./coin -datadir=/home/xiaoyu/src/data/ submitsendtx 0-1 0-3 220000000000000 100000000
./coin -datadir=/home/xiaoyu/src/data/ submitsendtx 0-1 0-4 220000000000000 100000000
./coin -datadir=/home/xiaoyu/src/data/ submitsendtx 0-1 0-5 220000000000000 100000000
./coin -datadir=/home/xiaoyu/src/data/ submitsendtx 0-1 0-6 220000000000000 100000000
./coin -datadir=/home/xiaoyu/src/data/ submitsendtx 0-1 0-7 220000000000000 100000000
./coin -datadir=/home/xiaoyu/src/data/ submitsendtx 0-1 0-8 220000000000000 100000000
./coin -datadir=/home/xiaoyu/src/data/ submitsendtx 0-1 0-9 220000000000000 100000000
./coin -datadir=/home/xiaoyu/src/data/ submitsendtx 0-1 0-10 220000000000000 100000000
./coin -datadir=/home/xiaoyu/src/data/ submitsendtx 0-1 0-11 220000000000000 100000000
./coin -datadir=/home/xiaoyu/src/data/ submitsendtx 0-1 0-12 220000000000000 100000000

./coin -datadir=/home/xiaoyu/src/data/ setgenerate true 1
sleep 2.5

## miners delegate to self
./coin -datadir=/home/xiaoyu/src/data/ submitdelegatevotetx "0-3" '[{"delegate":"0-3", "votes":210000000000000}]' 100000000
./coin -datadir=/home/xiaoyu/src/data/ submitdelegatevotetx "0-4" '[{"delegate":"0-4", "votes":210000000000000}]' 100000000
./coin -datadir=/home/xiaoyu/src/data/ submitdelegatevotetx "0-5" '[{"delegate":"0-5", "votes":210000000000000}]' 100000000
./coin -datadir=/home/xiaoyu/src/data/ submitdelegatevotetx "0-6" '[{"delegate":"0-6", "votes":210000000000000}]' 100000000
./coin -datadir=/home/xiaoyu/src/data/ submitdelegatevotetx "0-7" '[{"delegate":"0-7", "votes":210000000000000}]' 100000000
./coin -datadir=/home/xiaoyu/src/data/ submitdelegatevotetx "0-8" '[{"delegate":"0-8", "votes":210000000000000}]' 100000000
./coin -datadir=/home/xiaoyu/src/data/ submitdelegatevotetx "0-9" '[{"delegate":"0-9", "votes":210000000000000}]' 100000000
./coin -datadir=/home/xiaoyu/src/data/ submitdelegatevotetx "0-10" '[{"delegate":"0-10", "votes":210000000000000}]' 100000000
./coin -datadir=/home/xiaoyu/src/data/ submitdelegatevotetx "0-11" '[{"delegate":"0-11", "votes":210000000000000}]' 100000000
./coin -datadir=/home/xiaoyu/src/data/ submitdelegatevotetx "0-12" '[{"delegate":"0-12", "votes":210000000000000}]' 100000000

## 0-1 undelegate miners except 0-2
./coin -datadir=/home/xiaoyu/src/data/ submitdelegatevotetx "0-1" '[
{"delegate":"0-3", "votes":-210000000000000},
{"delegate":"0-4", "votes":-210000000000000},
{"delegate":"0-5", "votes":-210000000000000},
{"delegate":"0-6", "votes":-210000000000000},
{"delegate":"0-7", "votes":-210000000000000},
{"delegate":"0-8", "votes":-210000000000000},
{"delegate":"0-9", "votes":-210000000000000},
{"delegate":"0-10", "votes":-210000000000000},
{"delegate":"0-11", "votes":-210000000000000},
{"delegate":"0-12", "votes":-210000000000000}
]' 100000000

./coin -datadir=/home/xiaoyu/src/data/ submitsendtx 0-1 waem4ZAtWToVZ1dD3QkbrSJgigQ7CNtUrN 220000000000000 100000000
./coin -datadir=/home/xiaoyu/src/data/ submitsendtx 0-1 wghDEbNXMBhpdBoETMHDT5rb9pkdAbrNSB 220000000000000 100000000
./coin -datadir=/home/xiaoyu/src/data/ setgenerate true 1
./coin -datadir=/home/xiaoyu/src/data/ submitaccountregistertx waem4ZAtWToVZ1dD3QkbrSJgigQ7CNtUrN
./coin -datadir=/home/xiaoyu/src/data/ submitaccountregistertx wghDEbNXMBhpdBoETMHDT5rb9pkdAbrNSB

sleep 2.5

## mine to scoin enable
./coin -datadir=/home/xiaoyu/src/data/ setgenerate true 6

echo "wait for 13s ..." && sleep 13
./coin -datadir=/home/xiaoyu/src/data/ getinfo

## give mined fee to 0-2
./coin -datadir=/home/xiaoyu/src/data/ submitsendtx "0-1" "0-2" 10000010000000 100000000
## give mined fee to 8-2
./coin -datadir=/home/xiaoyu/src/data/ submitsendtx "0-1" "8-2" 10000010000000 100000000
./coin -datadir=/home/xiaoyu/src/data/ setgenerate true 1

sleep 2.5
## give WGRT to 0-2 from 8-2
./coin -datadir=/home/xiaoyu/src/data/ submitsendtx "8-2" "0-2" "WGRT:21000000000000" 100000000
./coin -datadir=/home/xiaoyu/src/data/ setgenerate true 1

sleep 2.5

## 0-2 stake fcoin for price feeding
./coin -datadir=/home/xiaoyu/src/data/ submitfcoinstaketx "0-2" 21000000000000
./coin -datadir=/home/xiaoyu/src/data/ setgenerate true 1

sleep 2.5

./coin -datadir=/home/xiaoyu/src/data/ submitpricefeedtx "0-2" '[{"coin":"GVC","currency":"USD","price":10000},{"coin":"WGRT","currency":"USD","price":10000}]'
./coin -datadir=/home/xiaoyu/src/data/ setgenerate true 1

sleep 2.5

./coin -datadir=/home/xiaoyu/src/data/ getscoininfo

echo "Init finish!!!"
