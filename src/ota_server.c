/**
 * RTL8710 OTA server.
 * Copyright (C) 2016  Tido Klaassen <tido@4gh.eu>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <getopt.h>

#define BUF_SIZE 256

#define CHUNK_SIZE 1460

static struct option long_options[] = {
        { "file",      required_argument, 0, 'f' },
        { "interface", required_argument, 0, 'i' },
        { "port",      required_argument, 0, 'p' },
        { "loop",      no_argument,       0, 'l' },
        { "help",      no_argument,       0, 'h' },
        { 0, 0, 0, 0 } };

void print_help(void)
{
    printf("Usage: upload_ota\n");
    printf("  -f  --file       the OTA image to be uploaded\n");
    printf("  -i  --interface  the network ip address\n");
    printf("  -p  --port       the network port\n");
    printf("  -l  --loop       do not exit after upload\n");
    printf("  -h  --help\n");
}

int run_server(char *path, char *addr, char *port, bool loop)
{
    uint32_t ota_hdr[3];
    struct addrinfo hints, *list, *lp;
    int fd, srv_sock, clnt_sock;
    unsigned int i;
    struct stat info;
    uint8_t *filebuf, *hdrbuf;
    uint32_t chksum;
    ssize_t chunk, total;
    int result;

    filebuf = NULL;
    srv_sock = -1;
    clnt_sock = -1;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = (AI_PASSIVE | AI_V4MAPPED | AI_ADDRCONFIG);

    result = getaddrinfo(addr, port, &hints, &list);
    if(result != 0){
        fprintf(stderr, "[%s] getaddrinfo: %s\n", 
                __func__, gai_strerror(result));
        goto err_out;
    }

    for(lp = list; lp != NULL; lp = lp->ai_next){
        srv_sock = socket(lp->ai_family, lp->ai_socktype, lp->ai_protocol);
        if(srv_sock < 0){
            continue;
        }

        result = bind(srv_sock, lp->ai_addr, lp->ai_addrlen);
        if(result == 0){
            break; /* Success */
        }

        close(srv_sock);
        srv_sock = -1;
    }

    if(lp == NULL){ /* No address succeeded */
        fprintf(stderr, "[%s] bind() failed\n", __func__);
        result = -1;
        goto err_out;
    }

    freeaddrinfo(list); 

    result = listen(srv_sock, 3);
    if(result != 0){
        goto err_out;
    }

    printf("OTA server running\n");
    printf("file: %s\n", path);
    printf("ip: %s\n", addr == NULL ? "INADDR_ANY" : addr);
    printf("port: %s\n", port);

    do{
        clnt_sock = accept(srv_sock, NULL, NULL);
        if(clnt_sock < 0){
            fprintf(stderr, "[%s] accept failed\n", __func__);
            result = -1;
            goto err_out;
        }
        
        fd = open(path, O_RDONLY);
        if(fd < 0){
            fprintf(stderr, "[%s] Failed to open file %s\n", __func__, path);
            result = -1;
            goto err_out;
        }

        result = fstat(fd, &info);
        if(result != 0){
            fprintf(stderr, "[%s] Failed to get info on file %s\n",
                        __func__, path);

            result = -1;
            goto err_out;
        }

        filebuf = malloc(info.st_size);
        if(filebuf == NULL){
            fprintf(stderr, "[%s] Failed to allocate file buffer\n", __func__);
            result = -1;
            goto err_out;
        }

        total = 0;
        chunk = 42;
        while(chunk > 0 && total < info.st_size){
            chunk = read(fd, filebuf + total, info.st_size - total);
            if(chunk < 0){
                fprintf(stderr, "[%s] Failed to read file %s\n",
                            __func__, path);

                result = -1;
                goto err_out;
            }

            total += chunk;
        }

        close(fd);

        chksum = 0;
        for(i = 0;i < info.st_size;++i){
            chksum += filebuf[i];
        }

        ota_hdr[0] = htole32(chksum);
        ota_hdr[1] = 0;
        ota_hdr[2] = htole32(info.st_size);

        printf("Sending OTA header: Checksum: 0x%08x Size: %ld\n", 
                    chksum, info.st_size);
        
        hdrbuf = (uint8_t *) &ota_hdr[0];
        total = 0;
        chunk = 42;
        while(total < sizeof(ota_hdr)){
            chunk = write(clnt_sock, hdrbuf + total, sizeof(ota_hdr) - total);
            if(chunk < 0){
                fprintf(stderr, "[%s] Failed to send header\n", __func__);
                result = -1;
                goto err_out;
            }

            total += chunk;
        }

        printf("Sending OTA file...\n");
        total = 0;
        chunk = 42;
        while(total < info.st_size){
            chunk = write(clnt_sock, filebuf + total, info.st_size - total);
            if(chunk < 0){
                fprintf(stderr, "[%s] Failed to send file\n", __func__);
                result = -1;
                goto err_out;
            }

            total += chunk;
        }

        result = close(clnt_sock);
        clnt_sock = -1;
        if(result != 0){
            fprintf(stderr, "[%s] error closing client socket\n", __func__);
            goto err_out;
        }

        free(filebuf);
        filebuf = NULL;
    }while(loop);

err_out: 
    if(filebuf != NULL){
        free(filebuf);
    }

    if(clnt_sock >= 0){
        close(clnt_sock);
    }
    
    if(srv_sock >= 0){
        close(srv_sock);
    }

    return result;
}

int main(int argc, char *argv[])
{
    char path[BUF_SIZE];
    char port[32];
    char addr_buf[32];
    char *addr;
    bool loop;
    int idx, opt;
    int result;

    path[0] = '\0';
    addr_buf[0] = '\0';
    addr = NULL;
    sprintf(port, "4711");
    loop = 0;
    result = 0;

    idx = 0;
    while(1){
        opt = getopt_long(argc, argv, "f:i:p:lh?", long_options, &idx);

        if(opt < 0){
            break;
        }

        switch (opt) {
        case 'f':
            sprintf(path, "%s", optarg);
            break;
        case 'i':
            strncpy(addr_buf, optarg, sizeof(addr_buf) - 1);
            addr = &(addr_buf[0]);
            break;
        case 'p':
            strncpy(port, optarg, sizeof(port) - 1);
            break;
        case 'l':
            loop = true;
            break;
        case 'h':
        case '?':
        default:
            print_help();
            goto err_out;
            break;
        }
    }

    if(strlen(port) == 0 || strlen(path) == 0){
        print_help();
        result = -1;
        goto err_out;
    }

    result = run_server(path, addr, port, loop);

err_out:
    exit(result == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
}

