# Version
* 2019-09-10
* v2.0.0

# Run in docker
Run greenventureschain coin inside a docker container!

## Install Dependencies
  * Docker 17.05 or higher is required
## Docker Environment Requirement
  * At least 4GB RAM (Docker -> Preferences -> Advanced -> Memory -> 4GB or above)
  * If the build below fails, make sure you've adjusted Docker Memory settings and try again.

## Build greenventurescoin docker image
### method-1: build from Dockerfile
1. ```git clone https://github.com/GreenVentures/GreenVenturesChain.git```
1. ```cd GreenVenturesChain/Docker && sh ./bin/build-greenventureschain.sh```

### method-2: pull from Docker Hub without build
``` docker pull greenventures/greenventureschain ```

## Run GreenVenturesChain Docker container
1. create a host dir to keep container data (you are free to choose your own preferred dir path)
   * For mainnet: ``` sudo mkdir -p /opt/docker-instances/greenventurescoin-main ```
   * For testnet: ``` sudo mkdir -p /opt/docker-instances/greenventurescoin-test ```
1. first, cd into the above created node host dir and create ```data``` and ```conf``` subdirs:
   * ``` sudo mkdir data conf ```
1. copy the entire Docker/bin dir from GreenVenturesChain repository:
   * ``` sudo cp -r ${your_path_of_GreenVentureschain}/Docker/bin ./ ```
1. copy WaykiCoind.conf into ```conf``` dir from GreenVenturesChain repository:
   * ``` sudo cp -r ${your_path_of_GreenVenturesChain}/Docker/GreenVenturesChain.conf ./conf/ ```
1. modify content of ```GreenVenturesCoin.conf``` accordingly
   * For mainnet, please make sure ```nettype=main``` is set
   * For testnet, please make sure only ```nettype=test``` is set
   * For regtest, please make suer only ```nettype=regtest``` is set
   * For common nodes (no mining), please set ```gen=0``` to avoid computing resources waste
1. launch the node container:
   * For mainnet, run ```$sh ./bin/run-greenventurescoin-main.sh```
   * For testnet,  run ```$sh ./bin/run-greenventurescoin-test.sh```

## Lookup Help menu from coin
* ```docker exec -it greenventurescoin-test coin help```

## Stop coin (in a graceful way)
* ```docker exec -it greenventurescoin-test coin stop```

## Test
* ```$docker exec -it greenventurescoin-test coin getpeerinfo```
* ```$docker exec -it greenventurescoin-test coin getinfo```

## Q&A

|Q | A|
|--|--|
|How to modify JSON RPC port | Two options: <br> <li>modify [GreenVenturesChain.conf](https://github.com/GreenVentures/GreenVenturesChain/wiki/GreenVenturesChain.conf) (```rpcport=6968```)<li>modify docker container mapping port |
|How to run a testnet | modify GreenVenturesChain.conf by adding ```testnet=test```,  |
|How to run a regtest | modify GreenVenturesChain.conf by adding ```regtest=regtest```, |
|How to run a mainnet | modify GreenVenturesChain.conf by adding ```regtest=main```,  |
