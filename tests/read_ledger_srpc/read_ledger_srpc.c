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

void HandleLedger(NaClSrpcRpc *rpc,
                  NaClSrpcArg **in_args,
                  NaClSrpcArg **out_args,
                  NaClSrpcClosure *done) {
  //const char *ledger_hash = (const char*) in_args[0]->arrays.str;
  const char *ledger_index = (const char*) in_args[1]->arrays.str;
  int             ledger;
  int             status;
  NaClSrpcChannel ledger_channel;

  if (NACL_SRPC_RESULT_OK !=
      NaClSrpcInvokeBySignature(&ns_channel, NACL_NAME_SERVICE_LOOKUP,
                                "RippleLedgerService", O_RDWR,
                                &status, &ledger)) {
    fprintf(stderr, "nameservice lookup RPC failed\n");
  }
  printf("Got ripple ledger service descriptor %d\n", ledger);
  if (-1 == ledger) {
    fprintf(stderr, "nameservice lookup failed: status %d\n", status);
    rpc->result = NACL_SRPC_RESULT_APP_ERROR;
    goto done;
  }

  // connect to Ripple ledger server 
  int ledger_conn;

  ledger_conn = imc_connect(ledger);
  printf("Got ripple ledger connection %d\n", ledger_conn);
  if (-1 == ledger_conn) {
    fprintf(stderr, "could not connect\n");
    rpc->result = NACL_SRPC_RESULT_APP_ERROR;
    goto done;
  }

  if (!NaClSrpcClientCtor(&ledger_channel, ledger_conn)) {
    fprintf(stderr, "could not build srpc client\n");
    rpc->result = NACL_SRPC_RESULT_APP_ERROR;
    goto done;
  }
  close(ledger);

  if (NACL_SRPC_RESULT_OK !=
      NaClSrpcInvokeBySignature(&ledger_channel, NACL_RIPPLE_LEDGER_SERVICE_GET_ACCOUNT_TXS,
                                account, ledger_index)) {
    fprintf(stderr, "read ripple ledger failed\n");
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

  //UNREFERENCED_PARAMETER(in_args);
  //UNREFERENCED_PARAMETER(out_args);

  rpc->result = NACL_SRPC_RESULT_OK;
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
