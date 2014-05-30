/*
 * Ripple kickstarter "contract".
 * Returns payments if goal is not reached by deadline.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <sys/fcntl.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <json/reader.h>

#include "native_client/src/public/imc_syscalls.h"
#include "native_client/src/public/name_service.h"
#include "native_client/src/shared/srpc/nacl_srpc.h"
#include "native_client/src/public/ripple_ledger_service.h"

NaClSrpcChannel ns_channel;

bool campaign_active = false;
int  start_ledger_index;

/* Ripple time is seconds since 1/1/2000 00:00:00 UTC epoch.
   This is equal to Unix timestamp minus 946684800. */
const long START_TIME = 454786200;  //2014-05-30 17:30:00
const long END_TIME = 454786500 ;   //2014-05-30 17:35:00

const double USD_GOAL = 10;

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
  long ledger_time;
  NaClSrpcChannel ledger_channel;
  
  if ((!reader.parse(ledger_json, ledger_root))) {
    fprintf(stderr, "Error parsing ledger json.\n");
    rpc->result = NACL_SRPC_RESULT_APP_ERROR;
    goto done;
  }
  
  ledger_time = ledger_root["ledger_time"].asInt64();
  if (!campaign_active && ledger_time>START_TIME &&
      ledger_time<END_TIME) {
    campaign_active = true;
    start_ledger_index = ledger_root["ledger_index"].asInt();
  }

  else if (campaign_active && ledger_time>=END_TIME) {
    campaign_active = false;

    if (!ConnectToRippleLedgerService(&ledger_channel)) {
      rpc->result = NACL_SRPC_RESULT_APP_ERROR;
      goto done;
    }

    if (NACL_SRPC_RESULT_OK !=
        NaClSrpcInvokeBySignature(&ledger_channel,
                                  NACL_RIPPLE_LEDGER_SERVICE_GET_ACCOUNT_TXS,
                                  ACCOUNT,
                                  start_ledger_index,
                                  ledger_root["ledger_index"].asInt(),
                                  "handle_txs:s:")) {
      fprintf(stderr, "NACL_RIPPLE_LEDGER_SERVICE_GET_ACCOUNT_TXS failed\n");
      rpc->result = NACL_SRPC_RESULT_APP_ERROR;
      goto done;
    }
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
  const char *currency = "USD";
  Json::Reader reader;
  Json::Value txs_root;
  Json::Value tx;
  std::vector<uint> valid_txs;
  double campaign_total=0;

  rpc->result = NACL_SRPC_RESULT_OK;

  if (!ConnectToRippleLedgerService(&ledger_channel)) {
    rpc->result = NACL_SRPC_RESULT_APP_ERROR;
    goto done;
  }

  /* Reciprocate payment transactions sent to the contract account. */
  if ((!reader.parse(transactions_json, txs_root))) {
    fprintf(stderr, "Error parsing transaction json.\n");
    rpc->result = NACL_SRPC_RESULT_APP_ERROR;
    goto done;
  }

  /* Count how much USD the contract account 
     received in payments during the campaign.
     Record the indices of valid transactions. */
  for (uint i=0; i<txs_root.size(); ++i) {
    if (txs_root[i]["tx"].empty()) { continue; }
    tx = txs_root[i]["tx"];

    /* Only handle USD "Payment" transactions to the contract account.
       For USD payments, "Amount" is an object with a currency, issuer, and value. */
    if (!tx["TransactionType"].isString() || 
        tx["TransactionType"].asString()!="Payment" ||
        !tx["Destination"].isString() || 
        strcmp(tx["Destination"].asCString(), ACCOUNT)!=0 ||
        !tx["Account"].isString() ||
        !tx["Amount"].isObject()) {
      continue;
    }

    if (tx["Amount"]["currency"].isString() && tx["Amount"]["currency"].asString()=="USD") {
      if (!tx["Amount"]["value"].isString()) {
        fprintf(stderr, "Missing transaction amount data.\n");
        rpc->result = NACL_SRPC_RESULT_APP_ERROR;
        continue;
      }
    
      /* Count the backer's payment. */
      campaign_total += atof(tx["Amount"]["value"].asCString());

      /* Record the transaction index. */
      valid_txs.push_back (i);
    }
  }

  printf ("campaign_total: %f\n", campaign_total);

  /* Campaign funded. Do nothing. */
  if (campaign_total>=USD_GOAL) {
    printf("Campaign funded!\n");
  }

  /* Campaign failed to meet goal.
     Return funds to backers. */
  else {
    for (std::vector<uint>::iterator iTx=valid_txs.begin(); iTx!=valid_txs.end(); ++iTx) {
      tx = txs_root[*iTx]["tx"]; 

      if (NACL_SRPC_RESULT_OK !=
          NaClSrpcInvokeBySignature(&ledger_channel,
                                    NACL_RIPPLE_LEDGER_SERVICE_SUBMIT_PAYMENT_TX,
                                    ACCOUNT,
                                    SECRET,
                                    tx["Account"].asCString(),
                                    tx["Amount"]["value"].asCString(),
                                    currency,
                                    USD_ISSUER,
                                    dummy_callback)) {
        fprintf(stderr, "NACL_RIPPLE_LEDGER_SERVICE_SUBMIT_PAYMENT_TX failed\n");
        rpc->result = NACL_SRPC_RESULT_APP_ERROR;
      }
    }
  }

 done:
  done->Run(done);
}

const struct NaClSrpcHandlerDesc srpc_methods[] = {
  { "new_ledger:s:", HandleLedger },
  { "handle_txs:s:", HandleTransactions },
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
