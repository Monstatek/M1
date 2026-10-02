#!/usr/bin/env python3
"""Execute production API functions with fault-injected host dependencies.

Run from any directory: python3 m1_csrc/test/m1_pass4_api_test.py
No hardware acceptance is implied by mocked filesystem/UART operations.
"""
import subprocess
import sys
import tempfile
from pathlib import Path
sys.dont_write_bytecode = True
from m1_pass3_helpers_test import ROOT, source, function, build_run, SAVE_STUBS


def functions(path, names):
    text = source(path)
    return '\n'.join(function(text, name) for name in names) + '\n'


FATFS = r'''
typedef unsigned UINT;
typedef int FRESULT;
typedef struct { int value; } FIL;
typedef FIL DIR;
typedef struct { unsigned fattrib; } FILINFO;
enum { FR_OK, FR_DISK_ERR, FR_INT_ERR, FR_NOT_READY, FR_NO_FILE,
       FR_NO_PATH, FR_INVALID_NAME, FR_DENIED, FR_EXIST, FR_INVALID_OBJECT,
       FR_WRITE_PROTECTED, FR_INVALID_DRIVE, FR_NOT_ENABLED, FR_NO_FILESYSTEM,
       FR_MKFS_ABORTED, FR_TIMEOUT, FR_LOCKED, FR_NOT_ENOUGH_CORE,
       FR_TOO_MANY_OPEN_FILES, FR_INVALID_PARAMETER };
#define AM_DIR 16
#define FA_CREATE_ALWAYS 8
#define FA_WRITE 2
#define FA_READ 1
#define FA_OPEN_EXISTING 0
typedef enum { FS_PATH_NONE, FS_PATH_FILE, FS_PATH_DIRECTORY } fs_path_kind_t;
static int result, calls; static unsigned attributes, transferred;
static unsigned s_fb_perf_file_open_count;
static FRESULT f_stat(const char *p, FILINFO *f) { assert(p); calls++; f->fattrib=attributes; return result; }
static FRESULT f_open(FIL *f,const char *p,unsigned mode) { assert(f&&p); (void)mode; calls++; return result; }
static FRESULT f_opendir(DIR *f,const char *p) { assert(f&&p); calls++; return result; }
static FRESULT f_read(FIL *f,void *p,UINT size,UINT *n) { assert(f&&n); assert(transferred<=size); if(transferred) memset(p,'r',transferred); *n=transferred; calls++; return result; }
static FRESULT f_write(FIL *f,const void *p,UINT size,UINT *n) { assert(f&&n); (void)p; assert(transferred<=size); *n=transferred; calls++; return result; }
'''


def filesystem(tmp):
    code = FATFS
    code += functions('m1_csrc/m1_file_util.c', ['fs_path_kind', 'fs_file_exists', 'fs_directory_exists'])
    code += functions('m1_csrc/m1_file_browser.c', ['m1_fb_open_new_file', 'm1_fb_open_file', 'm1_fb_open_dir',
                        'm1_fb_read_file', 'm1_fb_read_from_file', 'm1_fb_write_file', 'm1_fb_write_to_file'])
    code += r'''
int main(void) {
    FIL f; DIR d; char buf[32]; UINT n; fs_path_kind_t kind;
    for(result=0;result<=FR_INVALID_PARAMETER;result++) {
        for(unsigned dir=0;dir<2;dir++) {
            attributes=dir?AM_DIR:0; kind=FS_PATH_DIRECTORY;
            assert(fs_path_kind("file",&kind)==result);
            assert(kind==(result?FS_PATH_NONE:(dir?FS_PATH_DIRECTORY:FS_PATH_FILE)));
            assert(fs_file_exists("file")== (result?0:(dir?-1:1)));
            assert(fs_directory_exists("file")== (result?0:(dir?1:-1)));
        }
        assert(m1_fb_open_file(&f,"x")==result);
        assert(m1_fb_open_new_file(&f,"x")==result);
        assert(m1_fb_open_dir(&d,"x")==result);
        for(transferred=0;transferred<=sizeof(buf);transferred++) {
            n=999; assert(m1_fb_read_file(&f,buf,sizeof(buf),&n)==result && n==transferred);
            assert(m1_fb_read_from_file(&f,buf,sizeof(buf))==(result?0:transferred));
            n=999; assert(m1_fb_write_file(&f,buf,sizeof(buf),&n)==result && n==transferred);
            assert(m1_fb_write_to_file(&f,buf,sizeof(buf))==(result?0:transferred));
        }
    }
    int before=calls;
    assert(fs_path_kind(NULL,&kind)==FR_INVALID_PARAMETER && kind==FS_PATH_NONE);
    assert(fs_path_kind("x",NULL)==FR_INVALID_PARAMETER);
    assert(m1_fb_open_file(NULL,"x")==FR_INVALID_OBJECT);
    assert(m1_fb_open_new_file(NULL,"x")==FR_INVALID_OBJECT);
    assert(m1_fb_open_dir(NULL,"x")==FR_INVALID_OBJECT);
    assert(m1_fb_open_file(&f,NULL)==FR_INVALID_PARAMETER);
    assert(m1_fb_read_file(&f,NULL,1,&n)==FR_INVALID_PARAMETER && n==0);
    assert(m1_fb_write_file(&f,NULL,1,&n)==FR_INVALID_PARAMETER && n==0);
    assert(m1_fb_read_file(NULL,buf,1,&n)==FR_INVALID_OBJECT && n==0);
    assert(m1_fb_write_file(NULL,buf,1,&n)==FR_INVALID_OBJECT && n==0);
    assert(m1_fb_read_file(&f,buf,1,NULL)==FR_INVALID_PARAMETER);
    assert(m1_fb_write_file(&f,buf,1,NULL)==FR_INVALID_PARAMETER);
    assert(calls==before);
    puts("filesystem: all FatFs errors, EOF, partial transfers and invalid arguments PASS");
}
'''
    build_run(tmp, 'filesystem', code)


def save_bounds(tmp):
    code = SAVE_STUBS.replace('return *kind == FS_PATH_NONE ? FR_NO_FILE : FR_OK;',
                            'return scenario==8 ? 1 : (*kind == FS_PATH_NONE ? FR_NO_FILE : FR_OK);')
    code = code.replace('strcpy(out,scenario==6?',
                        "if(scenario==9) { memset(out,'x',50); return 1; }\n    strcpy(out,scenario==6?")
    # run_save is used below to exercise the same workflow stub as Pass 3.
    code += functions('m1_csrc/m1_save_filename.c', ['m1_save_filename'])
    code += r'''
static uint8_t normal(char *p,size_t n) { return m1_save_filename(p,n,"0:/NFC","0:/NFC/","nfc_",".nfc"); }
static void reset(void) { scenario=attempts=messages=step=got_seed=0; trace[0]=0; }
int main(void) {
    char log[2048], out[80];
    assert(run_save(normal,0,0,out,log)==0 && strcmp(out,"0:/NFC/saved_name.nfc")==0);
    const size_t need=strlen(out)+1;
    for(size_t capacity=0;capacity<=need+1;capacity++) {
        reset(); memset(out,0x5A,sizeof(out));
        uint8_t r=normal(out,capacity);
        assert(r==(capacity<need?2:0));
        if(r) { for(size_t i=0;i<sizeof(out);i++) assert(out[i]==0x5A); }
        else assert((unsigned char)out[need]==0x5A);
    }
    char long_part[80]; memset(long_part,'x',sizeof(long_part)-1); long_part[79]=0;
    reset(); strcpy(out,"untouched");
    assert(m1_save_filename(out,sizeof(out),"dir","p/",long_part,".nfc")==2);
    assert(strcmp(out,"untouched")==0);
    reset(); assert(m1_save_filename(out,sizeof(out),"dir",long_part,"n_",".nfc")==2);
    reset(); assert(m1_save_filename(out,sizeof(out),"dir","p/","n_",long_part)==2);
    reset(); assert(m1_save_filename(out,sizeof(out),NULL,"p/","n_",".nfc")==2 && step==0);
    reset(); scenario=8; assert(normal(out,sizeof(out))==2 && strcmp(out,"untouched")==0);
    reset(); scenario=9; assert(normal(out,sizeof(out))==2 && strcmp(out,"untouched")==0);
    reset(); assert(normal(NULL,0)==0);
    puts("save names: exact-fit/short capacity, overlong inputs, I/O failure, NULL output PASS");
}
'''
    build_run(tmp, 'save_bounds', code)


def selection(tmp):
    code = r'''
enum { FB_OK, FB_ERR_SDCARD, FB_ERR_GUI };
typedef struct { char *dir_name,*file_name; bool file_is_selected; int status; } S_M1_file_info;
typedef struct { S_M1_file_info info; } Browser;
static Browser *pfb_hdl;
'''
    code += functions('m1_csrc/m1_file_browser.c', ['m1_fb_copy_selection'])
    code += r'''
int main(void) {
    char d[32],f[32]; Browser b={{NULL,NULL,true,FB_OK}};
    memset(d,'D',sizeof(d)); memset(f,'F',sizeof(f));
    assert(!m1_fb_copy_selection(d,sizeof(d),f,sizeof(f)));
    pfb_hdl=&b;
    assert(!m1_fb_copy_selection(d,sizeof(d),f,sizeof(f)));
    b.info.dir_name=malloc(7); b.info.file_name=malloc(6);
    strcpy(b.info.dir_name,"0:/NFC"); strcpy(b.info.file_name,"a.nfc");
    for(size_t nd=0;nd<9;nd++) for(size_t nf=0;nf<8;nf++) {
        memset(d,'D',sizeof(d)); memset(f,'F',sizeof(f));
        bool ok=m1_fb_copy_selection(d,nd,f,nf);
        assert(ok==(nd>=7&&nf>=6));
        if(!ok) { for(unsigned i=0;i<32;i++) assert(d[i]=='D'&&f[i]=='F'); }
        else { assert(!strcmp(d,"0:/NFC")&&!strcmp(f,"a.nfc")); assert(d[7]=='D'&&f[6]=='F'); }
    }
    b.info.status=FB_ERR_SDCARD; assert(!m1_fb_copy_selection(d,32,f,32));
    b.info.status=FB_OK; b.info.file_is_selected=false; assert(!m1_fb_copy_selection(d,32,f,32));
    b.info.file_is_selected=true; assert(m1_fb_copy_selection(d,32,f,32));
    free(b.info.dir_name); free(b.info.file_name); pfb_hdl=NULL;
    assert(!strcmp(d,"0:/NFC")&&!strcmp(f,"a.nfc"));
    puts("browser snapshot: bounds, no partial publication, error/empty state, lifetime PASS");
}
'''
    build_run(tmp, 'selection', code)


def sd_lifecycle(tmp):
    code = r'''
typedef int FRESULT;
enum { FR_OK, FR_DISK_ERR, FR_INT_ERR, FR_NO_FILESYSTEM, FR_NOT_READY };
typedef enum { SD_access_OK, SD_access_NotOK, SD_access_NotReady,
 SD_access_UnMounted, SD_access_NoFS, SD_access_EndOfStatus } S_M1_SDCard_Access_Status;
static struct { int sdfs; char sdpath[4]; S_M1_SDCard_Access_Status status; unsigned timestamp; } sdcard_ctl;
static FRESULT sd_fres, mount_result, free_result;
static int sd_free_clusters,sd_pfatfs,unmount_calls,free_calls,provision_calls;
static bool usbmsc_sd_enable;
#define M1_LOG_I(...) ((void)0)
static unsigned HAL_GetTick(void) { return 123; }
static bool m1_sdcard_provision_canonical(void) { provision_calls++; return true; }
static FRESULT f_mount(void *fs,const char *p,int now) {
    (void)p; assert((fs!=NULL)==(now!=0));
    if(!fs) unmount_calls++; return mount_result;
}
static FRESULT f_getfree(const char *p,int *c,int *f) {
    (void)p; assert(c==&sd_free_clusters&&f==&sd_pfatfs); free_calls++; return free_result;
}
'''
    code += functions('m1_csrc/m1_sdcard.c', ['m1_sdcard_mount', 'm1_sdcard_unmount',
                                           'm1_sdcard_set_status', 'm1_sdcard_invalidate'])
    code += r'''
int main(void) {
    for(mount_result=0;mount_result<=FR_NOT_READY;mount_result++)
      for(free_result=0;free_result<=FR_NOT_READY;free_result++) {
        sdcard_ctl.status=SD_access_OK; free_calls=unmount_calls=provision_calls=0;
        usbmsc_sd_enable=false;
        bool queried=mount_result==FR_OK||mount_result==FR_NO_FILESYSTEM;
        FRESULT r=m1_sdcard_mount();
        assert(r==(mount_result?mount_result:free_result));
        assert(free_calls==queried && unmount_calls==!queried);
        assert(provision_calls==(queried&&free_result==FR_OK));
        int expected=queried ? (free_result==FR_OK?SD_access_OK:
                     free_result==FR_NO_FILESYSTEM?SD_access_NoFS:SD_access_NotOK) :
                     (mount_result==FR_DISK_ERR?SD_access_NotOK:SD_access_UnMounted);
        assert((int)sdcard_ctl.status==expected && sdcard_ctl.timestamp==123);
        assert(m1_sdcard_unmount()==mount_result);
        assert(sdcard_ctl.status==(mount_result?SD_access_NotOK:SD_access_UnMounted));
        m1_sdcard_invalidate(); assert(sdcard_ctl.status==SD_access_NotReady);
    }
    mount_result=free_result=FR_OK; provision_calls=0; usbmsc_sd_enable=true;
    assert(m1_sdcard_mount()==FR_OK && provision_calls==0);
    puts("SD lifecycle: direct results, cached-state transitions, unmount failures PASS");
}
'''
    build_run(tmp, 'sd_lifecycle', code)


def uart(tmp):
    current = source('m1_csrc/m1_esp_uart_transport.c')
    baseline = subprocess.check_output(['git','show','a41fdeaaa807fca12f96607b95125e8bcb3a7a1f:m1_csrc/m1_wifi.c'],cwd=ROOT,text=True)
    names = ['esp32_uart_write', 'esp32_uart_read', 'esp32_uart_read_until_prompt']
    for name in names:
        assert function(current,name)==function(baseline,name), name+' implementation changed'
    code = r'''
typedef unsigned TickType_t;
typedef int HAL_StatusTypeDef;
#define TRUE 1
#define FALSE 0
#define HAL_OK 0
#define pdMS_TO_TICKS(x) (x)
static int huart_esp,esp32_rb_hdl,tx_result;
static unsigned tick,pos; static const char *input;
static unsigned xTaskGetTickCount(void) { return tick; }
static void vTaskDelay(unsigned n) { tick+=n; }
static int HAL_UART_Transmit(int *h,uint8_t *p,unsigned n,unsigned t) {
    assert(h==&huart_esp&&p&&n==1&&t==123); return tx_result;
}
static unsigned m1_ringbuffer_read(int *r,uint8_t *p,unsigned size) {
    assert(r==&esp32_rb_hdl); unsigned n=0;
    while(n<size&&input[pos]) p[n++]=input[pos++]; return n;
}
'''
    code += '\n'.join(function(current,name) for name in names)
    code += r'''
int main(void) {
    uint8_t v=1; char buf[32];
    for(tx_result=0;tx_result<4;tx_result++) assert(esp32_uart_write(&v,1,123)==(tx_result==0));
    input="abc>> tail"; pos=tick=0;
    assert(esp32_uart_read_until_prompt(buf,32,10,">> ")==6 && !strcmp(buf,"abc>> "));
    input="partial"; pos=tick=0;
    assert(esp32_uart_read_until_prompt(buf,32,10,">> ")==7 && tick==10);
    input="too long"; pos=tick=0; memset(buf,'X',32);
    assert(esp32_uart_read_until_prompt(buf,4,10,">> ")==3 && buf[3]==0 && buf[4]=='X');
    assert(esp32_uart_read_until_prompt(NULL,0,10,">> ")==0);
    input="abc";pos=tick=0; assert(esp32_uart_read((uint8_t*)buf,3,10)==0);
    input="ab";pos=tick=0; assert(esp32_uart_read((uint8_t*)buf,3,10)==1 && tick==10);
    puts("UART: unchanged bodies, legacy returns, prompt, timeout and bounds PASS");
}
'''
    build_run(tmp, 'uart', code)


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='m1-pass4-host-') as directory:
        tmp = Path(directory)
        for test in (filesystem, save_bounds, selection, sd_lifecycle, uart):
            test(tmp)
    print('Pass 4 APIs: all five groups PASS under ASan/UBSan.')
