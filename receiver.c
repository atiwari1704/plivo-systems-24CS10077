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
unsigned char payloads[65536][160];
uint64_t nack_time[65536];
int out_fd;
struct sockaddr_in player;

void deliver(uint32_t seq) {
    unsigned char out_buf[164];
    uint32_t net_seq = htonl(seq);
    memcpy(out_buf, &net_seq, 4);
    memcpy(out_buf + 4, payloads[seq % 65536], 160);
    sendto(out_fd, out_buf, 164, 0, (struct sockaddr *)&player, sizeof player);
}

int main(void) {
    int in_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in in_addr = {0};
    in_addr.sin_family = AF_INET;
    in_addr.sin_port = htons(47002);
    in_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    bind(in_fd, (struct sockaddr *)&in_addr, sizeof in_addr);

    out_fd = socket(AF_INET, SOCK_DGRAM, 0);
    memset(&player, 0, sizeof(player));
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
        tv.tv_usec = 5000; 

        int ret = select(in_fd + 1, &readfds, NULL, NULL, &tv);

        if (ret > 0 && FD_ISSET(in_fd, &readfds)) {
            ssize_t n = recvfrom(in_fd, buf, sizeof buf, 0, NULL, NULL);
            
            if (n == 164) {
                uint32_t seq;
                memcpy(&seq, buf, 4);
                seq = ntohl(seq);

                if (!initialized) { highest_seq = seq; initialized = true; }
                if ((int32_t)(seq - highest_seq) > 0) { highest_seq = seq; }

                if (!seen[seq % 65536]) {
                    seen[seq % 65536] = true;
                    memcpy(payloads[seq % 65536], buf + 4, 160);
                    deliver(seq);

                    uint32_t start = (highest_seq >= 50) ? (highest_seq - 50) : 0;
                    for (uint32_t i = start; i < highest_seq; i++) {
                        if (!seen[i % 65536]) {
                            uint32_t net_i = htonl(i);
                            sendto(nack_fd, &net_i, 4, 0, (struct sockaddr *)&relay_fb, sizeof relay_fb);
                            nack_time[i % 65536] = get_time_ms() + 50;
                        }
                    }
                }
            } 
            else if (n == 168) {
                uint32_t marker;
                memcpy(&marker, buf, 4);
                if (ntohl(marker) == 0x80000000) {
                    uint32_t base_seq;
                    memcpy(&base_seq, buf + 4, 4);
                    base_seq = ntohl(base_seq);
                    
                    uint32_t s0 = base_seq;
                    uint32_t s1 = base_seq + 1;
                    bool seen0 = seen[s0 % 65536];
                    bool seen1 = seen[s1 % 65536];

                    if (seen0 && !seen1) {
                        for(int i = 0; i < 160; i++) payloads[s1 % 65536][i] = payloads[s0 % 65536][i] ^ buf[8+i];
                        seen[s1 % 65536] = true;
                        deliver(s1);
                        if ((int32_t)(s1 - highest_seq) > 0) { highest_seq = s1; }
                    } else if (!seen0 && seen1) {
                        for(int i = 0; i < 160; i++) payloads[s0 % 65536][i] = payloads[s1 % 65536][i] ^ buf[8+i];
                        seen[s0 % 65536] = true;
                        deliver(s0);
                    }
                }
            }
        }

        if (initialized) {
            uint64_t now = get_time_ms();
            uint32_t start = (highest_seq >= 50) ? (highest_seq - 50) : 0;
            for (uint32_t i = start; i < highest_seq; i++) {
                if (!seen[i % 65536] && now >= nack_time[i % 65536]) {
                    uint32_t net_i = htonl(i);
                    sendto(nack_fd, &net_i, 4, 0, (struct sockaddr *)&relay_fb, sizeof relay_fb);
                    nack_time[i % 65536] = now + 50; 
                }
            }
        }
    }
    return 0;
}