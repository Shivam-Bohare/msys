# CONTRACT FOR msys.token::launchlock

## ACTION NAME: launchlock

### Parameters
Input parameters:

* `to` (account to lock tokens in)
* `quantity` (amount of tokens with symbol as string to be locked)

Implied parameters: 

* `account_name` (name of the party invoking and signing the contract)
* `asset` (amount and symbol (example: "1.2345 msys") about to be sent)

### Intent
INTENT. Sets distributed user's tokens to be locked for the time being until the designated launch date.

### Term
TERM. This Contract expires at the conclusion of code execution. 

### Relationships
RELATIONSHIPS. This contract calls a private launch_lock() function.