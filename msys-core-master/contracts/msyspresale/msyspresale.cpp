#include "msyspresale.hpp"

#include "../msys.token/msys.token.hpp"

int64_t msyspresale::get_bonus_state(const uint8_t stage) const
{
  asset sum = bonus_thr[stage];

  contributions contribution_table(_self, _self);
  for (auto ct = contribution_table.begin(); ct != contribution_table.end(); ct++)
  {
    if (ct->validated > 0)
    {
      sum -= ct->msys_dist;
    }
  }

  return sum.amount;
}

int64_t msyspresale::get_sale_state(const asset &cap) const
{
  asset sum = cap;

  contributions contribution_table(_self, _self);

  if (cap == softcap)
  {
    for (auto ct = contribution_table.begin(); ct != contribution_table.end(); ct++)
    {
      if (ct->validated > 0)
      {
        sum -= ct->msys_dist;
      }
    }
  }
  else
  {
    for (auto ct = contribution_table.begin(); ct != contribution_table.end(); ct++)
    {
      if (ct->distributed > 0)
      {
        sum -= ct->msys_dist;
      }
    }
  }

  return sum.amount;
}

bool msyspresale::is_contributor_approved(const account_name contributor) const
{
  whitelists whitelist_table(_self, _self);
  auto itr = whitelist_table.find(contributor);
  if (itr != whitelist_table.end() && itr->acceptedterms && itr->approved > 0)
  {
    return true;
  }
  return false;
}

void msyspresale::apply(const account_name contributor,
                         const bool acceptedterms)
{
  require_auth(contributor);

  whitelists whitelist_table(_self, _self);
  auto itr = whitelist_table.find(contributor);
  if (itr == whitelist_table.end())
  {
    // create
    whitelist_table.emplace(_self, [&](auto &wt) {
      wt.contributor = contributor;
      wt.acceptedterms = acceptedterms;
      wt.approved = 0;
    });
  }
  else
  {
    // update
    whitelist_table.modify(itr, _self, [&](auto &wt) {
      wt.contributor = contributor;
      wt.acceptedterms = acceptedterms;
    });
  }
}

void msyspresale::approve(const account_name contributor)
{
  require_auth(_self);
  eosio_assert(is_account(contributor), "contributor account does not exist");

  whitelists whitelist_table(_self, _self);
  auto itr = whitelist_table.find(contributor);

  eosio_assert(itr != whitelist_table.end(), "there is no such contributor in whitelist");
  eosio_assert(itr->acceptedterms, "contributor have not accepted terms");

  whitelist_table.modify(itr, _self, [&](auto &wt) {
    wt.approved = now();
  });
}

void msyspresale::buymsys(const account_name contributor,
                            const uint8_t network,
                            const asset &quantity,
                            const string &memo)
{
  require_auth(contributor);
  eosio_assert(get_sale_state(hardcap) > 0, "hard cap reached");
  eosio_assert(is_contributor_approved(contributor), "please wait while we approve your participation");

  contributions contribution_table(_self, _self);

  auto contributorIndex = contribution_table.get_index<N(contributor)>();

  uint8_t counter = 0;
  auto lower = contributorIndex.lower_bound(contributor);
  auto upper = contributorIndex.upper_bound(contributor);
  
  for (auto itr = lower; itr != upper; itr++)
  {
    counter++;
  }

  eosio_assert(counter < 3, "contributor cannot have more than 3 unverified transactions");

  contribution_table.emplace(_self, [&](auto &ct) {
    ct.id = contribution_table.available_primary_key();
    ct.contributor = contributor;
    ct.network = network;
    ct.quantity = quantity;
    ct.datetime = now();
    ct.memo = memo;
    ct.distributed = 0;
    ct.refunded = 0;

    token::exrates exrate_table(tokencontract, tokencontract); // TODO: abstract to configurable const on the top of .hpp;
    auto rt = exrate_table.find(network);
    eosio_assert(rt != exrate_table.end(), "no such network in msys.token exrates table");

    ct.rate = rt->rate;
    ct.msys_dist = asset(ct.quantity.amount * ct.rate / 10000, S(4, msys));
    ct.msys_bonus = asset(0, S(4, msys));

    // if (network == NETWORK_EOS) {
    // action(
    //   { contributor, N(active) },
    //   tokencontract, N(transfer),
    //   std::make_tuple(contributor, _self, quantity, ct.memo)
    // ).send();
    // ct.validated = now();
    // }
  });
}

void msyspresale::validate(const uint64_t id, const string &memo, const string &transaction)
{
  require_auth(_self);

  contributions contribution_table(_self, _self);
  auto itr = contribution_table.find(id);
  eosio_assert(itr != contribution_table.end(), "there is no contribution with this id");

  contribution_table.modify(itr, _self, [&](auto &vt) {
    eosio_assert(is_contributor_approved(vt.contributor), "please wait while we approve your participation");
    eosio_assert(memo == vt.memo, "transaction memo mismatch");
    eosio_assert(start < vt.datetime && end > vt.datetime, "date of the transaction outside sale date and time bounds");

    vt.validated = now();
    vt.transaction = transaction;

    const uint8_t stages_count = 2;

    for (uint8_t stage = 0;
         vt.msys_bonus.amount == 0 && get_bonus_state(stage) > 0 && stage < stages_count;
         stage++)
    {
      auto msys_bonus = vt.msys_dist.amount * bonus[stage] / 100;

      if (msys_bonus <= get_bonus_state(stage))
      {
        vt.msys_bonus.amount = msys_bonus;
      }
      else
      {
        vt.msys_bonus.amount = get_bonus_state(stage);

        if (stage < stages_count - 1)
        {
          vt.msys_bonus.amount += (vt.msys_dist.amount - vt.msys_bonus.amount * 100 / bonus[stage]) * bonus[stage + 1] / 100;
        }
      }
    }
  });
}

void msyspresale::deletetx(const uint64_t id)
{
  require_auth(_self);
  contributions contribution_table(_self, _self);
  auto itr = contribution_table.find(id);
  eosio_assert(itr != contribution_table.end(), "there is no contribution with this id");
  eosio_assert(itr->validated > 0, "cannot delete validated contribution");
  contribution_table.erase(itr);
  eosio_assert(itr != contribution_table.end(), "contribution not deleted properly");
}

void msyspresale::distribute(const uint64_t id)
{
  eosio_assert(get_sale_state(softcap) < 0, "cannot distribute tokens before reaching soft cap");

  contributions contribution_table(_self, _self);
  auto itr = contribution_table.find(id);
  eosio_assert(itr != contribution_table.end(), "there is no contribution with this id");

  contribution_table.modify(itr, _self, [&](auto &dt) {
    eosio_assert(dt.validated > 0, "contribution is not validated");
    eosio_assert(dt.refunded == 0, "contribution is refunded");

    auto hardcapLeft = get_sale_state(hardcap);

    if (hardcapLeft > 0)
    {
      auto dc = asset(0, S(4, msys));

      if (hardcapLeft < dt.msys_dist.amount)
      {
        dc.amount = hardcapLeft;
        dt.refund = dt.quantity - (dt.msys_dist - dc) / dt.rate;
      }
      else
      {
        dc = dt.msys_dist + dt.msys_bonus;
      }

      action(
          {_self, N(active)},
          tokencontract, N(distribsale),
          std::make_tuple(_self, dt.contributor, dc, dt.memo))
          .send();

      dt.distributed = now();
    }
    else
    {
      dt.refund = dt.msys_dist / dt.rate;
      dt.distrib_tx = "";
    }
  });
}

void msyspresale::refundsale(uint64_t id, string refund_tx)
{
  require_auth(_self);
  eosio_assert(get_sale_state(softcap) < 0 && (get_sale_state(hardcap) > 1), "cannot refund if soft cap reached but hard cap not");
  contributions contribution_table(_self, _self);

  auto r = contribution_table.find(id);
  if (r->validated > 0 && r->refund.amount > 0)
  {
    if (r->network == NETWORK_EOS)
    {
      action(
          {N(msyspresale), N(active)},
          tokencontract, N(transfer),
          std::make_tuple(N(msyspresale), r->contributor, r->refund, r->memo))
          .send();
    }
    if (refund_tx != "")
    {
      contribution_table.modify(r, _self, [&](auto &val) {
        val.refunded = now();
        val.refund_tx = refund_tx;
      });
    }
  }
}

EOSIO_ABI(msyspresale, (apply)(approve)(buymsys)(validate)(deletetx)(distribute)(refundsale))
