/* BASELINE RECEIVER (C) — naive on purpose. Rewrite it (C, C++, Go, or Rust).
 *
 * Ports (all 127.0.0.1):
 *   bind 47002  <- media from your sender, via the hostile relay
 *   send 47020  -> harness player. MUST be: 4-byte big-endian seq +
 *                  160-byte payload. Frame i counts only if it arrives
 *                  BEFORE its deadline t0 + DELAY_MS + i*20ms.
 *   send 47003  -> feedback to your sender, via the relay (optional)
 *
 * This baseline forwards whatever arrives straight to the player: lost
 * frames stay lost, late frames stay late, duplicates are re-sent
 * harmlessly. All yours to fix — jitter buffer, reordering, recovery.
 *
 * Env vars available: T0, DURATION_S, DELAY_MS. Harness kills the process
 * at run end; a forever-loop is fine.
 */
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/select.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

uint64_t get_time_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

bool seen[65536];
uint64_t nack_time[65536];

int main(void) {
    int in_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in in_addr = {0};
    in_addr.sin_family = AF_INET;
    in_addr.sin_port = htons(47002);
    in_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    bind(in_fd, (struct sockaddr *)&in_addr, sizeof in_addr);

    int out_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in player = {0};
    player.sin_family = AF_INET;
    player.sin_port = htons(47020);
    player.sin_addr.s_addr = inet_addr("127.0.0.1");

    int nack_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in relay_fb = {0};
    relay_fb.sin_family = AF_INET;
    relay_fb.sin_port = htons(47003);
    relay_fb.sin_addr.s_addr = inet_addr("127.0.0.1");

    memset(seen, 0, sizeof(seen));
    memset(nack_time, 0, sizeof(nack_time));

    uint32_t highest_seq = 0;
    bool initialized = false;
    unsigned char buf[2048];
    fd_set readfds;
    struct timeval tv;

    for (;;) {
        FD_ZERO(&readfds);
        FD_SET(in_fd, &readfds);
        tv.tv_sec = 0;
        tv.tv_usec = 5000; // 5ms timeout for background NACK scanner

        int ret = select(in_fd + 1, &readfds, NULL, NULL, &tv);

        if (ret > 0 && FD_ISSET(in_fd, &readfds)) {
            ssize_t n = recvfrom(in_fd, buf, sizeof buf, 0, NULL, NULL);
            if (n >= 4) {
                uint32_t seq;
                memcpy(&seq, buf, 4);
                seq = ntohl(seq);

                if (!initialized) {
                    highest_seq = seq;
                    initialized = true;
                }

                if (!seen[seq % 65536]) {
                    seen[seq % 65536] = true;
                    // Forward immediately to beat the deadline
                    sendto(out_fd, buf, (size_t)n, 0, (struct sockaddr *)&player, sizeof player);

                    if ((int32_t)(seq - highest_seq) > 0) {
                        for (uint32_t i = highest_seq + 1; i < seq; i++) {
                            seen[i % 65536] = false;
                            uint32_t net_i = htonl(i);
                            sendto(nack_fd, &net_i, 4, 0, (struct sockaddr *)&relay_fb, sizeof relay_fb);
                            nack_time[i % 65536] = get_time_ms() + 100;
                        }
                        highest_seq = seq;
                    }
                }
            }
        }

        if (initialized) {
            uint64_t now = get_time_ms();
            uint32_t start = (highest_seq >= 100) ? (highest_seq - 100) : 0;
            for (uint32_t i = start; i < highest_seq; i++) {
                if (!seen[i % 65536]) {
                    if (now >= nack_time[i % 65536]) {
                        uint32_t net_i = htonl(i);
                        sendto(nack_fd, &net_i, 4, 0, (struct sockaddr *)&relay_fb, sizeof relay_fb);
                        nack_time[i % 65536] = now + 100;
                    }
                }
            }
        }
    }
    return 0;
}