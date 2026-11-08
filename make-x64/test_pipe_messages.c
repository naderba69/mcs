#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "../src/pipe.h"

static int failures;

#define CHECK(condition, description) do { \
    if (condition) printf("PASS: %s\n", description); \
    else { printf("FAIL: %s\n", description); failures++; } \
} while (0)

int main(void)
{
    uint8_t message[1 + sizeof(struct pipe_card_snapshot)];
    struct pipe_card_snapshot source, decoded;
    uint32_t client_id = 0, decoded_id = 0;
    uint32_t server_id = 0, decoded_server_id = 0;
    uint64_t connection_generation = 0, decoded_generation = 0;
    int len, i;

    len = pipe_encode_u32_message(message, sizeof(message), PIPE_CLI_CONNECTED,
                                  0x12345678u);
    CHECK(len == (int)PIPE_CLIENT_ID_LEN &&
          pipe_decode_u32_message(message, (size_t)len, PIPE_CLI_CONNECTED,
                                  &decoded_id) && decoded_id == 0x12345678u,
          "client-connect pipe message encodes and decodes a stable id");
    CHECK(!pipe_decode_u32_message(message, (size_t)len - 1u,
                                   PIPE_CLI_CONNECTED, &decoded_id),
          "truncated client-id message is rejected");
    CHECK(!pipe_decode_u32_message(message, (size_t)len,
                                   PIPE_CARD_ADD, &decoded_id),
          "client-id message with the wrong command is rejected");

    server_id = 0x12345678u;
    connection_generation = 0x1020304050607080ULL;
    len = pipe_encode_server_connected(message, sizeof(message), server_id,
                                       connection_generation);
    CHECK(len == (int)PIPE_SERVER_CONNECTED_LEN &&
          pipe_decode_server_connected(message, (size_t)len,
                                       &decoded_server_id,
                                       &decoded_generation) &&
          decoded_server_id == server_id &&
          decoded_generation == connection_generation,
          "server-connect event transports a stable ID and generation");
    CHECK(!pipe_decode_server_connected(message, (size_t)len - 1u,
                                        &decoded_server_id,
                                        &decoded_generation),
          "truncated server-connect event is rejected");
    CHECK(!pipe_decode_server_connected(message, (size_t)len + 1u,
                                        &decoded_server_id,
                                        &decoded_generation),
          "oversized server-connect event is rejected");
    CHECK(!pipe_encode_server_connected(message, sizeof(message), 0,
                                        connection_generation),
          "server-connect encoder rejects a zero server ID");
    CHECK(!pipe_encode_server_connected(message, sizeof(message), server_id, 0),
          "server-connect encoder rejects a zero connection generation");
    len = pipe_encode_server_connected(message, sizeof(message), server_id,
                                       connection_generation);
    memset(message + 1, 0, sizeof(server_id));
    CHECK(!pipe_decode_server_connected(message, (size_t)len,
                                        &decoded_server_id, &decoded_generation),
          "server-connect decoder rejects a zero server ID");
    len = pipe_encode_server_connected(message, sizeof(message), server_id,
                                       connection_generation);
    memset(message + 1 + sizeof(server_id), 0, sizeof(connection_generation));
    CHECK(!pipe_decode_server_connected(message, (size_t)len,
                                        &decoded_server_id, &decoded_generation),
          "server-connect decoder rejects a zero connection generation");
    CHECK(!pipe_encode_server_connected(message, PIPE_SERVER_CONNECTED_LEN - 1u,
                                        server_id, connection_generation),
          "server-connect encoder rejects insufficient capacity");

    memset(&source, 0, sizeof(source));
    source.profile_id = 0x10203040u;
    source.caid = 0x0500u;
    source.provider_count = 3;
    source.share_flags = PIPE_CARD_SHARE_CCCAM | PIPE_CARD_SHARE_MGCAMD;
    for (i = 0; i < source.provider_count; i++)
        source.provider_ids[i] = 0x010203u + (uint32_t)i;
    len = pipe_encode_card_message(message, sizeof(message), PIPE_CARD_ADD,
                                   &source);
    memset(&decoded, 0, sizeof(decoded));
    CHECK(len == (int)sizeof(message) &&
          pipe_decode_card_message(message, (size_t)len, PIPE_CARD_ADD,
                                   &decoded) &&
          decoded.profile_id == source.profile_id &&
          decoded.caid == source.caid &&
          decoded.provider_count == source.provider_count &&
          decoded.share_flags == source.share_flags &&
          memcmp(decoded.provider_ids, source.provider_ids,
                 sizeof(source.provider_ids)) == 0,
          "card event transports profile/card fields by value without pointers");
    CHECK(!pipe_decode_card_message(message, (size_t)len - 1u,
                                    PIPE_CARD_ADD, &decoded),
          "truncated card event is rejected");
    CHECK(!pipe_decode_card_message(message, (size_t)len,
                                    PIPE_CARD_DEL, &decoded),
          "card event with the wrong command is rejected");

    source.provider_count = PIPE_CARD_MAX_PROVIDERS + 1u;
    CHECK(!pipe_encode_card_message(message, sizeof(message), PIPE_CARD_ADD,
                                    &source),
          "card event encoder rejects provider-count overflow");
    source.provider_count = 0;
    source.share_flags = 0x80u;
    CHECK(!pipe_encode_card_message(message, sizeof(message), PIPE_CARD_DEL,
                                    &source),
          "card event encoder rejects unknown share flags");

    len = pipe_encode_u32_message(message, 0, PIPE_CLI_CONNECTED, client_id);
    CHECK(len == 0, "client-id encoder rejects insufficient output space");
    CHECK(sizeof(struct pipe_card_snapshot) < 504u,
          "value-only card snapshot fits the atomic pipe-frame payload limit");

    printf("%d pipe-message checks failed\n", failures);
    return failures ? 1 : 0;
}
