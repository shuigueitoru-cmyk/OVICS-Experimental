/* Explicit isolated model diagnostics. Live windows/input/VRAM remain unchanged. */
static struct wm_context wm_test_state;
struct wm_test_result { pm_u32 checks,passed,failure,live_count_before,live_count_after; };
static struct wm_test_result wm_test_record;
static int wm_selftest(void) {
    struct wm_context *live=wm;struct wm_window copy;int success=0;
    memset(&wm_test_record,0,sizeof(wm_test_record));wm_test_record.live_count_before=live->count;
    wm=&wm_test_state;memset(wm,0,sizeof(*wm));wm_init();
#define WM_CHECK(c) do{wm_test_record.checks++;if(!(c)){wm_test_record.failure=wm_test_record.checks;goto done;}}while(0)
    pm_u32 a=wm_create_window("Alpha",10,20,200,120,0x123456),b=wm_create_window("Beta",60,80,180,100,0xabcdef);
    WM_CHECK(a && b && a!=b && wm->count==2 && wm->active==b);
    WM_CHECK(wm_get_window(a,&copy) && copy.x==10 && copy.y==20 && copy.background==0x123456);
    WM_CHECK(wm_move_window(a,-2147483647-1,2147483647));
    WM_CHECK(wm_get_window(a,&copy) && copy.x==0 && copy.y==360);
    WM_CHECK(wm_hide_window(b) && !wm_find(b)->visible && wm->active==a);
    WM_CHECK(!wm_focus_window(b));
    WM_CHECK(wm_show_window(b) && wm->active==b && wm->order[1]==b);
    WM_CHECK(wm_focus_window(a) && wm->active==a && wm->order[1]==a);
    WM_CHECK(wm_destroy_window(a) && wm->count==1 && wm->active==b && !wm_find(a));
    WM_CHECK(!wm_destroy_window(a) && !wm_move_window(a,0,0) && !wm_get_window(a,&copy));
    pm_u32 c=wm_create_window("Gamma",0,0,96,64,0);WM_CHECK(c && c!=a);
    while(wm->count<8)WM_CHECK(wm_create_window("More",0,0,96,64,0));
    WM_CHECK(!wm_create_window("Ninth",0,0,96,64,0) && wm->count==8);
    WM_CHECK(!wm_destroy_window(0) && !wm_show_window(0xffffffffu) && !wm_hide_window(0) && !wm_focus_window(0));
    wm_init();WM_CHECK(wm->count==0 && !wm->active && !wm->drag_id);
    WM_CHECK(!wm_get_window(c,&copy));
    WM_CHECK(!wm_create_window(0,0,0,100,100,0));
    WM_CHECK(!wm_create_window("",0,0,100,100,0));
    WM_CHECK(!wm_create_window("12345678901234567890123456789012",0,0,100,100,0));
    WM_CHECK(!wm_create_window("bad\n",0,0,100,100,0));
    WM_CHECK(!wm_create_window("bad",0,0,2147483647,100,0));
    WM_CHECK(!wm_create_window("bad",0,0,100,-2147483647-1,0));
    a=wm_create_window("1234567890123456789012345678901",2147483647,-2147483647-1,100,100,0xff123456);
    WM_CHECK(a && wm_get_window(a,&copy) && copy.x==540 && !copy.y && copy.background==0x123456);
    WM_CHECK(wm_move_window(a,30,40));
    struct mouse_event e={MOUSE_LEFT_DOWN,40,50,1,1};
    WM_CHECK(wm_handle_mouse_event(&e) && wm->drag_id==a && wm->drag_dx==10 && wm->drag_dy==10);
    e.type=MOUSE_MOVE;e.x=70;e.y=80;e.sequence++;
    WM_CHECK(wm_handle_mouse_event(&e) && wm_find(a)->x==60 && wm_find(a)->y==70);
    e.type=MOUSE_LEFT_UP;e.buttons=0;e.sequence++;
    WM_CHECK(wm_handle_mouse_event(&e) && !wm->drag_id);
    e.type=MOUSE_MOVE;e.x=200;e.y=200;e.sequence++;
    WM_CHECK(wm_handle_mouse_event(&e) && wm_find(a)->x==60);
    e.type=MOUSE_LEFT_DOWN;e.buttons=1;e.x=70;e.y=80;e.sequence++;
    WM_CHECK(wm_handle_mouse_event(&e) && wm->drag_id==a);
    e.type=MOUSE_MOVE;e.sequence+=2;
    WM_CHECK(wm_handle_mouse_event(&e) && !wm->drag_id && wm->recoveries==1);
    WM_CHECK(!wm_handle_mouse_event(0));
    e.x=640;e.sequence++;WM_CHECK(!wm_handle_mouse_event(&e));e.x=0;
    e.type=99;WM_CHECK(!wm_handle_mouse_event(&e));e.type=MOUSE_LEFT_DOWN;e.buttons=0;
    WM_CHECK(!wm_handle_mouse_event(&e));
    wm_init();wm->next_id=0xfffffffeu;
    WM_CHECK(wm_create_window("Last A",0,0,100,100,0)==0xfffffffeu);
    WM_CHECK(wm_create_window("Last B",0,0,100,100,0)==0xffffffffu);
    WM_CHECK(!wm_create_window("Wrap",0,0,100,100,0));
    wm_init();WM_CHECK(!wm_create_window("Still exhausted",0,0,100,100,0));
    success=1;
done:
    wm=live;wm_test_record.live_count_after=live->count;wm_test_record.passed=success;return success;
#undef WM_CHECK
}
static void wm_test_command(void) {
    print(wm_selftest()?"Window manager model test PASS (isolated state)\n":"Window manager model test FAIL\n");
    print("Checks: ");print_number(wm_test_record.checks);print("\nPS/2 interaction: use wmdemo (not certified by this model test).\n");
}
