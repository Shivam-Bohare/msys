#include "msys.token.hpp"
#include <iostream>

namespace eosio
{

void token::create(account_name issuer,
                   asset maximum_supply)
{
    require_auth(_self);

    auto sym = maximum_supply.symbol;
    eosio_assert(sym.is_valid(), "invalid symbol name");
    eosio_assert(maximum_supply.is_valid(), "invalid supply");
    eosio_assert(maximum_supply.amount > 0, "max-supply must be positive");

    stats statstable(_self, sym.name());
    auto existing = statstable.find(sym.name());
    eosio_assert(existing == statstable.end(), "token with symbol already exists");

    statstable.emplace(_self, [&](auto &s) {
        s.supply.symbol = maximum_supply.symbol;
        s.max_supply = maximum_supply;
        s.issuer = issuer;
        s.distributor = 0;
    });
}

void token::issue(account_name to, asset quantity, string memo)
{
    auto sym = quantity.symbol;
    eosio_assert(sym.is_valid(), "invalid symbol name");
    eosio_assert(memo.size() <= 256, "memo has more than 256 bytes");

    auto sym_name = sym.name();
    stats statstable(_self, sym_name);
    auto existing = statstable.find(sym_name);
    eosio_assert(existing != statstable.end(), "token with symbol does not exist, create token before issue");
    const auto &st = *existing;

    require_auth(st.issuer);
    eosio_assert(quantity.is_valid(), "invalid quantity");
    eosio_assert(quantity.amount > 0, "must issue positive quantity");

    eosio_assert(quantity.symbol == st.supply.symbol, "symbol precision mismatch");
    eosio_assert(quantity.amount <= st.max_supply.amount - st.supply.amount, "quantity exceeds available supply");

    statstable.modify(st, 0, [&](auto &s) {
        s.supply += quantity;
    });

    add_balance(st.issuer, quantity, st.issuer);

    if (to != st.issuer)
    {
        SEND_INLINE_ACTION(*this, transfer, {st.issuer, N(active)}, {st.issuer, to, quantity, memo});
    }
}

void token::transfer(account_name from,
                     account_name to,
                     asset quantity,
                     string memo)
{
    eosio_assert(from != to, "cannot transfer to self");
    require_auth(from);
    eosio_assert(is_account(to), "to account does not exist");
    auto sym = quantity.symbol.name();
    stats statstable(_self, sym);
    const auto &st = statstable.get(sym);

    require_recipient(from);
    require_recipient(to);

    eosio_assert(quantity.is_valid(), "invalid quantity");
    eosio_assert(quantity.amount > 0, "must transfer positive quantity");
    eosio_assert(quantity.symbol == st.supply.symbol, "symbol precision mismatch");
    eosio_assert(memo.size() <= 256, "memo has more than 256 bytes");

    // transfer lock check
    transferlocks transfer_lock_table(_self, _self);
    auto accidx = transfer_lock_table.get_index<N(acc)>();
    auto itr = accidx.lower_bound(from);

    time current_time = now();
    
    for (; itr != accidx.end() && itr->account == from; ++itr) // visiting all such accounts.
    {
        if (itr->locked_until > current_time)
        {
            accounts from_acnts(_self, from);
            const auto &sender = from_acnts.get(quantity.symbol.name(), "no balance object found");
            eosio_assert(quantity.amount <= (int64_t)((sender.balance.amount - itr->locked_balance)), "locked tokens cannot be transferred");
        }
    }
    // transfer lock check
    sub_balance(from, quantity);
    add_balance(to, quantity, from);
}

void token::setgrunlock(uint64_t date, uint8_t percent) // WIP
{
    require_auth(_self);
    gradunlocks gradual_unlock_table(_self, _self); // code: _self, scope: _self
    auto existing = gradual_unlock_table.find(date);
    eosio_assert(existing == gradual_unlock_table.end(), "This lock date is already set");
    gradual_unlock_table.emplace(_self, [&](auto &ut) {
        ut.locked_until = date;
        ut.lock_threshold = percent;
    });
}

void token::launchlock(account_name to, asset quantity)
{
    token::check_distributor_and_asset(quantity);
    /*time launch_date = stactic_cast<uint64_t>(1567987200)//==> epoch time in seconds corressponding to  Monday, 9 September 2019 00:00:00 GMT*/
    time launch_date = 1000; // launch date for testing.
    token::launch_lock(to, quantity, launch_date);
}

void token::gradlock(account_name to, asset quantity)
{
    token::check_distributor_and_asset(quantity);
    gradual_lock(to, quantity);
}

void token::distribsale(account_name from, account_name to, asset quantity, string memo)
{
    require_auth2(from, N(active)); // from and issuer should be same.

    SEND_INLINE_ACTION(*this, launchlock, {from, N(active)}, {to, quantity});
    SEND_INLINE_ACTION(*this, transfer, {from, N(active)}, {from, to, quantity, memo});
    // transfer(from, to, quantity, memo); // needs to be inlined
}

void token::setdistrib(asset currency, account_name distributor)
{
    eosio_assert(distributor == 0 || is_account(distributor), "there is no account with this name");

    auto sym_name = currency.symbol.name();
    stats statstable(_self, sym_name);
    auto itr = statstable.find(sym_name);
    eosio_assert(itr != statstable.end(), "token with symbol does not exist");

    statstable.modify(itr, _self, [&](auto &st) {
        require_auth(st.issuer);
        st.distributor = distributor;    
    });
}

void token::distribcontr(account_name from, account_name to, asset quantity, string memo)
{
    require_auth2(from, N(active)); // from and issuer should be same.

    SEND_INLINE_ACTION(*this, gradlock, {from, N(active)}, {to, quantity});
    SEND_INLINE_ACTION(*this, transfer, {from, N(active)}, {from, to, quantity, memo});
    // transfer(from, to, quantity, memo); // needs to be inlined.
}

void token::updaterate(uint8_t network, uint32_t rate) {
    require_auth(_self);
    exrates exrates_table(_self, _self); // code: _self, scope: _self
    auto itr = exrates_table.find(network);
    if (itr == exrates_table.end())
    {
        // create
        exrates_table.emplace(_self, [&](auto &rt) {
            rt.network = network;
            rt.rate = rate;
            rt.updated = now();
        });
    } else {
        // update
        exrates_table.modify(itr, _self, [&](auto &rt) {
            rt.rate = rate;
            rt.updated = now();
        });
    }
}

// PRIVATE UTILITY MEM-FUNCT'S DEFINITIONS
void token::sub_balance(account_name owner, asset value)
{
    accounts from_acnts(_self, owner);
    const auto &from = from_acnts.get(value.symbol.name(), "no balance object found");
    eosio_assert(from.balance.amount >= value.amount, "overdrawn balance");

    if (from.balance.amount == value.amount)
    {
        from_acnts.erase(from);
    }
    else
    {
        from_acnts.modify(from, owner, [&](auto &a) {
            a.balance -= value;
        });
    }
}

void token::add_balance(account_name owner, asset value, account_name ram_payer)
{
    accounts to_acnts(_self, owner);
    auto to = to_acnts.find(value.symbol.name());
    if (to == to_acnts.end())
    {
        to_acnts.emplace(ram_payer, [&](auto &a) {
            a.balance = value;
        });
    }
    else
    {
        to_acnts.modify(to, 0, [&](auto &a) {
            a.balance += value;
        });
    }
}

void token::launch_lock(account_name to, asset quantity, uint64_t launch_date)
{
    transferlocks transfer_lock_table(_self, _self); // code: _self, scope: _self
    auto accidx = transfer_lock_table.get_index<N(acc)>();
    auto itr = accidx.lower_bound(to);

    int count = 0;
    for (; itr != accidx.end() && itr->account == to; ++itr) // visiting all such accounts.
    {
        // lock date check
        if (itr->locked_until == launch_date)
        {
            accidx.modify(itr, _self, [&](auto &tfl) {
                tfl.locked_balance += static_cast<uint64_t>(quantity.amount);
            });
            count++;
            break;
        }
    }
    if (!count)
    {
        // create such entry in transferlocks
        auto prim_key = transfer_lock_table.available_primary_key();
        transfer_lock_table.emplace(_self, [&](auto &tfl) {
            // address.key = addresses.available_primary_key();
            tfl.id = prim_key;
            tfl.account = to;
            tfl.locked_balance = static_cast<uint64_t>(quantity.amount);
            tfl.locked_until = launch_date;
        });
    }
}

void token::gradual_lock(account_name to, asset quantity)
{
    gradunlocks gradual_unlock_table(_self, _self);
    transferlocks transfer_lock_table(_self, _self);
    auto accidx = transfer_lock_table.get_index<N(acc)>();
    int count = 0;

    for (auto &otr : gradual_unlock_table)
    {
        auto locked_until_date = otr.locked_until;
        auto threshold = otr.lock_threshold; // threshold value
        auto itr = accidx.lower_bound(to);

        for (; itr != accidx.end() && itr->account == to; ++itr) // visiting all accounts = to
        {
            {
                // lock date check
                if (itr->locked_until == locked_until_date)
                {
                    accidx.modify(itr, _self, [&](auto &tfl) {
                        tfl.locked_balance += (static_cast<uint64_t>(quantity.amount) * static_cast<uint64_t>(threshold) / static_cast<uint64_t>(100));
                    });
                    count++;
                    break;
                }
            }
        }
        if (!count)
        {
            // create such entry in transferlocks
            auto prim_key = transfer_lock_table.available_primary_key();
            transfer_lock_table.emplace(_self, [&](auto &tfl) {
                // address.key = addresses.available_primary_key();
                tfl.id = prim_key;
                tfl.account = to;
                tfl.locked_balance = static_cast<uint64_t>(quantity.amount);
                tfl.locked_until = locked_until_date;
            });
        }
    }
}

void token::check_distributor_and_asset(const asset& quantity) const
{
    auto sym = quantity.symbol;
    eosio_assert(sym.is_valid(), "invalid symbol name");
    auto sym_name = sym.name();
    stats statstable(_self, sym_name);
    auto st = statstable.find(sym_name);
    eosio_assert(st != statstable.end(), "token with symbol does not exist");
    eosio_assert(st->distributor, "distributor of this symbol is not set");
    require_auth2(st->distributor, N(active));

    eosio_assert(quantity.is_valid(), "invalid quantity");
    eosio_assert(quantity.amount > 0, "must issue positive quantity");
    eosio_assert(quantity.symbol == st->supply.symbol, "symbol precision mismatch");
    eosio_assert(quantity.amount <= st->max_supply.amount - st->supply.amount, "quantity exceeds available supply");
}

} // namespace eosio

EOSIO_ABI(eosio::token, (create)(issue)(transfer)(setgrunlock)(launchlock)(gradlock)(distribsale)(distribcontr)(updaterate)(setdistrib))
