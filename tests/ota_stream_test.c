#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ota_stream.h"
static uint8_t result[4096];static size_t used;static bool fail_write;
static bool writer(void *context,const uint8_t *p,size_t n){(void)context;if(fail_write)return false;assert(used+n<=sizeof(result));memcpy(result+used,p,n);used+=n;return true;}
int main(void) {
    uint8_t payload[1030]={0,0,0,4,0,0};for(size_t i=6;i<sizeof(payload);++i)payload[i]=i%251;
    for(size_t block=1;block<=150;++block) {
        ota_stream_t s;ota_stream_init(&s,1024);used=0;fail_write=false;
        for(size_t pos=0;pos<sizeof(payload);){size_t n=sizeof(payload)-pos;if(n>block)n=block;assert(ota_stream_feed(&s,payload+pos,n,writer,NULL));pos+=n;assert(ota_stream_complete(&s)==(pos==sizeof(payload)));}
        assert(used==1024 && !memcmp(result,payload+6,1024));
        uint8_t extra=42;assert(!ota_stream_feed(&s,&extra,1,writer,NULL));assert(!ota_stream_complete(&s));
    }
    ota_stream_t s;
    for(size_t cut=0;cut<sizeof(payload);++cut){ota_stream_init(&s,1024);used=0;assert(ota_stream_feed(&s,payload,cut,writer,NULL));assert(!ota_stream_complete(&s));}
    ota_stream_init(&s,1023);used=0;assert(!ota_stream_feed(&s,payload,6,writer,NULL));assert(!used);
    ota_stream_init(&s,1024);payload[0]=1;assert(!ota_stream_feed(&s,payload,sizeof(payload),writer,NULL));assert(!used);payload[0]=0;
    ota_stream_init(&s,1024);fail_write=true;assert(!ota_stream_feed(&s,payload,sizeof(payload),writer,NULL));assert(!ota_stream_complete(&s));fail_write=false;
    ota_stream_init(&s,1024);assert(!ota_stream_feed(&s,NULL,1,writer,NULL));
    ota_stream_init(&s,1024);uint8_t zero[6]={0};assert(!ota_stream_feed(&s,zero,6,writer,NULL));
    assert(ota_version_matches("2.2.0",0x02020000));assert(ota_version_matches("255.255.255",0xffffff00));
    assert(!ota_version_matches("2.2.0",0x02020100));assert(!ota_version_matches("2.2.0-rc1",0x02020000));assert(!ota_version_matches("2.2.0",0x02020001));
    const char *bad[]={"","2","2.","2.2","2.2.","-1.2.0","2.256.0","2.2.999999999999999999999","2.2.0evil","2.2.0\n"};
    for(size_t i=0;i<sizeof(bad)/sizeof(*bad);++i)assert(!ota_version_matches(bad[i],0x02020000));
    // Every truncated version string is safe to inspect under ASan.
    char text[16];for(size_t i=0;i<=5;++i){memcpy(text,"2.2.0",i);text[i]=0;assert(ota_version_matches(text,0x02020000)==(i==5));}
    puts("PASS: OTA subelement stream, all block boundaries, truncation, size limits, write failure and image version policy");
}
