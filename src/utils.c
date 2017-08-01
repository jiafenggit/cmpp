
/* 
 * China Mobile CMPP 2.0 Protocol Library
 * Copyright (C) 2017 typefo <typefo@qq.com>
 * Update: 2017-07-10
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <openssl/md5.h>
#include <iconv.h>
#include "packet.h"
#include "socket.h"
#include "utils.h"

int cmpp_init_sp(cmpp_sp_t *cmpp, char *host, unsigned short port) {
    if (!cmpp) {
        return -1;
    }

    int fd, err;
    cmpp->ok = false;
    cmpp->version = CMPP_VERSION;
    pthread_mutex_init(&cmpp->lock, NULL);

    /* Create a new socket */
    fd = cmpp_sock_create();
    if (fd < 1) {
        return 1;
    }

    /* Initialize the socket settings */
    cmpp_sock_init(&cmpp->sock, fd);

    /* Connect to server */
    err = cmpp_sock_connect(&cmpp->sock, host, port);
    if (err) {
        return 2;
    }

    /* TCP NONBLOCK */
    cmpp_sock_nonblock(&cmpp->sock, true);

    /* TCP NODELAY */
    cmpp_sock_tcpnodelay(&cmpp->sock, true);

    /* TCP KEEPAVLIE */
    cmpp_sock_keepavlie(&cmpp->sock, 30, 5, 3);

    return 0;
}

int cmpp_init_ismg(cmpp_ismg_t *cmpp, const char *addr, unsigned short port) {
    if (!cmpp) {
        return -1;
    }

    int fd, err;
    cmpp->version = CMPP_VERSION;
    pthread_mutex_init(&cmpp->lock, NULL);

    /* Create a new socket */
    fd = cmpp_sock_create();
    if (fd < 1) {
        return 1;
    }
    
    /* Initialize the socket settings */
    cmpp_sock_init(&cmpp->sock, fd);

    /* bind to address */
    err = cmpp_sock_bind(&cmpp->sock, addr, port, 1024);
    if (err) {
        return 2;
    }

    /* TCP NONBLOCK */
    cmpp_sock_nonblock(&cmpp->sock, true);

    /* TCP NODELAY */
    cmpp_sock_tcpnodelay(&cmpp->sock, true);

    /* TCP KEEPAVLIE */
    cmpp_sock_keepavlie(&cmpp->sock, 30, 5, 3);

    return 0;
}

int cmpp_sp_close(cmpp_sp_t *cmpp) {
    if (cmpp) {
        cmpp->ok = false;
        cmpp_sock_close(&cmpp->sock);
        return 0;
    }

    return -1;
}

int cmpp_ismg_close(cmpp_ismg_t *cmpp) {
    if (cmpp) {
        cmpp_sock_close(&cmpp->sock);
        return 0;
    }

    return -1;
}

int cmpp_send(cmpp_sock_t *sock, void *pack, size_t len) {
    int ret;
    cmpp_head_t *chp;

    chp = (cmpp_head_t *)pack;

    if (ntohl(chp->totalLength) > len) {
    	return CMPP_ERR_PROPACKLENERR;
    }

    ret = cmpp_sock_send(sock, (unsigned char *)pack, ntohl(chp->totalLength));

    if (ret != ntohl(chp->totalLength)) {
        switch (ret) {
        case -1:
            return -1;
        default:
            return CMPP_ERR_SOCKWRITEERR;
        }
    }
    
    return 0;
}

int cmpp_recv(cmpp_sock_t *sock, void *pack, size_t len) {
    int ret;
    cmpp_head_t *chp;
    int chpLen, pckLen;

    chpLen = sizeof(cmpp_head_t);
    
    if (len < chpLen) {
    	return CMPP_ERR_PROPACKLENERR;
    }

    ret = cmpp_sock_recv(sock, (unsigned char *)pack, chpLen);

    if (ret != chpLen) {
        switch (ret) {
        case -1:
            return -1;
        case 0:
            return CMPP_ERR_NODATA;
        default:
            return CMPP_ERR_PROPACKLENERR;
        }
    }

    chp = (cmpp_head_t *)pack;
    pckLen = ntohl(chp->totalLength);
    
    ret = cmpp_sock_recv(sock, (unsigned char *)pack + chpLen, pckLen - chpLen);
    if (ret != (pckLen - chpLen)) {
        switch (ret) {
        case -1:
            return -1;
        default:
            return CMPP_ERR_PROPACKLENERR;
        }
    }

    return 0;
}

int cmpp_add_header(cmpp_head_t *chp, unsigned int totalLength, unsigned int commandId, unsigned int sequenceId) {
    if (!chp) {
        return -1;
    }

    chp->totalLength = htonl(totalLength);
    chp->commandId = htonl(commandId);
    chp->sequenceId = htonl(sequenceId);

    return 0;
}

bool cmpp_check_method(void *pack, size_t len, unsigned int command) {
    if (pack && len >= sizeof(cmpp_head_t)) {
        cmpp_head_t *chp = (cmpp_head_t *)pack;
        if (ntohl(chp->commandId) == command) {
            return true;
        }
    }

    return false;
}

int cmpp_md5(unsigned char *md, unsigned char *src, unsigned int len) {
    if (md && src && len > 0) {
        MD5_CTX ctx;
        MD5_Init(&ctx);
        MD5_Update(&ctx, src, len);
        MD5_Final(md, &ctx);
        return 0;
    }

    return -1;
}

int cmpp_conv(const char *src, size_t slen, char *dst, size_t dlen, const char* fromcode, const char* tocode) {
    iconv_t cd;
    char *inbuf = (char *)src;
    size_t *inbytesleft = &slen;
    char *outbuf = dst;
    size_t *outbytesleft = &dlen;
    
    cd = iconv_open(tocode, fromcode);
    if (cd != (iconv_t)-1) {
        iconv(cd, &inbuf, inbytesleft, &outbuf, outbytesleft);
        iconv_close(cd);
        return 0;
    }

    return -1;
}

size_t cmpp_ucs2count(const char *src) {
    int i = 0;

    while (i < 140) {
        if (src[i] + src[i + 1] != 0) {
            i += 2;
        } else {
            break;
        }
    }

    return i;
}

const char *cmpp_get_error(cmpp_error_t code) {
    const char *error = "unknown error";
    if ((code >= 0) && (code < (sizeof(cmpp_error_strings) / sizeof(char *)))) {
        error = cmpp_error_strings[code];
    }
    
    return error;
}

int cmpp_msg_content(cmpp_msg_content_t *pack, size_t len, unsigned long long msgId, unsigned char *stat, unsigned char *submitTime,
                     unsigned char *doneTime, unsigned char *destTerminalId, unsigned int smscSequence) {

    pack->msgId = msgId;
    memcpy(pack->stat, stat, 7);
    memcpy(pack->submitTime, submitTime, 10);
    memcpy(pack->doneTime, submitTime, 10);
    memcpy(pack->destTerminalId, destTerminalId, 21);
    pack->smscSequence = smscSequence;

    return 0;
}

bool cmpp_check_authentication(cmpp_pack_t *pack, size_t size, const char *user, const char *password) {
    if (!pack || size < sizeof(cmpp_connect_t)) {
        return false;
    }

    cmpp_connect_t *ccp = (cmpp_connect_t *)pack;

    int len;
    char timestamp[11];
    unsigned char buff[128];
    unsigned char authenticatorSource[16];

    len = strlen(user) + 9 + strlen(password) + 10;
    if (len > sizeof(buff)) {
        return false;
    }
    
    memset(buff, 0, sizeof(buff));
    memcpy(buff, user, strlen(user));
    memcpy(buff + strlen(user) + 9, password, strlen(password));

    if (ntohl(ccp->timestamp) > 9999999999) {
        return false;
    }
    
    sprintf(timestamp, "%010u", ntohl(ccp->timestamp));
    memcpy(buff + strlen(user) + 9 + strlen(password), timestamp, 10);
    cmpp_md5(authenticatorSource, buff, len);

    if (memcmp(authenticatorSource, ccp->authenticatorSource, 16) != 0) {
        return false;
    }

    return true;
}

int cmpp_free_pack(cmpp_pack_t *pack) {
    if (pack == NULL) {
        return -1;
    }
    
    free(pack);
    pack = NULL;

    return 0;
}

bool cmpp_check_connect(cmpp_sock_t *sock) {
    if (!sock) {
        return false;
    }

    cmpp_pack_t pack;
    pthread_mutex_lock(&sock->wlock);
    pthread_mutex_lock(&sock->rlock);
    
    if (cmpp_active_test(sock) != 0) {
        return false;
    }

    if (cmpp_recv(sock, &pack, sizeof(cmpp_pack_t)) != 0) {
        return false;
    }

    pthread_mutex_unlock(&sock->wlock);
    pthread_mutex_unlock(&sock->rlock);
    
    if (!cmpp_check_method(&pack, sizeof(cmpp_pack_t), CMPP_ACTIVE_TEST_RESP)) {
        return false;
    }

    return true;
}

unsigned int cmpp_sequence(void) {
    static unsigned int seq = 1;
    return (seq < 0x7fffffff) ? (seq++) : (seq = 1);
}