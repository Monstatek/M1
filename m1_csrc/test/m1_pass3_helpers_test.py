#!/usr/bin/env python3
"""Run actual shared helpers/wrappers with host stubs under ASan/UBSan.

From the repository root: python3 m1_csrc/test/m1_pass3_helpers_test.py
Optional --baseline <commit> compares behavior with the pre-consolidation
functions read from Git. Only temporary test files are written. Hardware
register accesses are mocked; this does not grant hardware acceptance.
"""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
COMMON = ROOT / "NFC/NFC_drv/common"


def source(path, ref=None):
    if ref:
        return subprocess.check_output(["git", "show", f"{ref}:{path}"],
                                       cwd=ROOT, text=True)
    return (ROOT / path).read_text()


def function(text, name):
    match = re.search(r"^[^\n;]*\b" + name + r"\([^;{}]*\)\s*\n\{", text, re.M)
    assert match, name
    start = match.start()
    depth = 1
    body = text[match.end():]
    for token in re.finditer(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|'
                             r'//[^\n]*|/\*[\s\S]*?\*/|[{}]', body):
        if token.group() == "{":
            depth += 1
        elif token.group() == "}":
            depth -= 1
        if depth == 0:
            return text[start:match.end() + token.end()]
    raise AssertionError(name)


PREAMBLE = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
'''


def build_run(tmp, name, text, extra_sources=()):
    path = tmp / (name + ".c")
    path.write_text(PREAMBLE + text)
    exe = tmp / name
    # macOS deprecates sprintf even in the untouched historical functions.
    # Keep that warning visible without blocking the differential test.
    subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-O1", "-Wall",
                    "-Wextra", "-Werror", "-Wno-error=deprecated-declarations",
                    "-fsanitize=address,undefined",
                    "-I", str(COMMON), str(path), *map(str, extra_sources),
                    "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)


def parser_tests(tmp, baseline):
    specs = [("mfc_key_source.c", "mfc_key_parse_line", "mfc_key_line_kind_t", "MFC_KEY", 6),
             ("ntag_pwd_keys.c", "ntag_pwd_keys_parse_line", "ntag_pwd_line_kind_t", "NTAG_PWD", 4),
             ("ulc_keys.c", "ulc_keys_parse_line", "ulc_line_kind_t", "ULC", 16)]
    for file, name, typ, prefix, size in specs:
        path = "NFC/NFC_drv/common/" + file
        code = '#include "nfc_dict_line.h"\n'
        code += f"#define {prefix}_KEY_SIZE {size}\n#define {prefix}_KEY_HEXLEN {size * 2}\n" if prefix != "MFC_KEY" else f"#define MFC_KEY_SIZE {size}\n#define MFC_KEY_HEXLEN {size * 2}\n"
        code += f"typedef enum {{ {prefix}_LINE_SKIP, {prefix}_LINE_KEY, {prefix}_LINE_BAD }} {typ};\n"
        code += function(source(path), name) + "\n"
        if baseline:
            old = source(path, baseline)
            code += function(old, "hexval") + "\n" + function(old, "is_ws") + "\n"
            code += function(old, name).replace(name, "old_parse") + "\n"
        code += f"#define PARSE {name}\n#define KEY_SIZE {size}\n"
        code += r'''
static void check(const char *line, int expected) {
    uint8_t out[18], before[18]; memset(out, 0xA5, sizeof(out));
    memcpy(before, out, sizeof(out));
    assert((int)PARSE(line, out) == expected);
    assert((int)PARSE(line, NULL) == expected);
    assert(out[KEY_SIZE] == 0xA5 && out[17] == 0xA5);
    if (expected != 1) assert(memcmp(out, before, sizeof(out)) == 0);
}
int main(void) {
    char valid[80]; memset(valid, 'a', KEY_SIZE * 2); valid[KEY_SIZE * 2] = 0;
    check(NULL, 0); check("", 0); check(" \t\r\n", 0); check(" #comment", 0);
    check(valid, 1);
    uint8_t decoded[KEY_SIZE]; assert(PARSE(valid, decoded) == 1);
    for (unsigned i=0; i<KEY_SIZE; i++) assert(decoded[i] == 0xAA);
    for (unsigned n=0; n<KEY_SIZE*2; n++) {
        char *short_line=malloc(n+1); assert(short_line);
        memset(short_line, 'A', n); short_line[n]=0; check(short_line, n ? 2 : 0);
        free(short_line);
    }
    char text[96]; snprintf(text, sizeof(text), " \t%s \t# ok\r\n", valid); check(text, 1);
    snprintf(text, sizeof(text), "%s#bad", valid); check(text, 2);
    snprintf(text, sizeof(text), "%sF", valid); check(text, 2);
    snprintf(text, sizeof(text), "%s garbage", valid); check(text, 2);
    for (unsigned pos=0; pos<KEY_SIZE*2; pos++) {
        for (unsigned ch=1; ch<256; ch++) {
            strcpy(text, valid); text[pos]=(char)ch;
            int expected=((ch>='0'&&ch<='9')||(ch>='A'&&ch<='F')||
                          (ch>='a'&&ch<='f')) ? 1 : 2;
            /* At the start '#' denotes a full comment, not a bad key. */
            if (pos==0 && ch=='#') expected=0;
            check(text, expected);
        }
    }
'''
        if baseline:
            code += r'''
    uint32_t rng=7;
    for (unsigned trial=0; trial<30000; trial++) {
        char buf[96]; unsigned len=trial%sizeof(buf);
        for (unsigned i=0;i<len;i++) { rng=rng*1664525U+1013904223U; buf[i]=(char)(rng>>24); }
        buf[len]=0;
        if ((trial%3)==0 && len>=KEY_SIZE*2) memcpy(buf, valid, KEY_SIZE*2);
        uint8_t a[18], b[18]; memset(a,0xA5,sizeof(a)); memset(b,0xA5,sizeof(b));
        assert(PARSE(buf,a)==old_parse(buf,b)); assert(memcmp(a,b,sizeof(a))==0);
        assert(PARSE(buf,NULL)==old_parse(buf,NULL));
    }
'''
        code += f'puts("{name}: PASS"); return 0; }}\n'
        build_run(tmp, name, code, [COMMON / "nfc_dict_line.c"])


SAVE_STUBS = r'''
#define FR_OK 0
#define FR_NO_FILE 4
#define FR_NO_PATH 5
typedef int FRESULT;
typedef enum { FS_PATH_NONE, FS_PATH_FILE, FS_PATH_DIRECTORY } fs_path_kind_t;
#define IDS_ENTER_FILENAME "Enter filename"
#define IDS_DUPLICATE_FILE "Duplicate"
#define IDS_BACK "Back"
#define res_string(x) (x)
#define CONCAT_FILEPATH_FILENAME(a,b) a b
#define DRIVE0_NFC "0:/NFC"
#define NFC_FILE_PREFIX "nfc_"
#define NFC_FILE_EXTENSION ".nfc"
#define DRIVE0_RFID "0:/RFID"
#define RFID_FILEPATH "/RFID"
#define RFID_FILE_PREFIX "rfid_"
#define RFID_FILE_EXTENSION ".rfid"
#define DRIVE0_WIFI "0:/wifi"
#define WIFI_FILE_PREFIX "handshake_"
#define WIFI_FILE_EXTENSION ".txt"
static int m1_u8g2, scenario, attempts, messages, step, got_seed;
static char trace[2048];
static void event(const char *tag,const char *arg) {
    size_t n=strlen(trace); snprintf(trace+n,sizeof(trace)-n,"%s:%s;",tag,arg?arg:"");
}
static void m1_sdcard_get_info(void) { assert(step++==0); event("info",NULL); }
static uint32_t m1_sdcard_get_free_capacity(void) { assert(step++==1); event("space",NULL); return scenario==1?3:4; }
static int fs_directory_ensure(const char *dir) { assert(step++==2); event("dir",dir); return scenario==2?1:0; }
static unsigned HAL_GetTick(void) { event("tick",NULL); return 1234; }
static void fake_srand(unsigned seed) { assert(seed==1234); got_seed=1; event("seed",NULL); }
static int fake_rand(void) { assert(got_seed); event("rand",NULL); return attempts?0xABCDE:7; }
#define srand fake_srand
#define rand fake_rand
static uint8_t m1_vkb_get_filename(char *title,char *def,char *out,uint8_t generated) {
    assert(strcmp(title,IDS_ENTER_FILENAME)==0 && generated==1);
    event("default",def); attempts++;
    if(scenario==3 || (scenario==5&&attempts==2)) return 0;
    strcpy(out,scenario==6?"abcdefghijklmnopqrst":"saved_name"); return 1;
}
static FRESULT fs_path_kind(const char *path, fs_path_kind_t *kind) {
    event("exists",path);
    *kind = ((scenario==4||scenario==5||scenario==7)&&attempts==1) ?
        (scenario==7?FS_PATH_DIRECTORY:FS_PATH_FILE) : FS_PATH_NONE;
    return *kind == FS_PATH_NONE ? FR_NO_FILE : FR_OK;
}
static uint8_t m1_message_box(int *display,const char *a,const char *b,const char *c,const char *d) {
    assert(display==&m1_u8g2 && strcmp(a,IDS_DUPLICATE_FILE)==0 && !b);
    assert(strcmp(c," ")==0 && strcmp(d,IDS_BACK)==0); messages++; event("message",NULL); return 0;
}
typedef uint8_t (*save_fn)(char *, size_t);
static unsigned run_save(save_fn fn,unsigned mode,int null_out,char *out,char *log) {
    scenario=(int)mode; attempts=messages=step=got_seed=0; trace[0]=0;
    strcpy(out,"unchanged"); unsigned ret=fn(null_out?NULL:out,80);
    strcpy(log,trace); return ret;
}
'''


def save_tests(tmp, baseline):
    code = SAVE_STUBS + function(source("m1_csrc/m1_save_filename.c"), "m1_save_filename")
    specs = [("NFC/NFC_drv/common/nfc_file.c", "nfc_save_file_keyboard", "0:/NFC/saved_name.nfc"),
             ("lfrfid/lfrfid_file.c", "lfrfid_save_file_keyboard", "0:/RFID/saved_name.rfid"),
             ("m1_csrc/m1_wifi.c", "wifi_save_file_keyboard", "0:/wifi/saved_name.txt")]
    for path, name, _ in specs:
        code += "\n" + function(source(path), name)
        if baseline:
            code += "\n" + function(source(path, baseline), name).replace(name, "old_" + name)
            code += f'\nstatic uint8_t compat_{name}(char *p,size_t n) {{ (void)n; return old_{name}(p); }}\n'
    if baseline:
        # The old API could not distinguish an existing directory from absence.
        code = code.replace("typedef uint8_t (*save_fn)", '''static int fs_file_exists(const char *path) {
            fs_path_kind_t kind; FRESULT r=fs_path_kind(path,&kind);
            return r==FR_OK ? (kind==FS_PATH_FILE?1:-1) : 0;
        }
typedef uint8_t (*save_fn)''')
    code += '\nint main(void) {\n'
    for _, name, path in specs:
        code += f'''for(unsigned mode=0;mode<8;mode++) for(int nul=0;nul<2;nul++) {{
            char out[80], log[2048]; unsigned ret=run_save({name},mode,nul,out,log);
            unsigned expected=mode==1?1:mode==2?2:(mode==3||mode==5)?3:0;
            assert(ret==expected);
            if(ret||nul) assert(strcmp(out,"unchanged")==0);
            else if(mode!=6) assert(strcmp(out,"{path}")==0);
            if(mode==4||mode==5||mode==7) assert(messages==1 && attempts==2);
'''
        if baseline:
            code += f'''if(mode!=7) {{ char before[80], old_log[2048];
                assert(ret==run_save(compat_{name},mode,nul,before,old_log));
                assert(strcmp(out,before)==0 && strcmp(log,old_log)==0);
                }}
'''
        code += '}\n'
    code += 'puts("save-name workflows: PASS (48 cases)"); return 0; }\n'
    build_run(tmp, "save_names", code)


CAP_STUBS = r'''
typedef struct { uint32_t guard,uart_ore,dma_errors,tail; } m1_capture_soak_result_t;
typedef struct { uint32_t guard,unused,uart_ore,dma_errors,tail; } m1_capture_stream_result_t;
static int huart_esp,s_cap_dma; static unsigned flags; static char trace[32]; static size_t at;
#define UART_FLAG_ORE 1U
#define DMA_FLAG_DTE 2U
#define DMA_FLAG_ULE 4U
#define DMA_FLAG_USE 8U
static int read_flag(void *p,unsigned mask) { assert(p==(mask==1?&huart_esp:&s_cap_dma)); trace[at++]=(char)('a'+mask); return (flags&mask)!=0; }
static void clear_flag(void *p,unsigned mask) { assert(p==(mask==1?&huart_esp:&s_cap_dma)); trace[at++]=(char)('A'+mask); flags&=~mask; }
#define __HAL_UART_GET_FLAG(p,m) read_flag(p,m)
#define __HAL_UART_CLEAR_OREFLAG(p) clear_flag(p,1)
#define __HAL_DMA_GET_FLAG(p,m) read_flag(p,m)
#define __HAL_DMA_CLEAR_FLAG(p,m) clear_flag(p,m)
'''


def capture_tests(tmp, baseline):
    path = "m1_csrc/m1_capture_link.c"
    code = CAP_STUBS + function(source(path), "cap_poll_error_counts")
    for name in ("cap_poll_errors", "cap_poll_errors_stream"):
        code += "\n" + function(source(path), name)
        if baseline:
            code += "\n" + function(source(path, baseline), name).replace(name, "old_" + name)
    code += '\nint main(void) {\n'
    for typ, name in [("m1_capture_soak_result_t", "cap_poll_errors"),
                      ("m1_capture_stream_result_t", "cap_poll_errors_stream")]:
        code += f'''for(unsigned mask=0;mask<16;mask++) for(unsigned rollover=0;rollover<2;rollover++) {{
            {typ} x; memset(&x,0xA5,sizeof(x)); x.uart_ore=x.dma_errors=rollover?UINT32_MAX:7;
            uint32_t start=x.uart_ore;
'''
        if baseline:
            code += f'{typ} old=x;\n'
        code += f'''flags=mask; at=0; memset(trace,0,sizeof(trace)); {name}(&x);
            assert(x.uart_ore==start+((mask&1)!=0)); assert(x.dma_errors==start+((mask&14)!=0));
            assert(x.guard==0xA5A5A5A5 && x.tail==0xA5A5A5A5); assert(flags==0);
'''
        if baseline:
            code += f'''char now[32]; memcpy(now,trace,sizeof(now));
                flags=mask; at=0; memset(trace,0,sizeof(trace)); old_{name}(&old);
                assert(memcmp(&x,&old,sizeof(x))==0 && memcmp(now,trace,sizeof(now))==0);
'''
        code += '}\n'
    code += 'puts("capture error polling: PASS (64 cases)"); return 0; }\n'
    build_run(tmp, "capture_errors", code)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", help="pre-consolidation Git commit for differential checks")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="m1-pass3-tests-") as directory:
        tmp = Path(directory)
        parser_tests(tmp, args.baseline)
        save_tests(tmp, args.baseline)
        capture_tests(tmp, args.baseline)
    print("All Pass 3 helper tests PASS (ASan/UBSan).")
