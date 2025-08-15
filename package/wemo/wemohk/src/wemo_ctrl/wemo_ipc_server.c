#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <assert.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <semaphore.h>

#include "wemo_ipc_server.h"
#include "wemo_event_ctrl.h"
#include "wemo_net_ctrl.h"
#include "wemo_firm_ctrl.h"
#include "wemo_name_ctrl.h"
#include "wemo_set_name_value.h"
#include "wemo_reset_ctrl.h"
#include "wemo_dev_info.h"
#include "wemo_insight.h"
#include "logger.h"

#define MAX_CLIENTS         4
#define LISTEN_QUEUE        16
#define SENDBUF_SIZE        2048
#define RECVBUF_SIZE        2048

typedef enum { DISCONNECTED, CONNECTED, WAIT_FOR_MSG } ProcessingState;

typedef struct {
    int sockfd;
    ProcessingState state;
    char sendbuf[SENDBUF_SIZE];
    char recvbuf[RECVBUF_SIZE];
    int sendlen;
    int recvoff;
    sem_t sem;
} peer_state_t;

typedef struct {
    bool want_read;
    bool want_write;
} fd_status_t;

peer_state_t global_state[MAX_CLIENTS];

const fd_status_t fd_status_R = {.want_read = true, .want_write = false};
const fd_status_t fd_status_W = {.want_read = false, .want_write = true};
const fd_status_t fd_status_RW = {.want_read = true, .want_write = true};
const fd_status_t fd_status_NORW = {.want_read = false, .want_write = false};

int epollfd = 0;

static int run_server = 0;
static int connection_no = 0;

static int listen_socket(void)
{
    int i = 0;
    int sock = -1;
    struct sockaddr_un server;

    unlink(SOCKET_NAME);

    for (i = 0; i < MAX_CLIENTS; i++) {
        global_state[i].sockfd = -1;
        global_state[i].state = DISCONNECTED;
        memset(global_state[i].sendbuf, 0x00, SENDBUF_SIZE);
        memset(global_state[i].recvbuf, 0x00, RECVBUF_SIZE);
        global_state[i].sendlen = 0;
        global_state[i].recvoff = 0;
    }

    sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        APP_LOG("WEMOHK", LOG_ERR, "error : opening stream socket");
        return -1;
    }

    // This helps avoid spurious EADDRINUSE when the previous instance of this
    // server died.
    int opt = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        APP_LOG("WEMOHK", LOG_ERR, "error : setsockopt");
        return -1;
    }

    server.sun_family = AF_UNIX;
    strcpy(server.sun_path, SOCKET_NAME);

    if (bind(sock, (struct sockaddr *) &server, sizeof(struct sockaddr_un))) {
        APP_LOG("WEMOHK", LOG_ERR, "error: binding stream socket");
        return -1;
    }

    APP_LOG("WEMOHK", LOG_ERR, "socket name %s", server.sun_path);

    listen(sock, LISTEN_QUEUE);

    return sock;
}

static int make_socket_non_blocking(int sockfd)
{
    int flags = fcntl(sockfd, F_GETFL, 0);

    if (flags == -1) {
        APP_LOG("WEMOHK", LOG_ERR, "error : fcntl F_GETFL");
        return -1;
    }

    if (fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) == -1) {
        APP_LOG("WEMOHK", LOG_ERR, "error: fcntl F_SETFL O_NONBLOCK");
        return -1;
    }

    return 0;
}

static fd_status_t on_peer_connected(int connection_no, int sockfd)
{
    int i = 0;

    assert(connection_no < MAX_CLIENTS);
    /* find the empty global_state */
    for (i = 0; i < MAX_CLIENTS; i++) {
        peer_state_t* peerstate = &global_state[i];
        if (peerstate->sockfd == -1) {
            //Initialize state
            peerstate->sockfd = sockfd;
            peerstate->state = CONNECTED;
            memset(peerstate->sendbuf, 0x00, SENDBUF_SIZE);
            memset(peerstate->recvbuf, 0x00, RECVBUF_SIZE);
            peerstate->sendlen = 0;
            peerstate->recvoff = 0;
            if (sem_init(&global_state[i].sem, 0, 1) == 0) {
                APP_LOG("WEMOHK", LOG_DEBUG, "send semaphore initialized for %d.", i);
            }
            else {
                APP_LOG("WEMOHK", LOG_ERR, "send semaphore initialization failed for %d.", i);
                exit(1);
            }
            break;
        }
    }

    if (i == MAX_CLIENTS) {
        APP_LOG("WEMOHK", LOG_ERR, "No empty slot - too many clients");
        exit(1);
    }
    APP_LOG("WEMOHK", LOG_DEBUG, "peer client connected to wemo_ctrl service : connection slot = [%d], sockfd = [%d]",
                      i, sockfd);

    // Signal that this socket is ready for reading now.
    return fd_status_R;
}

static void on_peer_closed(int sockfd)
{
    int i = 0;

    for (i = 0; i < MAX_CLIENTS; i++) {
        peer_state_t* peerstate = &global_state[i];
        if (peerstate->sockfd == sockfd) {
            close(sockfd);
            peerstate->sockfd = -1;
            peerstate->state = DISCONNECTED;
            peerstate->sendbuf[0] = 0x00;
            peerstate->sendlen = 0;
            peerstate->recvoff = 0;
            connection_no--;
            APP_LOG("WEMOHK", LOG_DEBUG, "Destroying semaphore = %d.", i);
            sem_destroy(&peerstate->sem);
        }
    }
}

static fd_status_t on_peer_ready_recv(int sockfd)
{
    int i = 0;
    int rc = 0;

    struct we_ipc_hdr *ipchdr;
    struct we_state *state;
    char *ipc_data;

    assert(connection_no < MAX_CLIENTS);

    for (i = 0; i < MAX_CLIENTS; i++) {
        peer_state_t* peerstate = &global_state[i];
        if (peerstate->sockfd == -1) {
            continue;
        }
        if (peerstate->sockfd == sockfd) {
            /* read ipc_data header */
            if (peerstate->recvoff == 0) {
                if ((rc = read(sockfd, peerstate->recvbuf, sizeof(struct we_ipc_hdr))) < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        // The socket is not really ready for recv, wait until it is.
                        return fd_status_R;
                    }
                    else {
                        APP_LOG("WEMOHK", LOG_ERR, "ready_recv : failed to read stream message");
                        return fd_status_NORW;
                    }
                }
                else if (rc == 0) {
                    // The peer client is disconnected.
                    peerstate->state = DISCONNECTED;
                    APP_LOG("WEMOHK", LOG_INFO, "wemo_ctrl (wemo_ipc_server) connection from client ends.\n");
                    return fd_status_NORW;
                }
                ipchdr = (struct we_ipc_hdr *)peerstate->recvbuf;

                if (ipchdr->size > IPC_DATA_MAX) {
                    APP_LOG("WEMOHK", LOG_ERR, "Invalid ipc_data size");
                    return fd_status_NORW;
                }
            } else if (peerstate->recvoff == sizeof(struct we_ipc_hdr)) {
                ipchdr = (struct we_ipc_hdr *)peerstate->recvbuf;
            } else {
                APP_LOG("WEMOHK", LOG_ERR, "Error unexpected data format in ipc_server");
                return fd_status_NORW;
            }
            peerstate->recvoff = sizeof(struct we_ipc_hdr);
            /* read ipc_data body */
            if (ipchdr->size > 0) {
                if ((rc = read(sockfd, peerstate->recvbuf + peerstate->recvoff, ipchdr->size)) < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        // The socket is not really ready for recv, wait until it is.
                        return fd_status_R;
                    }
                    else {
                        APP_LOG("WEMOHK", LOG_ERR, "ready_recv : failed to read stream message");
                        return fd_status_NORW;
                    }
                }
                else if (rc == 0) {
                    // The peer client is disconnected.
                    peerstate->state = DISCONNECTED;
                    APP_LOG("WEMOHK", LOG_INFO, "wemo_ctrl (wemo_ipc_server) connection from client ends");
                    return fd_status_NORW;
                }
            }
            ipc_data = peerstate->recvbuf + peerstate->recvoff;
            peerstate->recvoff = 0;

            switch (ipchdr->cmd) {
            case CMD_SETUP:
                APP_LOG("WEMOHK", LOG_INFO, "wemo_ctrl (wemo_ipc_server) setup command");
                wemoCtrlPointNetworkSetup(ipchdr->wemo_id, (struct we_conn_data *)ipc_data);
                break;
            case CMD_CLOSESETUP:
                APP_LOG("WEMOHK", LOG_INFO, "wemo_ctrl (wemo_ipc_server) closesetup command");
                wemoCtrlPointCloseSetup(ipchdr->wemo_id);
                break;
            case CMD_CONNECTION_STATE:
                APP_LOG("WEMOHK", LOG_INFO, "wemo_ctrl (wemo_ipc_server) get connection state command");
                wemoCtrlPointGetNetworkStatus(ipchdr->wemo_id, (struct we_network_status *)ipc_data);
                break;
            case CMD_SET:
                state = (struct we_state *)ipc_data;
                APP_LOG("WEMOHK", LOG_INFO, "wemo_ctrl (wemo_ipc_server) set command: wemo_id = %d, is_online = %d, state = %d, level = %d",
                       ipchdr->wemo_id, state->is_online, state->state, state->level);
                wemoCtrlPointTriggerAction(ipchdr->wemo_id, state, 1);
                break;
            case CMD_GET:
                state = (struct we_state *)ipc_data;
                APP_LOG("WEMOHK", LOG_INFO, "wemo_ctrl (wemo_ipc_server) get command: wemo_id = %d, is_online = %d, state = %d, level = %d",
                       ipchdr->wemo_id, state->is_online, state->state, state->level);
                wemoCtrlPointRetrieveState(ipchdr->wemo_id, state);
                break;
            case CMD_DELETE:
                state = (struct we_state *)ipc_data;
                APP_LOG("WEMOHK", LOG_INFO, "wemo_ctrl (wemo_ipc_server) delete command: wemo_id = %d",
                       ipchdr->wemo_id);
                wemoCtrlPointDeleteDevice(ipchdr->wemo_id);
                break;
            case CMD_DISCOVER:
                APP_LOG("WEMOHK", LOG_INFO, "wemo_ctrl (wemo_ipc_server) discover command");
                wemoCtrlPointRefresh();
                break;
            case CMD_FIRMWARE_UPDATE:
                APP_LOG("WEMOHK", LOG_INFO, "wemo_ctrl (wemo_ipc_server) firmware update command");
                wemoCtrlPointFirmwareUpdate(ipchdr->wemo_id, (struct we_firmware_data *)ipc_data);
                break;
            case CMD_SET_HKSETUP_STATE:
                APP_LOG("WEMOHK", LOG_INFO, "wemo_ctrl (wemo_ipc_server) set HK setup state");
                wemoCtrlPointSetHKSetupState(ipchdr->wemo_id, (struct we_hksetup_state *)ipc_data);
                break;
            case CMD_CHANGE_NAME:
                APP_LOG("WEMOHK", LOG_INFO, "wemo_ctrl (wemo_ipc_server) change friendly name");
                wemoCtrlPointChangeName(ipchdr->wemo_id, (struct we_name_change *)ipc_data);
                break;
            case CMD_NAME_VALUE:
                APP_LOG("WEMOHK", LOG_INFO, "wemo_ctrl (wemo_ipc_server) name value");
                wemoCtrlPointSetNameValue(ipchdr->wemo_id, (struct we_name_value *)ipc_data);
                break;
            case CMD_RESET:
                APP_LOG("WEMOHK", LOG_INFO, "wemo_ctrl (wemo_ipc_server) reset");
                wemoCtrlPointReset(ipchdr->wemo_id, (struct we_reset *)ipc_data);
                break;
            case CMD_RESTART_RULE:
                APP_LOG("WEMOHK", LOG_INFO, "wemo_ctrl (wemo_ipc_server) rule restart command");
                wemoCtrlPointRestartRule(ipchdr->wemo_id);
                break;
            case CMD_GET_DEVINFO:
                APP_LOG("WEMOHK", LOG_INFO, "wemo_ctrl (wemo_ipc_server) getInformation command");
                wemoCtrlGetInformation(ipchdr->wemo_id);
                break;
            case CMD_GET_INSIGHTHOME_SETTINGS:
                APP_LOG("WEMOHK", LOG_INFO, "wemo_ctrl (wemo_ipc_server) GetInsightHomeSettings command");
                wemoCtrlGetInsightHomeSettings(ipchdr->wemo_id);
                break;
            case CMD_SET_INSIGHTHOME_SETTINGS:
                APP_LOG("WEMOHK", LOG_INFO, "wemo_ctrl (wemo_ipc_server) SetInsightHomeSettings command");
                wemoCtrlSetInsightHomeSettings(ipchdr->wemo_id, (struct we_insight_home_settings *)ipc_data);
                break;
            case CMD_GET_INSIGHT_PARAMS:
                APP_LOG("WEMOHK", LOG_INFO, "wemo_ctrl (wemo_ipc_server) GetInsightParams command");
                wemoCtrlGetInsightParams(ipchdr->wemo_id);
                break;
            case CMD_SET_POWER_THRESHOLD:
                APP_LOG("WEMOHK", LOG_INFO, "wemo_ctrl (wemo_ipc_server) SetPowerThreshold command");
                wemoCtrlSetPowerThreshold(ipchdr->wemo_id, (struct we_insight_threshold *)ipc_data);
                break;
            case CMD_GET_POWER_THRESHOLD:
                APP_LOG("WEMOHK", LOG_INFO, "wemo_ctrl (wemo_ipc_server) GetPowerThreshold command");
                wemoCtrlGetPowerThreshold(ipchdr->wemo_id);
                break;
            case CMD_GET_DATA_EXPORTINFO:
                APP_LOG("WEMOHK", LOG_INFO, "wemo_ctrl (wemo_ipc_server) GetDataExportInfo command");
                wemoCtrlGetDataExportInfo(ipchdr->wemo_id);
                break;
            case CMD_SCHEDULE_DATA_EXPORT:
                APP_LOG("WEMOHK", LOG_INFO, "wemo_ctrl (wemo_ipc_server) ScheduleDataExport command");
                wemoCtrlScheduleDataExport(ipchdr->wemo_id, (struct we_insight_export *)ipc_data);
                break;
            default:
                APP_LOG("WEMOHK", LOG_DEBUG, "wemo_ctrl (wemo_ipc_server) Invalid command!\n");
                break;
            }
        }
    }

    return fd_status_RW;
}

static fd_status_t on_peer_ready_send(int sockfd)
{
    int i = 0;
    int sem_value = 0;

    assert(connection_no < MAX_CLIENTS);

    for (i = 0; i < MAX_CLIENTS; i++) {
        peer_state_t* peerstate = &global_state[i];
        if (peerstate->sockfd == -1) {
            continue;
        }
        if (peerstate->sockfd == sockfd) {
            APP_LOG("WEMOHK", LOG_DEBUG, "sockfd = %d, sendlen = %d", peerstate->sockfd, peerstate->sendlen);

            if (peerstate->sendlen == 0) {
                return fd_status_R;
            }

            int pos = 0;

            while (1) {
                int nsent = send(sockfd, &peerstate->sendbuf[pos], peerstate->sendlen, 0);
                if (nsent == -1) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        APP_LOG("WEMOHK", LOG_CRIT, "Posting send semaphore...");
                        sem_post(&peerstate->sem);
                        sem_getvalue(&peerstate->sem, &sem_value);
                        APP_LOG("WEMOHK", LOG_CRIT, "i = %d, semaphore value = %d", i, sem_value);
                        return fd_status_W;
                    }
                    else {
                        APP_LOG("WEMOHK", LOG_ERR, "ready_send : failed to send data");
                        APP_LOG("WEMOHK", LOG_CRIT, "Posting send semaphore...");
                        sem_post(&peerstate->sem);
                        sem_getvalue(&peerstate->sem, &sem_value);
                        APP_LOG("WEMOHK", LOG_CRIT, "i = %d, semaphore value = %d", i, sem_value);
                        return fd_status_NORW;
                    }
                }

                if (nsent < peerstate->sendlen) {
                    APP_LOG("WEMOHK", LOG_DEBUG, "need to send more data = [%d]", peerstate->sendlen - nsent);
                    pos = nsent;
                    peerstate->sendlen = peerstate->sendlen - nsent;
                    continue;
                }
                else {
                    // Everything was sent successfully; reset the send queue.
                    memset(peerstate->sendbuf, 0x00, SENDBUF_SIZE);
                    peerstate->sendlen = 0;

                    APP_LOG("WEMOHK", LOG_CRIT, "Posting send semaphore...");
                    sem_post(&peerstate->sem);
                    sem_getvalue(&peerstate->sem, &sem_value);
                    APP_LOG("WEMOHK", LOG_CRIT, "i = %d, semaphore value = %d", i, sem_value);
                    return fd_status_RW;
                }
            }
        }
    }
    return fd_status_R;
}

void *wemo_ipc_server(void *args)
{
    int i = 0;
    int listener_sock;

    listener_sock = listen_socket();

    APP_LOG("WEMOHK", LOG_INFO, "Starting ipc_server - listener_sock = [%d]", listener_sock);
    if (listener_sock == -1) {
        exit(1);
    }

    make_socket_non_blocking(listener_sock);

    epollfd = epoll_create1(0);

    if (epollfd < 0) {
        APP_LOG("WEMOHK", LOG_ERR, "error : failed to call epoll_create1");
        exit(1);
    }

    APP_LOG("WEMOHK", LOG_DEBUG, "epollfd = [%d]", epollfd);

    struct epoll_event accept_event;

    memset(&accept_event, 0, sizeof(struct epoll_event));
    accept_event.data.fd = listener_sock;
    accept_event.events = EPOLLIN;

    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, listener_sock, &accept_event) < 0) {
        APP_LOG("WEMOHK", LOG_ERR, "error: failed to call epoll_ctl EPOLL_CTL_ADD");
        exit(1);
    }

    struct epoll_event* events = calloc(MAX_CLIENTS, sizeof(struct epoll_event));
    if (events == NULL) {
        APP_LOG("WEMOHK", LOG_ERR, "Unable to allocate memory for epoll_events");
        exit(1);
    }

    while (1) {
        int nready = epoll_wait(epollfd, events, MAX_CLIENTS, -1);

        if (nready != 0)
            APP_LOG("WEMOHK", LOG_DEBUG, "nready = [%d]", nready);

        for (i = 0; i < nready; i++) {
            if (events[i].events & EPOLLERR) {
                APP_LOG("WEMOHK", LOG_ERR, "error : epoll_wait returned EPOLLERR");
                close(events[i].data.fd);
                events[i].data.fd = -1;
                continue;
            }

            APP_LOG("WEMOHK", LOG_DEBUG, "events[%d].data.fd = [%d]\n", i, events[i].data.fd);

            if (events[i].data.fd == listener_sock) {
                APP_LOG("WEMOHK", LOG_DEBUG, "listen and accept socket");
                // This listening socket is ready, it means that new peer client is conneting to the wemo_ctrl's ipc server.
                int msgsock = accept(listener_sock, 0, 0);
                if (msgsock < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        // This can happen due to the nonblocking socket mode; in this
                        // case don't do anything, but print a notice
                        APP_LOG("WEMOHK", LOG_ERR, "accept returned EAGAIN or EWOULDBLOCK");
                    }
                    else {
                        APP_LOG("WEMOHK", LOG_ERR,  "error : failed to accept a new connection from the peer");
                    }
                }
                else {
                    APP_LOG("WEMOHK", LOG_DEBUG, "making the msgsock = [%d] as nonblocking mode", msgsock);
                    make_socket_non_blocking(msgsock);
                    if (connection_no >= MAX_CLIENTS) {
                        APP_LOG("WEMOHK", LOG_ERR, "connection number (%d) >= MAX_CLIENTS (%d)", connection_no, MAX_CLIENTS);
                    }

                    // Ready to read data from peer client.
                    fd_status_t status = on_peer_connected(connection_no, msgsock);

                    struct epoll_event event = {0};
                    event.data.fd = msgsock;

                    if (status.want_read) {
                        event.events |= EPOLLIN;
                    }
                    if (status.want_write) {
                        event.events |= EPOLLOUT;
                    }

                    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, msgsock, &event) < 0) {
                        APP_LOG("WEMOHK", LOG_ERR, "error : epoll_ctl EPOLL_CTL_ADD");
                        exit(1);
                    }
                    APP_LOG("WEMOHK", LOG_INFO, "connect : fd = [%d], no = [%d]", event.data.fd, connection_no);
                    connection_no++;
                }
            }
            else {
                // A peer socket is ready.
                if (events[i].events & EPOLLIN) {
                    // Ready for reading.
                    int fd = events[i].data.fd;

                    APP_LOG("WEMOHK", LOG_DEBUG, "call on_peer_ready_recv(%d)", fd);

                    fd_status_t status = on_peer_ready_recv(fd);

                    struct epoll_event event = {0};
                    event.data.fd = fd;

                    if (status.want_read) {
                        event.events |= EPOLLIN;
                    }
                    if (status.want_write) {
                        event.events |= EPOLLOUT;
                    }
                    if (event.events == 0) {
                        APP_LOG("WEMOHK", LOG_INFO, "socket = [%d] closing", fd);
                        if (epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, NULL) < 0) {
                            APP_LOG("WEMOHK", LOG_ERR, "EPOLLIN : failed to call epoll_ctl EPOLL_CTL_DEL");
                        }
                        on_peer_closed(fd);
                    }
                    else if (epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event) < 0) {
                        APP_LOG("WEMOHK", LOG_ERR, "EPOLLIN : failed to call epoll_ctl EPOLL_CTL_MOD");
                    }
                } else if (events[i].events & EPOLLOUT) {
                    // Ready for writing.
                    int fd = events[i].data.fd;

                    APP_LOG("WEMOHK", LOG_DEBUG, "call on_peer_ready_send(%d)", fd);

                    fd_status_t status = on_peer_ready_send(fd);

                    struct epoll_event event = {0};
                    event.data.fd = fd;

                    if (status.want_read) {
                        event.events |= EPOLLIN;
                    }
                    if (status.want_write) {
                        event.events |= EPOLLOUT;
                    }
                    if (event.events == 0) {
                        APP_LOG("WEMOHK", LOG_INFO, "socket = [%d] closing", fd);
                        if (epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, NULL) < 0) {
                            APP_LOG("WEMOHK", LOG_ERR, "EPOLLOUT : failed to call epoll_ctl EPOLL_CTL_DEL");
                        }
                        on_peer_closed(fd);
                    }
                    else if (epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event) < 0) {
                        APP_LOG("WEMOHK", LOG_ERR, "EPOLLOUT : failed to call epoll_ctl EPOLL_CTL_MOD");
                    }
                }
            }
        }

        if (discover == 1) {
            APP_LOG("WEMOHK", LOG_INFO, "Received SIGUSR1. Run we_discover()");
            wemoCtrlPointRefresh();
            discover = 0;
        }
    }
    close(listener_sock);
    unlink(SOCKET_NAME);

    return NULL;
}

static int wemo_ipc_send(struct we_ipc_hdr *ipchdr, char *ipc_data)
{
    int i, rc = 0;
    struct timespec ts;
    int s;

    if (clock_gettime(CLOCK_REALTIME, &ts) == -1) {
        /* handle error */
        return -1;
    }

    ts.tv_sec += 3;

    for (i = 0; i < MAX_CLIENTS; i++) {
        if (global_state[i].state == CONNECTED) {
            if (global_state[i].sockfd != -1) {
                APP_LOG("WEMOHK", LOG_CRIT,  "Try wait for semaphore[%d]...", i);
                while ((s = sem_timedwait(&global_state[i].sem, &ts)) == -1 && errno == EINTR)
                    continue;
                /* Check what happened */
                if (s == -1) {
                    if (errno == ETIMEDOUT) {
                        APP_LOG("WEMOHK", LOG_CRIT,  "Wait timeout, move on...");
                        sem_post(&global_state[i].sem);
                    }
                    else {
                        APP_LOG("WEMOHK", LOG_CRIT,  "sem_timedwait failed...");
                    }
                } else {
                    APP_LOG("WEMOHK", LOG_CRIT,  "Move on...");
                }
                global_state[i].sendlen = sizeof(struct we_ipc_hdr) + ipchdr->size;
                memcpy(global_state[i].sendbuf, (char* )ipchdr, sizeof(struct we_ipc_hdr));
                memcpy(global_state[i].sendbuf + sizeof(struct we_ipc_hdr), (char *)ipc_data, ipchdr->size);

                struct epoll_event event = {0};
                event.data.fd = global_state[i].sockfd;
                event.events |= EPOLLOUT;
                if (epoll_ctl(epollfd, EPOLL_CTL_MOD, global_state[i].sockfd, &event) < 0) {
                    APP_LOG("WEMOHK", LOG_ERR,  "ipc_send_event EPOLLOUT : failed to call epoll_ctl EPOLL_CTL_MOD");
                    // If epoll_ctl is failed to modify the event attribute to send data, call send() function directly.
                    rc = send(global_state[i].sockfd, global_state[i].sendbuf, global_state[i].sendlen, 0);
                    if (rc != global_state[i].sendlen) {
                        APP_LOG("WEMOHK", LOG_ERR, "error sending event");
                    }
                    if (global_state[i].sendlen > 0) {
                        rc = send(global_state[i].sockfd, global_state[i].sendbuf, global_state[i].sendlen, 0);
                        if (rc != global_state[i].sendlen) {
                            APP_LOG("WEMOHK", LOG_ERR, "error sending event");
                        }
                    }
                }
            }
        }
    }
    return rc;
}

void wemo_ipc_send_event(int wemo_id, struct we_state *state_buffer)
{
    struct we_ipc_hdr ipchdr;

    ipchdr.wemo_id = wemo_id;
    ipchdr.cmd = EVENT_STATE;
    ipchdr.size = sizeof(struct we_state);

    wemo_ipc_send(&ipchdr, (char *) state_buffer);
}

void wemo_ipc_send_netstate(int wemo_id, struct we_network_status *net_state)
{
    struct we_ipc_hdr ipchdr;

    ipchdr.wemo_id = wemo_id;
    ipchdr.cmd = EVENT_CONNECTION_STATE;
    ipchdr.size = sizeof(struct we_network_status);

    wemo_ipc_send(&ipchdr, (char *) net_state);
}

void wemo_ipc_send_devinfo(int wemo_id, char *data)
{
    struct we_ipc_hdr ipchdr;

    ipchdr.wemo_id = wemo_id;
    ipchdr.cmd = EVENT_DEVICE_INFO;
    ipchdr.size = strlen(data) + 1;

    wemo_ipc_send(&ipchdr, (char *) data);
}

void wemo_ipc_send_name_change(int wemo_id, struct we_name_change *name_change)
{
    struct we_ipc_hdr ipchdr;

    ipchdr.wemo_id = wemo_id;
    ipchdr.cmd = EVENT_NAME_CHANGE;
    ipchdr.size = sizeof(struct we_name_change);

    wemo_ipc_send(&ipchdr, (char *) name_change);
}

void wemo_ipc_send_name_value(int wemo_id, struct we_name_value *name_value)
{
    struct we_ipc_hdr ipchdr;

    ipchdr.wemo_id = wemo_id;
    ipchdr.cmd = EVENT_NAME_VALUE;
    ipchdr.size = sizeof(struct we_name_value);

    wemo_ipc_send(&ipchdr, (char *) name_value);
}

void wemo_ipc_send_insight_home_settings(int wemo_id, struct we_insight_home_settings *settings)
{
    struct we_ipc_hdr ipchdr;

    ipchdr.wemo_id = wemo_id;
    ipchdr.cmd = EVENT_INSIGHT_HOME_SETTINGS;
    ipchdr.size = sizeof(struct we_insight_home_settings);

    wemo_ipc_send(&ipchdr, (char *) settings);
}

void wemo_ipc_server_init()
{
    ithread_t wemo_ipc_thread;
    run_server = 1;

    ithread_create(&wemo_ipc_thread, NULL, wemo_ipc_server, NULL);
    ithread_detach(wemo_ipc_thread);
}

void wemo_ipc_server_finish()
{
    run_server = 0;
}
