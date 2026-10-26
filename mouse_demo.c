/* Shell/demo consumer. Driver does not draw or invoke CUI functions. */
static int mouse_demo_active;
static pm_u32 mouse_demo_color_changes,mouse_demo_draws;
static pm_u32 mouse_demo_last_sequence=0xffffffffu;
static void mouse_demo_number(int x,int y,pm_u32 n) {
    char buf[11];int at=10;buf[at]=0;
    do{buf[--at]=(char)('0'+n%10);n/=10;}while(n && at);
    gfx_text(x,y,&buf[at],0xffffff);
}
static void mouse_demo_redraw(int shape) {
    /* One cursor transaction spans all changing UI fields. */
    cursor_graphics_begin();
    gfx_fill_rect(24,76,580,112,0x162339);
    gfx_text(24,80,"X:",0xffffff);mouse_demo_number(56,80,mouse_state.x);
    gfx_text(180,80,"Y:",0xffffff);mouse_demo_number(212,80,mouse_state.y);
    gfx_text(24,112,mouse_is_pressed(MOUSE_LEFT)?"LEFT: DOWN":"LEFT: UP",0xffffff);
    gfx_text(200,112,mouse_is_pressed(MOUSE_RIGHT)?"RIGHT: DOWN":"RIGHT: UP",0xffffff);
    gfx_text(400,112,mouse_is_pressed(MOUSE_MIDDLE)?"MIDDLE: DOWN":"MIDDLE: UP",0xffffff);
    gfx_text(24,144,"LEFT PRESS CHANGES THE RECTANGLE COLOR",0x88ccee);
    if(shape){gfx_fill_rect(80,230,480,160,(mouse_demo_color_changes&1)?0x2ab878:0x3355aa);
    gfx_rect(80,230,480,160,0xffffff);}
    gfx_text(24,420,"PS/2 INPUT - ESC RETURNS TO VGA CUI",0xffffff);
    cursor_graphics_end();mouse_demo_draws++;
}
static void mouse_demo_start(void) {
    if(!mouse_state.ready){print("Mouse unavailable: ");print(mouse_error);print("\n");return;}
    if(!gfx_enter()){print("Graphics unavailable: ");print(gfx_error);print("\n");return;}
    struct mouse_event event;while(mouse_next_event(&event)){} /* discard historical events */
    mouse_demo_active=1;mouse_demo_color_changes=0;mouse_demo_last_sequence=mouse_state.sequence;
    gfx_clear(0x162339);gfx_text(24,24,"OVICS Mouse Demo",0xffffff);gfx_rect(0,0,640,480,0x55ccff);
    mouse_demo_redraw(1);cursor_state.enabled=1;cursor_draw();
}
static void mouse_ui_tick(void) {
    struct mouse_event event;pm_u32 changed=0;
    for(pm_u32 budget=0;mouse_demo_active && budget<MOUSE_EVENTS && mouse_next_event(&event);budget++) {
        if(mouse_demo_active && event.type==MOUSE_LEFT_DOWN){mouse_demo_color_changes++;changed=1;}
    }
    if(mouse_demo_active && !gfx_stats.active)mouse_demo_active=0;
    if(mouse_demo_active && (changed || mouse_demo_last_sequence!=mouse_state.sequence)) {
        mouse_demo_last_sequence=mouse_state.sequence;mouse_demo_redraw(changed);
    }
    cursor_update();
}
static void mouse_print_info(void) {
    print("Mouse Detected: ");print(mouse_state.detected?"Yes\n":"No\n");
    print("Driver Status: ");print(mouse_state.ready?"Ready (8042 AUX polling, 3-byte)\n":"Unavailable\n");
    print("Current X: ");print_number(mouse_state.x);print("\nCurrent Y: ");print_number(mouse_state.y);
    print("\nButton Status (L=1 R=2 M=4): ");print_number(mouse_state.buttons);
    print("\nPacket Count: ");print_number(mouse_state.packets);print("\nError Count: ");print_number(mouse_state.errors);
    print("\nOverflow packets: ");print_number(mouse_state.overflow);print("\nQueue dropped: ");print_number(mouse_state.dropped);
    print("\nKeyboard dropped: ");print_number(ps2_key_dropped);print("\nLast mouse error: ");print(mouse_error);print("\n");
}
/* Synthetic parser diagnostics use a separate device/queue. They neither forge
   hardware packet counters nor move the live pointer. */
struct mouse_test_result { pm_u32 checks,passed,failure,live_packets_before,live_packets_after; };
static struct mouse_test_result mouse_test_record;
static struct mouse_device mouse_test_device;
static struct mouse_event mouse_test_events[MOUSE_EVENTS];
static int mouse_selftest(void) {
    struct mouse_device *s=&mouse_test_device;memset(s,0,sizeof(*s));s->x=320;s->y=240;
    memset(&mouse_test_record,0,sizeof(mouse_test_record));mouse_test_record.live_packets_before=mouse_state.packets;
#define MOUSE_CHECK(c) do{mouse_test_record.checks++;if(!(c)){mouse_test_record.failure=mouse_test_record.checks;return 0;}}while(0)
#define PACKET(a,b,c) do{mouse_decode(s,mouse_test_events,a,0);mouse_decode(s,mouse_test_events,b,0);mouse_decode(s,mouse_test_events,c,0);}while(0)
    PACKET(8,10,20);MOUSE_CHECK(s->x==330 && s->y==220 && s->packets==1);
    PACKET(0x38,246,236);MOUSE_CHECK(s->x==320 && s->y==240);
    for(int i=0;i<4;i++){PACKET(8,255,255);}MOUSE_CHECK(s->x==639 && s->y==0);
    for(int i=0;i<4;i++){PACKET(0x38,0,0);}MOUSE_CHECK(s->x==0 && s->y==479);
    PACKET(0x0f,0,0);MOUSE_CHECK(s->buttons==7);
    MOUSE_CHECK(mouse_test_events[(s->head+MOUSE_EVENTS-3)%MOUSE_EVENTS].type==MOUSE_LEFT_DOWN);
    MOUSE_CHECK(mouse_test_events[(s->head+MOUSE_EVENTS-2)%MOUSE_EVENTS].type==MOUSE_RIGHT_DOWN);
    MOUSE_CHECK(mouse_test_events[(s->head+MOUSE_EVENTS-1)%MOUSE_EVENTS].type==MOUSE_MIDDLE_DOWN);
    PACKET(8,0,0);MOUSE_CHECK(!s->buttons);
    MOUSE_CHECK(mouse_test_events[(s->head+MOUSE_EVENTS-3)%MOUSE_EVENTS].type==MOUSE_LEFT_UP);
    MOUSE_CHECK(mouse_test_events[(s->head+MOUSE_EVENTS-2)%MOUSE_EVENTS].type==MOUSE_RIGHT_UP);
    MOUSE_CHECK(mouse_test_events[(s->head+MOUSE_EVENTS-1)%MOUSE_EVENTS].type==MOUSE_MIDDLE_UP);
    pm_u32 x=s->x,y=s->y,e=s->errors,p=s->packets;
    mouse_decode(s,mouse_test_events,0,0);MOUSE_CHECK(s->errors==e+1 && s->sync_errors==1 && s->packets==p);
    PACKET(0xcf,127,127);MOUSE_CHECK(s->x==x && s->y==y && s->buttons==7 && s->overflow==1);
    PACKET(0xc8,255,255);MOUSE_CHECK(s->buttons==0 && s->overflow==2 && s->x==x && s->y==y);
    mouse_decode(s,mouse_test_events,8,0);mouse_decode(s,mouse_test_events,8,300);
    MOUSE_CHECK(s->timeouts==1 && s->partial==1);
    mouse_decode(s,mouse_test_events,1,300);mouse_decode(s,mouse_test_events,1,300);MOUSE_CHECK(s->partial==0);
    for(int i=0;i<200;i++){PACKET((i&1)?8:9,0,0);}
    MOUSE_CHECK(s->queued==MOUSE_EVENTS && s->dropped>0 && s->head<MOUSE_EVENTS && s->tail<MOUSE_EVENTS);
    MOUSE_CHECK(s->buttons==0 && mouse_test_events[(s->head+MOUSE_EVENTS-1)%MOUSE_EVENTS].type==MOUSE_LEFT_UP);
    mouse_test_record.live_packets_after=mouse_state.packets;
    MOUSE_CHECK(mouse_test_record.live_packets_after==mouse_test_record.live_packets_before);
    mouse_test_record.passed=1;return 1;
#undef PACKET
#undef MOUSE_CHECK
}
static void mouse_test_command(void) {
    print(mouse_selftest()?"Mouse parser test PASS (synthetic, isolated state)\n":"Mouse parser test FAIL\n");
    print("Live PS/2 packets observed: ");print_number(mouse_state.packets);
    print("\nHardware interaction: use mousedemo; not certified by this parser test.\n");
}
