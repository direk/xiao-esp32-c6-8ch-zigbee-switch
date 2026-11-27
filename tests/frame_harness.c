#include <stdio.h>
#include <string.h>
#include "control.h"
static void emit(unsigned mode,const char *phase,command_t *c){
    uint8_t frame[16];size_t n=command_frame(c,42,frame,sizeof(frame));
    if(!n)return;
    printf("%u %s %u ",mode,phase,c->cluster);
    for(size_t i=0;i<n;++i)printf("%02x",frame[i]);
    putchar('\n');
}
static void sample(button_t *b,channel_config_t *c,unsigned mode,bool active,uint64_t now,const char *phase){
    command_t o;if(button_poll(b,active,c,now,&o))emit(mode,phase,&o);
}
int main(void){
    for(unsigned mode=0;mode<MODE_COUNT;++mode){
        channel_config_t c;channel_defaults(&c);c.function=mode;c.scene_group=123;c.scene_id=8;memcpy(c.scene_list,"\0050,1,3",6);
        if(mode==MODE_SCENE_CYCLE)continue;
        for(unsigned scenario=0;scenario<3;++scenario){
            button_t b;button_init(&b,false,0);
            sample(&b,&c,mode,true,10,"press");sample(&b,&c,mode,true,45,"press");
            if(scenario==0){
                sample(&b,&c,mode,true,545,"hold");sample(&b,&c,mode,false,600,"release");sample(&b,&c,mode,false,635,"release");
            }else{
                sample(&b,&c,mode,false,80,"release");sample(&b,&c,mode,false,115,"release");
                if(scenario==2){
                    sample(&b,&c,mode,true,200,"press");sample(&b,&c,mode,true,235,"press");
                    sample(&b,&c,mode,false,280,"release");sample(&b,&c,mode,false,315,"release");
                }
                sample(&b,&c,mode,false,800,scenario==1 ? "single" : "double");
            }
        }
    }
    for(unsigned boot=0;boot<2;++boot) {
        channel_config_t c;channel_defaults(&c);c.input_mode=INPUT_ROCKER;
        button_t b;button_init(&b,boot,0);
        sample(&b,&c,0,!boot,10,"rocker_edge");sample(&b,&c,0,!boot,45,"rocker_edge");
        sample(&b,&c,0,boot,100,"rocker_edge");sample(&b,&c,0,boot,135,"rocker_edge");
    }

    channel_config_t c;channel_defaults(&c);c.switch_action=SWITCH_ACTION_ON_OFF;
    button_t b;button_init(&b,false,0);
    for(unsigned n=0;n<4;++n) {
        uint64_t t=10+n*200;
        sample(&b,&c,0,true,t,"on_off");sample(&b,&c,0,true,t+35,"on_off");
        sample(&b,&c,0,false,t+80,"on_off");sample(&b,&c,0,false,t+115,"on_off");
    }

    c.function=MODE_SCENE_CYCLE;c.scene_group=321;memcpy(c.scene_list,"\0053,0,1",6);button_init(&b,false,0);
    for(unsigned n=0;n<4;++n) {
        uint64_t t=10+n*200;
        sample(&b,&c,MODE_SCENE_CYCLE,true,t,"scene_cycle");sample(&b,&c,MODE_SCENE_CYCLE,true,t+35,"scene_cycle");
        sample(&b,&c,MODE_SCENE_CYCLE,false,t+80,"scene_cycle");sample(&b,&c,MODE_SCENE_CYCLE,false,t+115,"scene_cycle");
    }

}
