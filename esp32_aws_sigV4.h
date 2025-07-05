/*!
 *  @file esp32_aws_sigV4.h
 *
 *  @brief Subroutines to calculate AWS SigV4 signatures
 */

#ifndef ESP32_AWS_SIGV4_H
#define ESP32_AWS_SIGV4_H

#ifdef __cplusplus
extern "C" {
#endif

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mbedtls/md.h>

// --- FUNCTION DECLARATIONS ---

char *aws_sigV4_url_encode(char *str);

char *aws_sigV4_to_hex_string(char *str);

char *aws_sigV4_create_signing_key(char *secret_access_key, char *x_amz_date,
                                   char *aws_region, char *aws_service);

char *aws_sigV4_sign(char *key, size_t key_len, char *value, size_t value_len);

char *aws_sigV4_create_canonical_request(char *method,
                                         char *canonical_uri,
                                         char *canonical_query_string,
                                         char *canonical_headers,
                                         char *x_amz_signed_headers,
                                         char *hashed_payload);

char *aws_sigV4_create_string_to_sign(char *x_amz_date,
                                      char *x_amz_time,
                                      char *aws_region,
                                      char *aws_service,
                                      char *hashed_canonical_request);

char *aws_sigV4_create_canonical_query_string(
    char *access_key, char *x_amz_date, char *x_amz_time, char *aws_region,
    char *aws_service, char *x_amz_expires, char *x_amz_signed_headers,
    char *x_amz_security_token);

char *aws_sigV4_create_canonical_headers_string(char *host,
                                                char *x_amz_content_sha256,
                                                char *x_amz_date,
                                                char *x_amz_security_token);

char *aws_sigV4_presign_url(char *access_key, char *secret_access_key,
                            char *x_amz_security_token, char *bucket,
                            char *object, char *aws_region, char *x_amz_date,
                            char *x_amz_time, char *x_amz_expires);

#ifdef __cplusplus
}
#endif

#endif  // ESP32_AWS_SIGV4_H
