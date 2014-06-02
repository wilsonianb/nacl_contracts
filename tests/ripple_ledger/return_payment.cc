/*
 * Ripple "contract" that returns XRP or USD payments.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <sys/fcntl.h>
#include <string.h>
#include <unistd.h>
#include <json/reader.h>

#include "native_client/src/public/imc_syscalls.h"
#include "native_client/src/public/name_service.h"
#include "native_client/src/shared/srpc/nacl_srpc.h"
#include "native_client/src/public/ripple_ledger_service.h"

NaClSrpcChannel ns_channel;

//Insert real values here.
const char *ACCOUNT    = "DummyContractAddress";
const char *SECRET     = "DummyContractSecret";
const char *USD_ISSUER = "DummyUSDIssuer";


int ConnectToRippleLedgerService (NaClSrpcChannel* ledger_channel) {
  int ledger;
  int status;

  if (NACL_SRPC_RESULT_OK !=
      NaClSrpcInvokeBySignature(&ns_channel, NACL_NAME_SERVICE_LOOKUP,
                                "RippleLedgerService", O_RDWR,
                                &status, &ledger)) {
    fprintf(stderr, "nameservice lookup RPC failed\n");
  }

  if (-1 == ledger) {
    fprintf(stderr, "nameservice lookup failed: status %d\n", status);
    return 0;
  }

  int ledger_conn;

  ledger_conn = imc_connect(ledger);
  if (-1 == ledger_conn) {
    fprintf(stderr, "could not connect\n");
    return 0;
  }

  if (!NaClSrpcClientCtor(ledger_channel, ledger_conn)) {
    fprintf(stderr, "could not build srpc client\n");
    return 0;
  }

  close(ledger);

  return 1;
}

void HandleLedger(NaClSrpcRpc *rpc,
                  NaClSrpcArg **in_args,
                  NaClSrpcArg **out_args,
                  NaClSrpcClosure *done) {
  const char *ledger_json = (const char*) in_args[0]->arrays.str;
  Json::Reader reader;
  Json::Value ledger_root;
  int ledger_index;
  NaClSrpcChannel ledger_channel;
  
  if ((!reader.parse(ledger_json, ledger_root))) {
    fprintf(stderr, "Error parsing ledger json.\n");
    rpc->result = NACL_SRPC_RESULT_APP_ERROR;
    goto done;
  }
  
  ledger_index = ledger_root["ledger_index"].asInt();

  if (!ConnectToRippleLedgerService(&ledger_channel)) {
    rpc->result = NACL_SRPC_RESULT_APP_ERROR;
    goto done;
  }

  if (NACL_SRPC_RESULT_OK !=
      NaClSrpcInvokeBySignature(&ledger_channel,
                                NACL_RIPPLE_LEDGER_SERVICE_GET_ACCOUNT_TXS,
                                ACCOUNT,
                                ledger_index,
                                ledger_index,
                                "new_transactions:s:")) {
    fprintf(stderr, "NACL_RIPPLE_LEDGER_SERVICE_GET_ACCOUNT_TXS failed\n");
    rpc->result = NACL_SRPC_RESULT_APP_ERROR;
    goto done;
  }
  rpc->result = NACL_SRPC_RESULT_OK;
 done:
  done->Run(done);
}

void HandleTransactions(NaClSrpcRpc *rpc,
                        NaClSrpcArg **in_args,
                        NaClSrpcArg **out_args,
                        NaClSrpcClosure *done) {
  NaClSrpcChannel ledger_channel;
  const std::string transactions_json = in_args[0]->arrays.str;
  const char *dummy_callback = "";
  Json::Reader reader;
  Json::Value txs_root;
  Json::Value tx;
  const char *amount;
  const char *currency;
  const char *issuer;

  rpc->result = NACL_SRPC_RESULT_OK;

  /* Reciprocate payment transactions sent to the contract account. */
  if ((!reader.parse(transactions_json, txs_root))) {
    fprintf(stderr, "Error parsing transaction json.\n");
    rpc->result = NACL_SRPC_RESULT_APP_ERROR;
    goto done;
  }

  if (!ConnectToRippleLedgerService(&ledger_channel)) {
    rpc->result = NACL_SRPC_RESULT_APP_ERROR;
    goto done;
  }

  for (uint i=0; i<txs_root.size(); ++i) {

    if (txs_root[i]["tx"].empty()) { continue; }
    tx = txs_root[i]["tx"];

    /* Only handle "Payment" transactions to the contract account. */
    if (!tx["TransactionType"].isString() || 
        tx["TransactionType"].asString()!="Payment" ||
        !tx["Destination"].isString() || 
        strcmp(tx["Destination"].asCString(), ACCOUNT)!=0 ||
        !tx["Account"].isString()) {
      continue;
    }

    /* For XRP payments, "Amount" is a single string.
       For payments in other currencies, "Amount" is an object with a currency, issuer, and value. */
    if (tx["Amount"].empty()) {
      fprintf(stderr, "Missing transaction amount data.\n");
      continue;
    }

    /* XRP transaction */
    else if (tx["Amount"].isString()) {
      amount   = tx["Amount"].asCString();
      currency = "";
      issuer   = "";
    }

    /* USD transaction */
    else if (tx["Amount"]["currency"].isString() && tx["Amount"]["currency"].asString()=="USD") {
      if (!tx["Amount"]["value"].isString()) {
        fprintf(stderr, "Missing transaction amount data.\n");
        rpc->result = NACL_SRPC_RESULT_APP_ERROR;
      }
    
      amount   = tx["Amount"]["value"].asCString();
      currency = "USD";
      issuer   = USD_ISSUER;
    }
    else {
      continue;
    }

    if (NACL_SRPC_RESULT_OK !=
        NaClSrpcInvokeBySignature(&ledger_channel,
                                  NACL_RIPPLE_LEDGER_SERVICE_SUBMIT_PAYMENT_TX,
                                  ACCOUNT,
                                  SECRET,
                                  tx["Account"].asCString(),
                                  amount, 
                                  currency,
                                  issuer,
                                  dummy_callback)) {
      fprintf(stderr, "NACL_RIPPLE_LEDGER_SERVICE_SUBMIT_PAYMENT_TX failed\n");
      rpc->result = NACL_SRPC_RESULT_APP_ERROR;
    }
  }
  
 done:
  done->Run(done);
}

const struct NaClSrpcHandlerDesc srpc_methods[] = {
  { "new_ledger:s:", HandleLedger },
  { "new_transactions:s:", HandleTransactions },
  { NULL, NULL },
};

int main(void) {
  int ns;
  int connected_socket;

  if (!NaClSrpcModuleInit()) {
    return 1;
  }
  
  ns = -1;
  nacl_nameservice(&ns);
  if (ns==-1) {
    fprintf(stderr, "Nameservice retrieval failed\n");
    return 1;
  }
  
  connected_socket = imc_connect(ns);  
  if (!NaClSrpcClientCtor(&ns_channel, connected_socket)) {
    fprintf(stderr, "Srpc client channel ctor failed\n");
    return 1;
  }
  close(ns);

  if (!NaClSrpcAcceptClientConnection(srpc_methods)) {
    return 1;
  }
  NaClSrpcModuleFini();
  return 0;
}
