#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "binding_table.h"

static const uint8_t own[8]={1,2,3,4,5,6,7,8};
static const uint8_t coord[8]={11,12,13,14,15,16,17,18};
static const uint8_t lamp[8]={21,22,23,24,25,26,27,28};
static size_t record(uint8_t *p, unsigned ep, uint16_t cluster, unsigned mode, const uint8_t *source, const uint8_t *destination) {
    memcpy(p,source,8);p[8]=ep;p[9]=cluster;p[10]=cluster>>8;p[11]=mode;
    if(mode==1){p[12]=42;p[13]=0;return 14;}
    memcpy(p+12,destination,8);p[20]=1;return 21;
}
static void header(uint8_t *p,unsigned index,unsigned total,unsigned count){p[0]=77;p[1]=0;p[2]=total;p[3]=index;p[4]=count;}
int main(void) {
    uint8_t data[800]={0}; binding_page_t page,poison;
    // Regression: group entries have 14 bytes, not the SDK's fixed 21-byte stride.
    for(unsigned first=1;first<=3;first+=2)
    for(unsigned second=1;second<=3;second+=2) {
        header(data,0,2,2);size_t n=5;
        n+=record(data+n,1,CL_ONOFF,first,own,lamp);
        n+=record(data+n,2,CL_ONOFF,second,own,lamp);
        assert(binding_page_decode(data,n,own,coord,&page));
        assert(page.mask==3 && page.clusters[0]==3 && page.count==2 && page.total==2 && page.tsn==77);
        for(size_t cut=0;cut<n;++cut){memset(&poison,0xa5,sizeof(poison));page=poison;assert(!binding_page_decode(data,cut,own,coord,&page));assert(!memcmp(&poison,&page,sizeof(page)));}
        data[n]=0;assert(!binding_page_decode(data,n+1,own,coord,&page));
    }
    // All eight endpoints, both orders, all four command clusters, group/unicast mixes.
    uint16_t clusters[]={CL_ONOFF,CL_LEVEL,CL_COLOR,CL_SCENES};
    for(unsigned reverse=0;reverse<2;++reverse)
    for(unsigned cluster=0;cluster<4;++cluster) {
        header(data,0,8,8);size_t n=5;
        for(unsigned i=0;i<8;++i){unsigned ep=reverse?8-i:i+1;n+=record(data+n,ep,clusters[cluster],i%2?3:1,own,lamp);}
        assert(binding_page_decode(data,n,own,coord,&page));assert(page.mask==0xff && page.clusters[cluster]==0xff);
    }
    // Pagination accumulates endpoints without changing earlier pages.
    binding_scan_t scan={0};uint8_t masks[4]={0};
    for(unsigned index=0;index<8;index+=2) {
        header(data,index,8,2);size_t n=5;
        n+=record(data+n,index+1,CL_ONOFF,1,own,lamp);n+=record(data+n,index+2,CL_LEVEL,3,own,lamp);
        assert(binding_page_decode(data,n,own,coord,&page));
        assert(binding_scan_page(&scan,page.index,page.total,page.count,page.mask)==(index==6?1:0));
        for(unsigned c=0;c<4;++c)masks[c]|=page.clusters[c];
    }
    assert(scan.mask==0xff && masks[0]==0x55 && masks[1]==0xaa);
    // Foreign sources, coordinator/self destinations, unsupported endpoints/clusters.
    header(data,0,7,7);size_t n=5;
    n+=record(data+n,1,CL_ONOFF,3,own,coord);
    n+=record(data+n,2,CL_LEVEL,3,own,own);
    n+=record(data+n,3,CL_COLOR,1,lamp,lamp);
    n+=record(data+n,0,CL_ONOFF,1,own,lamp);
    n+=record(data+n,9,CL_ONOFF,1,own,lamp);
    n+=record(data+n,4,0xfc00,3,own,lamp);
    n+=record(data+n,8,CL_SCENES,1,own,lamp);
    assert(binding_page_decode(data,n,own,coord,&page));assert(page.mask==0x80 && page.clusters[3]==0x80);
    // Maximum table and empty table; malformed headers, modes and failure status.
    header(data,0,32,32);n=5;for(unsigned i=0;i<32;++i)n+=record(data+n,i%8+1,CL_ONOFF,i%2?1:3,own,lamp);
    assert(binding_page_decode(data,n,own,NULL,&page) && page.mask==0xff);
    header(data,0,0,0);assert(binding_page_decode(data,5,own,coord,&page) && !page.mask);
    header(data,0,1,0);assert(!binding_page_decode(data,5,own,coord,&page));
    header(data,0,33,1);assert(!binding_page_decode(data,5,own,coord,&page));
    header(data,0,1,1);n=5+record(data+5,1,CL_ONOFF,1,own,lamp);data[16]=2;assert(!binding_page_decode(data,n,own,coord,&page));
    data[1]=0x84;assert(!binding_page_decode(data,2,own,coord,&page));
    assert(!binding_page_decode(NULL,5,own,coord,&page));
    unsigned random=1234;
    for(unsigned trial=0;trial<10000;++trial) {
        size_t length=trial%sizeof(data);
        for(size_t i=0;i<length;++i){random=random*1664525U+1013904223U;data[i]=random>>24;}
        memset(&poison,0xa5,sizeof(poison));page=poison;
        if(!binding_page_decode(data,length,own,coord,&page))assert(!memcmp(&poison,&page,sizeof(page)));
    }
    puts("Binding table wire-format tests passed");
}
