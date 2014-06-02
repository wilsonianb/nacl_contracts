/*
 * Ripple escrow "contract" for vickrey auction.
 * Seller is paid second highest bid from winning bidder.
 * Return payments from non-winning bidders.
 * Winning bid is paid to the seller.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <sys/fcntl.h>
#include <string.h>
#include <unistd.h>
#include <tr1/unordered_map>
#include <sstream>
#include <json/reader.h>

#include "native_client/src/public/imc_syscalls.h"
#include "native_client/src/public/name_service.h"
#include "native_client/src/shared/srpc/nacl_srpc.h"
#include "native_client/src/public/ripple_ledger_service.h"

NaClSrpcChannel ns_channel;

bool auction_active = false;
int  start_ledger_index;

/* Ripple time is seconds since 1/1/2000 00:00:00 UTC epoch.
   This is equal to Unix timestamp minus 946684800. */
const long START_TIME = 455044551;
const long END_TIME = 455044851;

const double USD_MIN_BID = 5;

//Insert real values here.
const char *ESCROW_ADDRESS  = "DummyEscrowAddress";
const char *ESCROW_SECRET   = "DummyEscrowSecret";
const char *USD_ISSUER      = "DummyUSDIssuer";
const char *SELLER_ADDRESS  = "DummySellerAddress";


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
  long ledger_time;
  NaClSrpcChannel ledger_channel;
  
  if ((!reader.parse(ledger_json, ledger_root))) {
    fprintf(stderr, "Error parsing ledger json.\n");
    rpc->result = NACL_SRPC_RESULT_APP_ERROR;
    goto done;
  }
  
  ledger_time = ledger_root["ledger_time"].asInt64();
  if (!auction_active && ledger_time>START_TIME &&
      ledger_time<END_TIME) {
    auction_active = true;
    start_ledger_index = ledger_root["ledger_index"].asInt();
  }

  else if (auction_active && ledger_time>=END_TIME) {
    auction_active = false;

    if (!ConnectToRippleLedgerService(&ledger_channel)) {
      rpc->result = NACL_SRPC_RESULT_APP_ERROR;
      goto done;
    }

    if (NACL_SRPC_RESULT_OK !=
        NaClSrpcInvokeBySignature(&ledger_channel,
                                  NACL_RIPPLE_LEDGER_SERVICE_GET_ACCOUNT_TXS,
                                  ESCROW_ADDRESS,
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
  std::tr1::unordered_map<std::string, double> bidder_totals; /* <bidder address, total bid> */
  double high_bid=0;
  double second_high_bid=0;
  double amount;
  std::string high_bidder_addr;

  rpc->result = NACL_SRPC_RESULT_OK;

  if (!ConnectToRippleLedgerService(&ledger_channel)) {
    rpc->result = NACL_SRPC_RESULT_APP_ERROR;
    goto done;
  }

  /* Return payment transactions sent to the contract account
     from non-winning bidders. */
  if ((!reader.parse(transactions_json, txs_root))) {
    fprintf(stderr, "Error parsing transaction json.\n");
    rpc->result = NACL_SRPC_RESULT_APP_ERROR;
    goto done;
  }

  /* Count how much USD the contract account received in payments
     from each bidder during the auction.. */
  for (uint i=0; i<txs_root.size(); ++i) {
    if (txs_root[i]["tx"].empty()) { continue; }
    tx = txs_root[i]["tx"];

    /* Only handle USD "Payment" transactions to the contract account.
       For USD payments, "Amount" is an object with a currency, issuer, and value. */
    if (!tx["TransactionType"].isString() || 
        tx["TransactionType"].asString()!="Payment" ||
        !tx["Destination"].isString() || 
        strcmp(tx["Destination"].asCString(), ESCROW_ADDRESS)!=0 ||
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
    
      /* Update the bidder's total bid. */
      std::string bidder = tx["Account"].asString();

      bidder_totals[bidder] += atof(tx["Amount"]["value"].asCString());
      if (bidder_totals[bidder] > high_bid) {
        if (high_bidder_addr!=bidder) {
          high_bidder_addr = bidder;
          second_high_bid = high_bid;
        }

        high_bid = bidder_totals[bidder];
      }

      else if (bidder_totals[bidder] > second_high_bid) {
        second_high_bid = bidder_totals[bidder];
      }
    }
  }

  printf ("High bidder: %s\n", high_bidder_addr.c_str());
  printf ("High bid: %f\n", high_bid);
  printf ("second highest bid: %f\n", second_high_bid);

  if (high_bid<USD_MIN_BID) {
    printf("Minimum bid ($%f) not met! No sale.\n", USD_MIN_BID);
    high_bidder_addr.clear();
  }

  else {

    /* Winner must pay at least the minimum bid. */
    if (second_high_bid<USD_MIN_BID) { second_high_bid = USD_MIN_BID; }
    printf("Minimum bid met! Winner pays: $%f\n", second_high_bid);

    /* Pay the seller the second highest bid. */
    std::ostringstream amount_str;
    amount_str << second_high_bid;

    if (NACL_SRPC_RESULT_OK !=
        NaClSrpcInvokeBySignature(&ledger_channel,
                                  NACL_RIPPLE_LEDGER_SERVICE_SUBMIT_PAYMENT_TX,
                                  ESCROW_ADDRESS,
                                  ESCROW_SECRET,
                                  SELLER_ADDRESS,
                                  amount_str.str().c_str(),
                                  currency,
                                  USD_ISSUER,
                                  dummy_callback)) {
      fprintf(stderr, "NACL_RIPPLE_LEDGER_SERVICE_SUBMIT_PAYMENT_TX failed\n");
      rpc->result = NACL_SRPC_RESULT_APP_ERROR;
    }
  }

  /* Return all non-winning bids and the extra paid by the winner. */
  for (std::tr1::unordered_map<std::string, double>::iterator iBidder=bidder_totals.begin();
       iBidder!=bidder_totals.end(); ++iBidder) {
    amount = iBidder->second;
    if (iBidder->first==high_bidder_addr) {
      amount -= second_high_bid;
    }
    
    std::ostringstream amount_str;
    amount_str << amount;
    
    printf("Repay bidder %s $%f\n", iBidder->first.c_str(), amount);

    if (NACL_SRPC_RESULT_OK !=
        NaClSrpcInvokeBySignature(&ledger_channel,
                                  NACL_RIPPLE_LEDGER_SERVICE_SUBMIT_PAYMENT_TX,
                                  ESCROW_ADDRESS,
                                  ESCROW_SECRET,
                                  iBidder->first.c_str(),
                                  amount_str.str().c_str(),
                                  currency,
                                  USD_ISSUER,
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
