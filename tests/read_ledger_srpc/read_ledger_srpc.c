/*
 * Simple test for reading Ripple ledger using simple rpc.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <sys/fcntl.h>
#include <string.h>
#include <unistd.h>

#include "native_client/src/public/imc_syscalls.h"
#include "native_client/src/public/name_service.h"
#include "native_client/src/shared/srpc/nacl_srpc.h"
#include "native_client/src/public/ripple_ledger_service.h"

NaClSrpcChannel ns_channel;

const char *account = "Insert contract's ripple address here";
const char *secret  = "Insert contract's secret key here";

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
  //const char *ledger_hash = (const char*) in_args[0]->arrays.str;
  const char *ledger_index = (const char*) in_args[1]->arrays.str;
  NaClSrpcChannel ledger_channel;

  if (!ConnectToRippleLedgerService(&ledger_channel)) {
    rpc->result = NACL_SRPC_RESULT_APP_ERROR;
    goto done;
  }

  if (NACL_SRPC_RESULT_OK !=
      NaClSrpcInvokeBySignature(&ledger_channel, NACL_RIPPLE_LEDGER_SERVICE_GET_ACCOUNT_TXS,
                                account, ledger_index)) {
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
  const char *sender = "address that just sent the contract money";

  if (!ConnectToRippleLedgerService(&ledger_channel)) {
    rpc->result = NACL_SRPC_RESULT_APP_ERROR;
    goto done;
  }

  if (NACL_SRPC_RESULT_OK !=
      NaClSrpcInvokeBySignature(&ledger_channel, NACL_RIPPLE_LEDGER_SERVICE_SUBMIT_PAYMENT_TX,
                                account, secret, sender, "5XRP", "5XRP")) {
    fprintf(stderr, "NACL_RIPPLE_LEDGER_SERVICE_SUBMIT_PAYMENT_TX failed\n");
    rpc->result = NACL_SRPC_RESULT_APP_ERROR;
    goto done;
  }

  //UNREFERENCED_PARAMETER(in_args);
  //UNREFERENCED_PARAMETER(out_args);

  rpc->result = NACL_SRPC_RESULT_OK;
 done:
  done->Run(done);
}

const struct NaClSrpcHandlerDesc srpc_methods[] = {
  { "new_ledger:ss:", HandleLedger },
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
