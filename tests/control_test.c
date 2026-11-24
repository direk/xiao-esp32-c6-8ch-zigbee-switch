#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "control.h"
static command_t cmd;
static button_t b;
static channel_config_t c;
static bool poll(bool active,uint64_t time){return button_poll(&b,active,&c,time,&cmd);}
static void setup(uint8_t function) {channel_defaults(&c);c.function=function;button_init(&b,false,0);}
static void press(uint64_t time) { assert(!poll(true,time)); }
static void click(uint64_t time) {
    press(time);assert(!poll(true,time+35));assert(!poll(false,time+80));assert(!poll(false,time+115));
}
int main(void) {
    for(unsigned ep=1;ep<=8;++ep) {
        uint8_t bit=1U<<(ep-1);
        for(unsigned cluster=0;cluster<4;++cluster) {
            uint16_t ids[]={CL_ONOFF,CL_LEVEL,CL_COLOR,CL_SCENES};
            assert(binding_mask_add(0,ep,ids[cluster],3,true,false)==bit);
        }
        assert(binding_mask_add(0,ep,CL_LEVEL,1,true,false)==bit);
        assert(binding_mask_add(0,ep,CL_LEVEL,3,true,true)==0);
        assert(binding_mask_add(0,ep,CL_LEVEL,3,false,false)==0);
    }
    assert(binding_mask_add(0,9,CL_LEVEL,3,true,false)==0);
    assert(binding_mask_add(0,1,0x9999,3,true,false)==0);
    binding_scan_t scan={0};assert(binding_scan_page(&scan,0,0,0,0)==1);
    scan=(binding_scan_t){0};assert(binding_scan_page(&scan,0,4,2,0x80)==0);
    assert(binding_scan_page(&scan,2,4,2,1)==1 && scan.mask==0x81);
    scan=(binding_scan_t){0};assert(binding_scan_page(&scan,0,4,2,1)==0);
    assert(binding_scan_page(&scan,2,5,2,2)==-1 && scan.mask==1);
    setup(MODE_TOGGLE);press(10);assert(!poll(false,20));press(30);
    assert(!poll(true,64));assert(poll(true,65) && cmd.action==ACT_TOGGLE);
    assert(!poll(true,1000));assert(!poll(false,1010));assert(!poll(false,1045));
    setup(MODE_MOMENTARY);c.switch_action=ACT_ON;press(10);assert(poll(true,45)&&cmd.id==1);
    assert(!poll(false,100));assert(poll(false,135)&&cmd.id==0);
    // Both boot positions, all channels and switch_actions: each stable edge is Toggle.
    for(unsigned boot=0;boot<2;++boot)
    for(unsigned action=ACT_OFF;action<=ACT_TOGGLE;++action) {
        setup(MODE_TOGGLE);c.input_mode=INPUT_ROCKER;c.switch_action=action;
        button_init(&b,boot,0);assert(!poll(boot,500)); // No command on boot.
        assert(!poll(!boot,510));assert(!poll(boot,520));assert(!poll(!boot,530));
        assert(!poll(!boot,564));assert(poll(!boot,565));
        assert(cmd.cluster==CL_ONOFF && cmd.id==2 && cmd.action==ACT_TOGGLE);
        uint8_t frame[3];assert(command_frame(&cmd,77,frame,sizeof(frame))==3);
        assert(frame[0]==0x11 && frame[1]==77 && frame[2]==0x02);
        assert(command_direct(&cmd,true));assert(!command_reports(&cmd,true,true));
        assert(command_reports(&cmd,true,false) && command_reports(&cmd,false,true));
        assert(!poll(!boot,10000)); // Holding never repeats or overrides external changes.
        assert(!poll(boot,10010));assert(!poll(boot,10044));assert(poll(boot,10045));
        assert(cmd.cluster==CL_ONOFF && cmd.id==2 && cmd.action==ACT_TOGGLE);
    }
    setup(MODE_MOMENTARY);c.input_mode=INPUT_ROCKER;press(10);assert(poll(true,45)&&cmd.id==1);
    assert(!poll(false,100));assert(poll(false,135)&&cmd.id==0);
    setup(MODE_MULTIFUNCTION);click(10);assert(poll(false,545)&&cmd.action==ACT_SINGLE&&cmd.report_only);
    setup(MODE_MULTIFUNCTION);click(10);click(200);assert(poll(false,735)&&cmd.action==ACT_DOUBLE);
    setup(MODE_MULTIFUNCTION);click(10);click(180);click(350);assert(poll(false,885)&&cmd.action==ACT_TRIPLE);
    setup(MODE_MULTIFUNCTION);press(10);assert(!poll(true,45));
    assert(poll(true,545)&&cmd.action==ACT_HOLD);assert(!poll(true,900));
    assert(!poll(false,1000));assert(poll(false,1035)&&cmd.action==ACT_RELEASE);
    for(unsigned mode=MODE_LEVEL;mode<MODE_COUNT;++mode) {
        if(mode>=MODE_COLOR && mode<=MODE_SCENE)continue;
        setup(mode);click(10);
        bool single=poll(false,545);
        assert(single==(mode<MODE_LEVEL_NO_ONOFF));
        if(single)assert(cmd.cluster==CL_ONOFF);
        setup(mode);click(10);click(200);
        bool step=poll(false,735);
        bool alternating=mode==MODE_LEVEL||mode==MODE_LEVEL_NO_ONOFF;
        assert(step==!alternating);
        if(step){assert(cmd.cluster==CL_LEVEL&&cmd.data[1]==25);assert(cmd.id==(mode>=10?2:6));
            if(mode>=10)assert(cmd.length==6&&cmd.data[4]==1&&cmd.data[5]==0);}
        setup(mode);press(10);assert(!poll(true,45));assert(poll(true,545));
        assert(cmd.cluster==CL_LEVEL&&cmd.data[0]==((mode==MODE_LEVEL_DOWN||mode==MODE_LEVEL_DOWN_NO_ONOFF)?1:254));
        if(mode>=10)assert(cmd.id==0&&cmd.length==5&&cmd.data[3]==1&&cmd.data[4]==0);
        assert(!poll(false,600));assert(poll(false,635)&&cmd.id==3);
        if(mode>=10)assert(cmd.length==2&&cmd.data[0]==1&&cmd.data[1]==0);
        assert(!poll(false,1000));
    }
    setup(MODE_LEVEL_NO_ONOFF);press(10);assert(!poll(true,45));assert(poll(true,545)&&cmd.data[0]==254);
    assert(!poll(false,600));assert(poll(false,635));press(800);assert(!poll(true,835));
    assert(poll(true,1335)&&cmd.data[0]==1);
    assert(button_cancel(&b,&cmd)&&cmd.id==3&&cmd.length==2);
    setup(MODE_COLOR_UP);click(10);assert(poll(false,545)&&cmd.id==0x4c&&cmd.length==9);
    uint8_t frame[16];assert(command_frame(&cmd,7,frame,sizeof(frame))==12);
    assert(frame[0]==0x11&&frame[1]==7&&frame[2]==0x4c&&frame[3]==1);
    setup(MODE_SCENE);c.scene_group=123;c.scene_id=8;press(10);
    assert(poll(true,45)&&cmd.cluster==CL_SCENES&&cmd.id==5&&cmd.group==123&&cmd.data[2]==8);
    setup(MODE_LEVEL_UP_NO_ONOFF);button_init(&b,true,0);assert(!poll(true,1000)); // No startup hold.
    assert(!poll(false,1100));assert(!poll(false,1135));
    setup(MODE_LEVEL_UP_NO_ONOFF);c.input_mode=INPUT_ROCKER;assert(!channel_valid(&c));
    setup(MODE_LEVEL);c.min_level=200;c.max_level=100;assert(!channel_valid(&c));
    setup(MODE_LEVEL_UP_NO_ONOFF);press(10);assert(!poll(true,45));assert(poll(true,545));
    assert(command_frame(&cmd,99,frame,7)==0);assert(command_frame(&cmd,99,frame,16)==8);
    const uint8_t expected[]={0x11,99,0,254,25,0,1,0};assert(!memcmp(frame,expected,sizeof(expected)));
    button_t independent[CHANNELS];
    for(unsigned ep=0;ep<CHANNELS;++ep)button_init(&independent[ep],false,0);
    channel_defaults(&c);
    for(unsigned ep=0;ep<CHANNELS;++ep)assert(!button_poll(&independent[ep],ep==7,&c,10,&cmd));
    for(unsigned ep=0;ep<CHANNELS;++ep)assert(button_poll(&independent[ep],ep==7,&c,45,&cmd)==(ep==7));
    c.input_mode=INPUT_ROCKER;
    for(unsigned ep=0;ep<CHANNELS;++ep) {
        bool boot=(ep%2)!=0;button_init(&independent[ep],boot,0);
        assert(!button_poll(&independent[ep],boot,&c,500,&cmd));
        assert(!button_poll(&independent[ep],!boot,&c,510,&cmd));
    }
    for(unsigned ep=0;ep<CHANNELS;++ep) {
        bool boot=(ep%2)!=0;
        assert(button_poll(&independent[ep],!boot,&c,550,&cmd));
        assert(cmd.cluster==CL_ONOFF && cmd.id==2);
        assert(!button_poll(&independent[ep],boot,&c,600,&cmd));
    }
    for(unsigned ep=0;ep<CHANNELS;++ep) {
        assert(button_poll(&independent[ep],(ep%2)!=0,&c,640,&cmd));assert(cmd.id==2);
    }
    // Full routing matrix: priority changes action reporting, never direct delivery.
    for(unsigned report=0;report<2;++report)
    for(unsigned bound=0;bound<2;++bound)
    for(unsigned group=0;group<2;++group)
    for(unsigned priority=0;priority<2;++priority) {
        command_t route={.report_only=report,.group=group?123:0};
        bool direct=!report && (bound || group);
        assert(command_direct(&route,bound)==direct);
        assert(command_reports(&route,bound,priority)==(!direct || !priority));
    }
    puts("PASS: eight endpoints, per-cluster bindings, debounce, 13 modes, clicks/holds/releases and safe raw ZCL frames");
}
