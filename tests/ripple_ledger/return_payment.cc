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

const char *ACCOUNT    = "Insert contract's ripple address here";
const char *SECRET     = "Insert contract's secret key here";
const char *USD_ISSUER = "Insert trusted USD issuer address here";

int ConnectToRippleLedgerService (NaClSrpcChannel* ledger_channel) {
  int ledger;
  int status;

  if (NACL_SRPC_RESULT_OK !=
      NaClSrpcInvokeBySignature(&ns_channel, NACL_NAME_SERVICE_LOOKUP,
                                "RippleLedgerService", O_RDWR,
                                &status, &ledger)) {
    fprintf(stderr, "nameservice lookup RPC failed\n");
  }
  printf("Got ripple ledger service descriptor %d\n", ledger);
  if (-1 == ledger) {
    fprintf(stderr, "nameservice lookup failed: status %d\n", status);
    return 0;
  }

  // connect to Ripple ledger server 
  int ledger_conn;

  ledger_conn = imc_connect(ledger);
  printf("Got ripple ledger connection %d\n", ledger_conn);
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
  NaClSrpcChannel ledger_channel;
  
  if ((!reader.parse(ledger_json, ledger_root))) {
    fprintf(stderr, "Error parsing ledger json.\n");
    rpc->result = NACL_SRPC_RESULT_APP_ERROR;
    goto done;
  }
  
  if (!ConnectToRippleLedgerService(&ledger_channel)) {
    rpc->result = NACL_SRPC_RESULT_APP_ERROR;
    goto done;
  }

  if (NACL_SRPC_RESULT_OK !=
      NaClSrpcInvokeBySignature(&ledger_channel,
                                NACL_RIPPLE_LEDGER_SERVICE_GET_ACCOUNT_TXS,
                                ACCOUNT,
                                ledger_root["ledger_index"].asInt(),
                                "new_transaction:s:")) {
    fprintf(stderr, "NACL_RIPPLE_LEDGER_SERVICE_GET_ACCOUNT_TXS failed\n");
    rpc->result = NACL_SRPC_RESULT_APP_ERROR;
    goto done;
  }
  rpc->result = NACL_SRPC_RESULT_OK;
 done:
  done->Run(done);
}

void HandleTransaction(NaClSrpcRpc *rpc,
                       NaClSrpcArg **in_args,
                       NaClSrpcArg **out_args,
                       NaClSrpcClosure *done) {
  NaClSrpcChannel ledger_channel;
  const std::string transaction_json = in_args[0]->arrays.str;
  const char *dummy_callback = "";
  Json::Reader reader;
  Json::Value tx_root;
  const char *amount;
  const char *currency;
  const char *issuer;

  /* Reciprocate payment transactions sent to the contract account. */
  if ((!reader.parse(transaction_json, tx_root))) {
    fprintf(stderr, "Error parsing transaction json.\n");
    rpc->result = NACL_SRPC_RESULT_APP_ERROR;
    goto done;
  }

  /* Only handle "Payment" transactions to the contract account. */
  if (!tx_root["TransactionType"].isString() || tx_root["TransactionType"].asString()!="Payment" ||
      !tx_root["Destination"].isString() || strcmp(tx_root["Destination"].asCString(), ACCOUNT)!=0 ||
      !tx_root["Account"].isString()) {
    rpc->result = NACL_SRPC_RESULT_OK;
    goto done;
  }

  /* For XRP payments, "Amount" is a single string.
     For payments in other currencies, "Amount" is an object with a currency, issuer, and value. */
  if (tx_root["Amount"].empty()) {
    fprintf(stderr, "Missing transaction amount data.\n");
    rpc->result = NACL_SRPC_RESULT_APP_ERROR;
    goto done;
  }
  
  /* XRP transaction */
  else if (tx_root["Amount"].isString()) {
    amount   = tx_root["Amount"].asCString();
    currency = "";
    issuer   = "";
  }

  /* USD transaction */
  else if (tx_root["Amount"]["currency"].isString() && tx_root["Amount"]["currency"].asString()=="USD") {
    if (!tx_root["Amount"]["value"].isString()) {
      fprintf(stderr, "Missing transaction amount data.\n");
      rpc->result = NACL_SRPC_RESULT_APP_ERROR;
      goto done;
    }
    
    amount   = tx_root["Amount"]["value"].asCString();
    currency = "USD";
    issuer   = USD_ISSUER;
  }
  else {
    rpc->result = NACL_SRPC_RESULT_OK;
    goto done;
  }

  if (!ConnectToRippleLedgerService(&ledger_channel)) {
    rpc->result = NACL_SRPC_RESULT_APP_ERROR;
    goto done;
  }

  if (NACL_SRPC_RESULT_OK !=
      NaClSrpcInvokeBySignature(&ledger_channel,
                                NACL_RIPPLE_LEDGER_SERVICE_SUBMIT_PAYMENT_TX,
                                ACCOUNT,
                                SECRET,
                                tx_root["Account"].asCString(),
                                amount, 
                                currency,
                                issuer,
                                dummy_callback)) {
    fprintf(stderr, "NACL_RIPPLE_LEDGER_SERVICE_SUBMIT_PAYMENT_TX failed\n");
    rpc->result = NACL_SRPC_RESULT_APP_ERROR;
    goto done;
  }

  rpc->result = NACL_SRPC_RESULT_OK;
 done:
  done->Run(done);
}

const struct NaClSrpcHandlerDesc srpc_methods[] = {
  { "new_ledger:s:", HandleLedger },
  { "new_transaction:s:", HandleTransaction },
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
