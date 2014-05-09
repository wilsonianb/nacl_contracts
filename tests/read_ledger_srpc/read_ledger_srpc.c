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


int main(void) {
  NaClSrpcChannel ns_channel;
  int             ns;
  int             connected_socket;
  int             ledger;
  int             status;
  char            *ledger_hash = "7EA447F26C2BB396218D39FEA13DC1273D5BAE10327193783BBE96AAD42C44EB";
  char            buffer[1024];
  uint32_t        nbytes = sizeof buffer;

  if (!NaClSrpcModuleInit()) {
    return 1;
  }
  
  printf ("Hello world\n");
  
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

  if (NACL_SRPC_RESULT_OK !=
      NaClSrpcInvokeBySignature(&ns_channel, NACL_NAME_SERVICE_LOOKUP,
                                "RippleLedgerService", O_RDWR,
                                &status, &ledger)) {
    fprintf(stderr, "nameservice lookup RPC failed\n");
  }
  printf("Got ripple ledger service descriptor %d\n", ledger);
  if (-1 == ledger) {
    fprintf(stderr, "nameservice lookup failed: status %d\n", status);
    return 1;
  }

  /* connect to Ripple ledger server */
  int ledger_conn;
  struct NaClSrpcChannel ledger_channel;

  ledger_conn = imc_connect(ledger);
  printf("got ripple ledger connection %d\n", ledger_conn);
  if (-1 == ledger_conn) {
    fprintf(stderr, "could not connect\n");
    return 1;
  }
  close(ledger);
  if (!NaClSrpcClientCtor(&ledger_channel, ledger_conn)) {
    fprintf(stderr, "could not build srpc client\n");
    return 1;
  }
  if (NACL_SRPC_RESULT_OK !=
      NaClSrpcInvokeBySignature(&ledger_channel, NACL_RIPPLE_LEDGER_SERVICE_READ,
                                ledger_hash, &nbytes, buffer)) {
    fprintf(stderr, "read ripple ledger failed, status %d\n", status);
    return 1;
  }
  printf ("Ledger data: %s\n", buffer);
}
