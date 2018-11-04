# PREPARATION

## Launch token
* create account for token contract, example: `msysreserve`, with at least 400KiB of RAM, stake 5 EOS on bandwidth, 15 EOS on CPU
* compile ABI, WAST and WASM for token, set contract:
```
cd contracts/msys.token
eosiocpp -g msys.token.abi msys.token.hpp
eosiocpp -o msys.token.wast msys.token.cpp
cleos set contract msysreserve ./ msys.token.wast msys.token.abi -p msysreserve
```
* create token, `cleos push action msysreserve create '["msysreserve", "428000000.0000 msys"]' -p msysreserve`

## Launch presale
* create account for presale contract, example: `msyspresale`, with at least 400KiB of RAM, stake 5 EOS on bandwidth, 15 EOS on CPU
* set `msyspresale@eosio.code` permission to `msyspresale@active`: `cleos set account permission msyspresale active '{"threshold": 1,"keys": [{"key": "CURRENT_PUBLIC_KEY_for_msyspresale@active","weight": 1}],"accounts": [{"permission":{"actor": "msyspresale","permission":"eosio.code"},"weight":1}]}' owner -p msyspresale`
* in `msyspresale.hpp` adjust configurable constants:
```cpp
  const time start = 1532433600; // Tue Jul 24 2018 12:00:00 GMT+0000 //1532433600
  const time end = 1537790400;   // Tue Sep 24 2018 12:00:00 GMT+0000 //1537790400

  const uint64_t softcap_i = 38400000000;
  const uint64_t hardcap_i = 192000000000;

  const asset softcap = asset(softcap_i, S(4, msys));
  const asset hardcap = asset(hardcap_i, S(4, msys));

  const uint8_t bonus[2] = {50, 25};
  const asset bonus_thr[2] = {asset(19200000000, S(4, msys)), asset(38400000000, S(4, msys))};

  const account_name tokencontract = N(msysreserve);
```
* compile ABI, WAST and WASM for token, set contract:
```
cd contracts/msyspresale
eosiocpp -g msyspresale.abi msyspresale.hpp
eosiocpp -o msyspresale.wast msyspresale.cpp
cleos set contract msyspresale ./ msyspresale.wast msyspresale.abi -p msyspresale
```
* issue tokens to self: `cleos push action msysreserve issue '["msyspresale", "20640000.0000 msys", "for round A fundraising distribution"]' -p msysreserve`
* set self as active distributor: `cleos push action msysreserve setdistrib '["0.0000 msys", "msyspresale"]' -p msysreserve`
* set rates, `cleos push action msysreserve updaterate '[0, 580000]" -p msysreserve` is for example for EOS, 1 = EUR, 2 = USD, 3 = ETH. Set rates for all networks. 
P.S. Due to `uint32_t` limitation max rate is capped at 429496.7295, beware selling for bitcoin once it moons.

# TEST CASE

We assume user is `msysjungles`
* apply as user: `cleos push action msyspresale apply '["msysjungles", 1] -p msysjungles`
* approve user in whitelist: `cleos push action msyspresale approve '["msysjungles"]' -p msysreserve`
* buy some msys as user: `cleos push action msyspresale buymsys '["msysjungles", 0, "1000.0000 EOS", "testmemo"]' -p msysjungles`
* transfer some EOS as user: `cleos transfer msysjungles msyspresale "1000.0000 EOS" "testmemo" -p msysjungles`
* check the entry in contributions table, you need ID: `cleos get table msyspresale msyspresale contribution --index 2 --key-type name -L "msysjungles" -U "msysjunglet"`
* before transaction is validated, you can delete it with `deletetx` action, id is the argument.
* validate transaction: `cleos push action msyspresale validate '[0, "testmemo", ""]' -p msyspresale`
* once softcap is reached, you can distribute: `cleos push action msyspresale distribute '[0]' -p msyspresale`
* refund is done by `cleos push action msyspresale refund '[0, ""]' -p msyspresale`
* once all tokens are distributed and all EOS/money refunded for oversale, or softcap not reached before the `end`, you can do finalization.

