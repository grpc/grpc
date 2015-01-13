#ifndef NET_GRPC_PHP_GRPC_BYTE_BUFFER_H_
#define NET_GRPC_PHP_GRPC_BYTE_BUFFER_H_

#include "grpc/grpc.h"

grpc_byte_buffer *string_to_byte_buffer(char *string, size_t length);

void byte_buffer_to_string(grpc_byte_buffer *buffer, char **out_string,
                           size_t *out_length);

#endif /* NET_GRPC_PHP_GRPC_BYTE_BUFFER_H_ */
