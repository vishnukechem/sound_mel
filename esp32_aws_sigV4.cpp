#include "esp32_aws_sigV4.h"

static int pstrcmp(const void *a, const void *b)
{
    return strcmp(*(const char **)a, *(const char **)b);
}

static char *string_gen(char **arr, int count, char delimiter, int sort)
{
    int total_len = 0;
    for (int i = 0; i < count; i++) {
        total_len += strlen(arr[i]) + 1; // +1 for delimiter or null-terminator
    }

    char *result = (char *)malloc(total_len);
    if (!result) return NULL;
    result[0] = '\0';

    if (sort) {
        qsort(arr, count, sizeof(arr[0]), pstrcmp);
    }

    for (int i = 0; i < count; i++) {
        strcat(result, arr[i]);
        if (i < count - 1) {
            int len = strlen(result);
            result[len] = delimiter;
            result[len + 1] = '\0';
        }
    }
    return result;
}

char *aws_sigV4_url_encode(char *str)
{
    static char hex[] = "0123456789ABCDEF";
    char *pstr = str;
    char *buf = (char *)malloc(strlen(str) * 3 + 1);
    char *pbuf = buf;
    while (*pstr) {
        if (isalnum(*pstr) || *pstr == '-' || *pstr == '_' || *pstr == '.' || *pstr == '~') {
            *pbuf++ = *pstr;
        } else {
            *pbuf++ = '%';
            *pbuf++ = hex[(*pstr >> 4) & 15];
            *pbuf++ = hex[*pstr & 15];
        }
        pstr++;
    }
    *pbuf = '\0';
    return buf;
}

char *aws_sigV4_to_hex_string(char *str)
{
    char *hex = (char *)malloc(65);
    for (int i = 0; i < 32; i++) {
        sprintf(hex + (i * 2), "%02x", (unsigned int)str[i] & 0xFF);
    }
    hex[64] = '\0';
    return hex;
}

char *aws_sigV4_create_signing_key(char *secret_access_key, char *x_amz_date,
                                   char *aws_region, char *aws_service)
{
    char *AWS4;
    asprintf(&AWS4, "AWS4%s", secret_access_key);

    char *kDate = aws_sigV4_sign(AWS4, strlen(AWS4), x_amz_date, strlen(x_amz_date));
    char *kRegion = aws_sigV4_sign(kDate, 32, aws_region, strlen(aws_region));
    char *kService = aws_sigV4_sign(kRegion, 32, aws_service, strlen(aws_service));
    char *kSigning = aws_sigV4_sign(kService, 32, "aws4_request", strlen("aws4_request"));

    return kSigning;
}

char *aws_sigV4_sign(char *key, size_t key_len, char *value, size_t value_len)
{
    char *signature = (char *)malloc(32);
    mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
                    (unsigned char *)key, key_len, (unsigned char *)value,
                    value_len, (unsigned char *)signature);
    free(key);
    return signature;
}

char *aws_sigV4_create_canonical_request(char *method, char *canonical_uri,
                                         char *canonical_query_string,
                                         char *canonical_headers,
                                         char *x_amz_signed_headers,
                                         char *hashed_payload)
{
    char *canonical_request;
    asprintf(&canonical_request, "%s\n%s\n%s\n%s\n\n%s\n%s",
             method, canonical_uri, canonical_query_string, canonical_headers,
             x_amz_signed_headers, hashed_payload);

    char *hash = (char *)malloc(32);
    mbedtls_md(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
               (unsigned char *)canonical_request, strlen(canonical_request),
               (unsigned char *)hash);

    free(canonical_request);
    return hash;
}

char *aws_sigV4_create_string_to_sign(char *x_amz_date, char *x_amz_time,
                                      char *aws_region, char *aws_service,
                                      char *hashed_canonical_request)
{
    char *string_to_sign;
    asprintf(&string_to_sign, "AWS4-HMAC-SHA256\n%sT%sZ\n%s/%s/%s/aws4_request\n%s",
             x_amz_date, x_amz_time, x_amz_date, aws_region, aws_service,
             hashed_canonical_request);

    return string_to_sign;
}

char *aws_sigV4_create_canonical_query_string(
    char *access_key, char *x_amz_date, char *x_amz_time, char *aws_region,
    char *aws_service, char *x_amz_expires, char *x_amz_signed_headers,
    char *x_amz_security_token)
{
    int num_params = 5;

    if (strlen(x_amz_security_token) != 0)
        num_params++;

    char *queries[5] = {"X-Amz-Algorithm=AWS4-HMAC-SHA256"};

    char *credential;
    size_t credential_length =
        asprintf(&credential, "%s/%s/%s/%s/aws4_request", access_key,
                 x_amz_date, aws_region, aws_service);
    char *URL_credential = (char *)malloc(credential_length * 3 * sizeof(char));
    URL_credential = aws_sigV4_url_encode(credential);

    free(credential);

    char *x_amz_credential;
    asprintf(&x_amz_credential, "X-Amz-Credential=%s", URL_credential);

    queries[1] = x_amz_credential;

    char *x_amz_datetime_string;
    asprintf(&x_amz_datetime_string, "X-Amz-Date=%sT%sZ", x_amz_date, x_amz_time);

    queries[2] = x_amz_datetime_string;

    char *x_amz_expires_string;
    asprintf(&x_amz_expires_string, "X-Amz-Expires=%s", x_amz_expires);

    queries[3] = x_amz_expires_string;

    char *x_amz_security_token_string;
    if (strlen(x_amz_security_token) != 0)
    {
        char *URL_security_token = aws_sigV4_url_encode(x_amz_security_token);

        asprintf(&x_amz_security_token_string, "X-Amz-Security-Token=%s",
                 URL_security_token);

        queries[4] = x_amz_security_token_string;
        free(URL_security_token);
    }

    char *x_amz_signed_headers_string;
    asprintf(&x_amz_signed_headers_string, "X-Amz-SignedHeaders=%s",
             x_amz_signed_headers);

    // The signed headers query parameter must be last
    // Offset by one if the security token is present
    queries[(strlen(x_amz_security_token) != 0) ? 5 : 4] = x_amz_signed_headers_string;

    char *query_string = string_gen(queries, num_params, '&', 0);

    free(URL_credential);
    free(x_amz_credential);
    free(x_amz_expires_string);
    free(x_amz_signed_headers_string);
    free(x_amz_datetime_string);

    if (strlen(x_amz_security_token) != 0)
        free(x_amz_security_token_string);

    return query_string;
}

char *aws_sigV4_create_canonical_headers_string(char *host,
                                                char *x_amz_content_sha256,
                                                char *x_amz_date,
                                                char *x_amz_security_token)
{
    char *amz_host;
    char *content_sha256;
    char *amz_date;
    char *amz_security_token;

    int num_headers = 1;

    char *headers[4];

    asprintf(&amz_host, "host:%s", host);
    headers[0] = amz_host;

    if (strlen(x_amz_content_sha256) != 0)
    {
        asprintf(&content_sha256, "x-amz-content-sha256:%s", x_amz_content_sha256);
        headers[num_headers] = content_sha256;
        num_headers++;
    }
    if (strlen(x_amz_date) != 0)
    {
        asprintf(&amz_date, "x-amz-date:%s", x_amz_date);
        headers[num_headers] = amz_date;
        num_headers++;
    }
    if (strlen(x_amz_security_token) != 0)
    {
        asprintf(&amz_security_token, "x-amz-security-token:%s",
                 x_amz_security_token);
        headers[num_headers] = amz_security_token;
        num_headers++;
    }
    char *str = string_gen(headers, num_headers, '\n', 1);

    free(amz_host);
    if (strlen(x_amz_content_sha256) != 0)
        free(content_sha256);
    if (strlen(x_amz_date) != 0)
        free(amz_date);
    if (strlen(x_amz_security_token) != 0)
        free(amz_security_token);

    return str;
}

char *aws_sigV4_presign_url(char *access_key, char *secret_access_key,
                            char *x_amz_security_token, char *bucket,
                            char *object, char *aws_region, char *x_amz_date,
                            char *x_amz_time, char *x_amz_expires

)
{
    // Service is set as S3 because it wouldn't make sense to presign URLs for any other service
    char *signing_key = aws_sigV4_create_signing_key(
        secret_access_key, x_amz_date, aws_region, "s3");

    char *query_string = aws_sigV4_create_canonical_query_string(
        access_key, x_amz_date, x_amz_time, aws_region, "s3", x_amz_expires,
        "host", x_amz_security_token);

    char *host;
    asprintf(&host, "%s.s3.amazonaws.com", bucket);

    char *canonical_headers = aws_sigV4_create_canonical_headers_string(host, "", "", "");

    char *canonical_request_hash = aws_sigV4_create_canonical_request(
        "PUT", object, query_string, canonical_headers, "host", "UNSIGNED-PAYLOAD");

    char *hash_hex = aws_sigV4_to_hex_string(canonical_request_hash);

    char *string_to_sign = aws_sigV4_create_string_to_sign(
        x_amz_date, x_amz_time, aws_region, "s3", hash_hex);

    char *signature =
        aws_sigV4_sign(signing_key, 32, string_to_sign, strlen(string_to_sign));

    char *signature_hex = aws_sigV4_to_hex_string(signature);

    char *presigned_url;

    asprintf(&presigned_url,
             "https://%s.s3.amazonaws.com%s?%s&X-Amz-Signature=%s", bucket,
             object, query_string, signature_hex);

    free(host);
    free(query_string);
    free(canonical_headers);
    free(canonical_request_hash);
    free(hash_hex);
    free(string_to_sign);
    free(signature);
    free(signature_hex);

    return presigned_url;
}