#ifndef NATIVE_CLIENT_SRC_PUBLIC_RIPPLE_LEDGER_SERVICE_H_
#define NATIVE_CLIENT_SRC_PUBLIC_RIPPLE_LEDGER_SERVICE_H_

#define NACL_RIPPLE_LEDGER_SERVICE_READ  "read_ledger:s:C"
/* ledger hash
   -> ledger data */

/* Request account transactions from the specified ledger. */
#define NACL_RIPPLE_LEDGER_SERVICE_GET_ACCOUNT_TXS "get_account_txs:ss:"
/* account, ledger index */

/* Submit payment transaction. */
#define NACL_RIPPLE_LEDGER_SERVICE_SUBMIT_PAYMENT_TX "submit_payment_tx:sssss:"
/* account, secret, recipient, amount, currency */

#endif /* NATIVE_CLIENT_SRC_PUBLIC_RIPPLE_LEDGER_SERVICE_H_ */
