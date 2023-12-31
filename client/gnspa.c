#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <arpa/inet.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include "liboath/oath.h"
#include "gnspa.h"

void generate_otp(const char *shared_secret, const char *machine_id, char *otp_value) {
    // Generate HMAC for machine_id
    unsigned char hmac_value[HMAC_SIZE];
    SHA256_CTX ctx;
    SHA256_Init(&ctx);
    SHA256_Update(&ctx, shared_secret, strlen(shared_secret));
    SHA256_Update(&ctx, machine_id, strlen(machine_id));
    SHA256_Final(hmac_value, &ctx);

    // Generate TOTP
    time_t now = time(NULL);
    char otp[OTP_SIZE];
    oath_totp_generate((const char *)hmac_value, HMAC_SIZE, now, 30, 0, OTP_SIZE, otp);

    // Copy TOTP value to the provided buffer
    memcpy(otp_value, otp, OTP_SIZE);

    char otp_str[OTP_SIZE + 1];
    otp_str[OTP_SIZE] = '\0';
    memcpy(otp_str, otp_value, OTP_SIZE);
    printf(" - OTP  [ %s ]\n", otp_str);
}

void generate_hmac(const char *shared_secret, const struct Packet *packet, unsigned char *hmac_value) {
    SHA256_CTX ctx;
    SHA256_Init(&ctx);
    SHA256_Update(&ctx, shared_secret, strlen(shared_secret));
    SHA256_Update(&ctx, packet, sizeof(struct Packet) - HMAC_SIZE);
    SHA256_Final(hmac_value, &ctx);
}

int spa_fill_packet(const char *shared_secret, const char *machine_id, const char *source_ip, struct Packet *packet) {
    // Validate source IP
    struct in_addr addr;
    if (inet_pton(AF_INET, source_ip, &addr) != 1) {
        fprintf(stderr, "Invalid source IP address format\n");
        return -1;
    }

    // Validate machine ID length
    size_t len = strlen(machine_id);
    if (len != MACHINE_ID_SIZE) {
        fprintf(stderr, "Invalid machine ID length\n");
        return -1;
    }

    // Generate nonce value (between 0 and 65535)
    uint16_t nonce = rand() % 65536;
    uint64_t timestamp = time(NULL);

    // Set packet field values
    memcpy(packet->machine_id, machine_id, MACHINE_ID_SIZE);
    packet->nonce = htons(nonce);
    packet->timestamp = timestamp;
    packet->source_ip = inet_addr(source_ip);

    generate_otp(shared_secret, machine_id, packet->totp_value);

    // Generate HMAC
    generate_hmac(shared_secret, packet, packet->hmac_value);

    printf(" - HMAC [ ");
    for (int i = 0; i < HMAC_SIZE; i++) {
        printf("%02x", packet->hmac_value[i]);
    }
    printf(" ]\n");    

    return 0; // Success
}

int spa_send_packet(const struct Packet *packet, const char *server_ip, int server_port) {
    // Create UDP socket
    int client_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (client_socket < 0) {
        perror("Failed to create socket");
        return -1;
    }

    // Set server address
    struct sockaddr_in server_address;
    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(server_port);
    if (inet_aton(server_ip, &server_address.sin_addr) == 0) {
        perror("Invalid server IP address");
        return -1;
    }

    // Send packet
    ssize_t sent_bytes = sendto(client_socket, packet, sizeof(struct Packet), 0,
                                (struct sockaddr *)&server_address, sizeof(server_address));
    if (sent_bytes < 0) {
        perror("Failed to send packet");
        return -1;
    }

    // Close the socket
    close(client_socket);

    return 0; // Success
}
