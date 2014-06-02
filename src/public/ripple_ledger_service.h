#ifndef NATIVE_CLIENT_SRC_PUBLIC_RIPPLE_LEDGER_SERVICE_H_
#define NATIVE_CLIENT_SRC_PUBLIC_RIPPLE_LEDGER_SERVICE_H_

/* Request account transactions from the specified ledger. */
#define NACL_RIPPLE_LEDGER_SERVICE_GET_ACCOUNT_TXS "get_account_txs:siis:"
/* account, ledger_index_min, ledger_index_max, callback */

/* Submit payment transaction. */
#define NACL_RIPPLE_LEDGER_SERVICE_SUBMIT_PAYMENT_TX "submit_payment_tx:sssssss:"
/* account, secret, recipient, amount, currency, issuer, callback */

#endif /* NATIVE_CLIENT_SRC_PUBLIC_RIPPLE_LEDGER_SERVICE_H_ */
