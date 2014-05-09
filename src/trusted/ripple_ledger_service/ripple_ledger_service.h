#ifndef NATIVE_CLIENT_SRC_TRUSTED_RIPPLE_LEDGER_SERVICE_PROXY_RIPPLE_LEDGER_SERVICE_H_
#define NATIVE_CLIENT_SRC_TRUSTED_RIPPLE_LEDGER_SERVICE_PROXY_RIPPLE_LEDGER_SERVICE_H_

#include "native_client/src/include/nacl_base.h"

#include "native_client/src/shared/srpc/nacl_srpc.h"
#include "native_client/src/trusted/service_runtime/sel_ldr_thread_interface.h"
#include "native_client/src/trusted/simple_service/nacl_simple_service.h"

EXTERN_C_BEGIN

struct NaClSecureService;

/*
 * Trusted SRPC server that proxies name service lookups to the
 * embedder.
 */

struct NaClRippleLedgerService {
  struct NaClSimpleService  base NACL_IS_REFCOUNT_SUBCLASS;

  struct NaClSecureService  *server;
};

struct NaClRippleLedgerServiceConnection {
  struct NaClSimpleServiceConnection  base NACL_IS_REFCOUNT_SUBCLASS;

  struct NaClMutex                    mu;
  struct NaClCondVar                  cv;
  int                                 channel_initialized;
  struct NaClSrpcChannel              client_channel;
};

extern struct NaClSimpleServiceVtbl const kNaClRippleLedgerServiceVtbl;

/*
 * The client reference is used by the connection factory to initiate a
 * reverse connection: the connection factory enqueue a callback via
 * the NaClSecureReverseClientCtor that wakes up the connection
 * factory which issues an upcall on the existing reverse connection
 * to ask for a new one.  When the new connection arrives, the
 * NaClRippleLedgerServiceConnectionFactory can wrap the reverse channel
 * in the NaClRippleLedgerServiceConnection object, and subsequent RPC 
 * handlers use the connection object's reverse channel to forward RPC
 * requests.
 */
int NaClRippleLedgerServiceCtor(struct NaClRippleLedgerService  *self,
                                NaClThreadIfFactoryFunction     thread_factory_fn,
                                void                            *thread_factory_data,
                                struct NaClSecureService        *service);

int NaClRippleLedgerServiceConnectionCtor(struct NaClRippleLedgerServiceConnection  *self,
                                          struct NaClRippleLedgerService            *server,
                                          struct NaClDesc                           *conn);

int NaClRippleLedgerServiceConnectionFactory(
    struct NaClSimpleService            *vself,
    struct NaClDesc                     *conn,
    struct NaClSimpleServiceConnection  **out);

extern struct NaClSimpleServiceConnectionVtbl
  const kNaClRippleLedgerServiceConnectionVtbl;

EXTERN_C_END

#endif /* NATIVE_CLIENT_SRC_TRUSTED_RIPPLE_LEDGER_SERVICE_PROXY_RIPPLE_LEDGER_SERVICE_H_ */
