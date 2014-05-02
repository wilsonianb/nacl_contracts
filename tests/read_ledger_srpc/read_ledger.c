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


int main(void) {
  int ns;
  NaClSrpcChannel channel;
  int connected_socket;
  char *ledger_hash = "7EA447F26C2BB396218D39FEA13DC1273D5BAE10327193783BBE96AAD42C44EB";
  //char *ledger_data;
  char      buffer[1024];
  uint32_t  nbytes = sizeof buffer;

  if (!NaClSrpcModuleInit()) {
    return 1;
  }
  
  printf ("Hello world");
  
  ns = -1;
  nacl_nameservice(&ns);
  if (ns==-1) {
    return 1;
  }
  
  connected_socket = imc_connect(ns);
  
  if (!NaClSrpcClientCtor(&channel, connected_socket)) {
    fprintf(stderr, "Srpc client channel ctor failed\n");
    return 1;
  }
  
  if (NACL_SRPC_RESULT_OK !=
      NaClSrpcInvokeBySignature(&channel, NACL_REVERSE_READ_RIPPLE_LEDGER,
                                ledger_hash, &nbytes, buffer)) {
    fprintf(stderr, "read ripple ledger failed, status %d\n", status);
    return 1;
  }

  printf ("%s", ledger_data);
  
  return 0;
}
