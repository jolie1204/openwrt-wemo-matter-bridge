#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include "../wemo_engine/wemo_engine.c"
static int listener;
static int test_version=WE_PROTO_VERSION, test_size=sizeof(struct we_device_list);
static void *respond(void *unused) {
    (void)unused;
    int fd=accept(listener,NULL,NULL); assert(fd>=0);
    struct we_ipc_hdr req; struct we_proto_hdr proto;
    assert(we_read_full(fd,&req,sizeof req)); assert(we_read_full(fd,&proto,sizeof proto));
    struct we_device_list list={0}; list.count=1;
    list.items[0].device_type=WEMO_NETCAM_SENSOR; list.items[0].motion_snapshot=9; list.items[0].state=1; list.items[0].is_online=1;
    proto.magic=WE_PROTO_MAGIC; proto.version=test_version; proto.msg_type=WE_MSG_RESPONSE;
    proto.op=CMD_LIST_DEVICES; proto.status=WE_STATUS_OK; proto.payload_len=test_size;
    req.size=sizeof proto+test_size;
    assert(we_write_full(fd,&req,sizeof req)); assert(we_write_full(fd,&proto,sizeof proto));
    char payload[sizeof list+4]; memset(payload,0,sizeof payload); memcpy(payload,&list,sizeof list);
    assert(we_write_full(fd,payload,test_size)); close(fd); return NULL;
}
static void run(int version,int size,int expected) {
    pthread_t thread; test_version=version;test_size=size;
    assert(pthread_create(&thread,NULL,respond,NULL)==0);
    struct we_device_list list={0}; assert(we_fetch_cached_device_list_once(&list)==expected);
    if (expected==WE_STATUS_OK) { assert(list.count==1); assert(list.items[0].motion_snapshot==9); }
    pthread_join(thread,NULL);
}
int main(void) {
    listener=socket(AF_INET,SOCK_STREAM,0); assert(listener>=0);
    struct sockaddr_in address={0}; address.sin_family=AF_INET; address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    assert(bind(listener,(struct sockaddr*)&address,sizeof address)==0); assert(listen(listener,4)==0);
    socklen_t length=sizeof address; assert(getsockname(listener,(struct sockaddr*)&address,&length)==0);
    we_set_ipc_target("127.0.0.1",ntohs(address.sin_port));
    run(WE_PROTO_VERSION,sizeof(struct we_device_list),WE_STATUS_OK);
    run(2,sizeof(struct we_device_list),WE_STATUS_INVALID);
    run(WE_PROTO_VERSION,sizeof(struct we_device_list)-4,WE_STATUS_INVALID);
    run(WE_PROTO_VERSION,sizeof(struct we_device_list)+4,WE_STATUS_INVALID);
    close(listener); puts("PASS: sensor snapshot IPC, mixed version and unexpected payload sizes fail closed");
}
