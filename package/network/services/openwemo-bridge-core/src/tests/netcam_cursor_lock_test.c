#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <semaphore.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../wemo_ctrl/wemo_device_db.c"
extern sqlite3 *ctrlpt_dev_db, *ctrlpt_state_db;
static sem_t busy_started;
static pthread_t writer;
static int triggered, flush_result;
static atomic_int exhausted;
static int busy_callback(void *unused,int attempt) {
    (void)unused;
    if (!attempt) sem_post(&busy_started);
    if (attempt >= 200) { atomic_fetch_add(&exhausted,1); return 0; }
    usleep(1000); return 1;
}
static void *flush_writer(void *unused) {
    (void)unused;
    pthread_mutex_lock(&state_cache_lock);
    flush_result=state_cache_flush_locked();
    pthread_mutex_unlock(&state_cache_lock);
    return NULL;
}
static int reader_trace(unsigned kind,void *unused,void *stmt,void *sql) {
    (void)kind;(void)unused;(void)stmt;
    if (!triggered && strstr((const char*)sql,"SELECT value FROM state_capability")) {
        triggered=1;
        assert(pthread_create(&writer,NULL,flush_writer,NULL)==0);
        // online_stmt already returned SQLITE_ROW, so a genuine reader lock
        // blocks the writer. Keep that writer holding the cache mutex while
        // the actual list producer reaches its sensor cache read.
        struct timespec deadline;
        assert(clock_gettime(CLOCK_REALTIME,&deadline)==0);
        deadline.tv_sec += 3;
        assert(sem_timedwait(&busy_started,&deadline)==0);
    }
    return 0;
}
static int traced_open(const char *file,sqlite3 **db,int flags,const char *vfs) {
    int rc=sqlite3_open_v2(file,db,flags,vfs);
    if (rc==SQLITE_OK && !strcmp(file,wemo_state_db))
        sqlite3_trace_v2(*db,SQLITE_TRACE_STMT,reader_trace,NULL);
    return rc;
}
#define sqlite3_open_v2 traced_open
#include "../wemo_ctrl/wemo_ipc_server.c"
#undef sqlite3_open_v2
int main(int argc,char **argv) {
    assert(argc==2);snprintf(wemo_device_db,128,"%s/cursor-device.db",argv[1]);snprintf(wemo_state_db,128,"%s/cursor-state.db",argv[1]);
    assert(wemo_dev_db_init(&ctrlpt_dev_db,&ctrlpt_state_db)==DB_SUCCESS);
    struct wemoDevice dev={0};strcpy(dev.UDN,"uuid:NetCamSensor-cursor-fixture");strcpy(dev.deviceType,"urn:Belkin:device:NetCamSensor:1");strcpy(dev.modelName,"NetCam");strcpy(dev.firmwareVersion,"unknown");
    wemo_dev_db_insert(ctrlpt_dev_db,&dev);int id=wemo_dev_db_retrieve_id(ctrlpt_dev_db,dev.UDN);assert(id>0);
    wemo_dev_statedb_insert(ctrlpt_state_db,id,1,"'0=0'");wemo_dev_db_update_motion(id,5,(int)time(NULL));
    // Exercise the supported rollback-journal fallback, where reader cursors
    // hold SHARED locks. In normal WAL mode these do not block this writer.
    assert(sqlite3_exec(ctrlpt_state_db,"PRAGMA journal_mode=DELETE",NULL,NULL,NULL)==SQLITE_OK);
    sqlite3_busy_handler(ctrlpt_state_db,busy_callback,NULL);sem_init(&busy_started,0,0);
    struct we_device_list list;
    assert(wemo_ctrl_list_devices(&list)==WE_STATUS_OK);assert(triggered);pthread_join(writer,NULL);
    assert(flush_result==DB_SUCCESS);assert(atomic_load(&exhausted)==0);
    assert(list.count==1 && list.items[0].motion_snapshot==5);
    sem_destroy(&busy_started);wemo_dev_db_finish(ctrlpt_dev_db,ctrlpt_state_db);
    puts("PASS: actual list producer releases SQLite reader cursor before cache-flusher mutex wait");
}
