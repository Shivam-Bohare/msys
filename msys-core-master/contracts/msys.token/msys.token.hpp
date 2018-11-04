#pragma once

#include <eosiolib/asset.hpp>
#include <eosiolib/eosio.hpp>

#include <string>

#define NETWORK_EOS 0
#define NETWORK_EUR 1
#define NETWORK_USD 2
#define NETWORK_ETH 3

namespace eosiosystem
{
class system_contract;
}

namespace eosio
{

using std::string;

class token : public contract
{
public:
  token(account_name self) : contract(self) {}

  // @abi action
  void create(account_name issuer,
              asset maximum_supply);

  // @abi action
  void issue(account_name to, asset quantity, string memo);

  // @abi action
  void transfer(account_name from,
                account_name to,
                asset quantity,
                string memo);

  // @abi action
  void setgrunlock(uint64_t date, uint8_t percent);

  // @abi action
  void launchlock(account_name to, asset quantity);

  // @abi action
  void gradlock(account_name to, asset quantity);

  // @abi action
  void distribsale(account_name from, account_name to, asset quantity, string memo);

  // @abi action
  void distribcontr(account_name from, account_name to, asset quantity, string memo);

  // @abi action
  void setdistrib(asset currency, account_name distributor);

  // @abi action
  void updaterate(uint8_t network, uint32_t rate);

  inline asset get_supply(symbol_name sym) const;
  inline asset get_balance(account_name owner, symbol_name sym) const;

  /// @abi table exrate i64
  struct exrate
  {
    uint8_t network;
    uint32_t rate;
    uint64_t updated;

    uint8_t primary_key() const { return network; }

    EOSLIB_SERIALIZE(exrate, (network)(rate)(updated))
  };

  typedef eosio::multi_index<N(exrate), exrate> exrates;

private:
  /// @abi table accounts i64
  struct account
  {
    asset balance;

    uint64_t primary_key() const { return balance.symbol.name(); }

    EOSLIB_SERIALIZE(account, (balance))
  };

  typedef eosio::multi_index<N(accounts), account> accounts;

  /// @abi table stat i64
  struct stat
  {
    asset supply;
    asset max_supply;
    account_name issuer;
    account_name distributor;

    uint64_t primary_key() const { return supply.symbol.name(); }

    EOSLIB_SERIALIZE(stat, (supply)(max_supply)(issuer)(distributor))
  };

  typedef eosio::multi_index<N(stat), stat> stats;

  /// @abi table gradunlock i64
  struct gradunlock
  {
    uint64_t locked_until;
    uint8_t lock_threshold;

    uint64_t primary_key() const { return locked_until; }

    EOSLIB_SERIALIZE(gradunlock, (locked_until)(lock_threshold))
  };

  typedef eosio::multi_index<N(gradunlock), gradunlock> gradunlocks;

  /// @abi table transferlock i64
  struct transferlock
  {
    uint64_t id;
    account_name account;
    uint64_t locked_balance;
    uint64_t locked_until;

    uint64_t primary_key() const { return id; }

    account_name get_account() const { return account; }

    EOSLIB_SERIALIZE(transferlock, (id)(account)(locked_balance)(locked_until))
  };

  typedef eosio::multi_index<N(transferlock), transferlock,
                             indexed_by<N(acc),
                                        const_mem_fun<transferlock, uint64_t, &transferlock::get_account>>>
      transferlocks;

  // PRIVATE UTILITY FUNCTIONS
  void launch_lock(account_name to, asset quantity, uint64_t launch_date);
  void gradual_lock(account_name to, asset quantity);
  void sub_balance(account_name owner, asset value);
  void add_balance(account_name owner, asset value, account_name ram_payer);
  void check_distributor_and_asset(const asset& quantity) const;

public:
  struct transfer_args
  {
    account_name from;
    account_name to;
    asset quantity;
    string memo;
  };
}; // class token.

asset token::get_supply(symbol_name sym) const
{
  stats statstable(_self, sym);
  const auto &st = statstable.get(sym);
  return st.supply;
}

asset token::get_balance(account_name owner, symbol_name sym) const
{
  accounts accountstable(_self, owner);
  const auto &ac = accountstable.get(sym);
  return ac.balance;
}

} // namespace eosio
