/* Compiled ONLY into build/window-api/ovics.elf. Keyboard-driven API fixture,
   not an application and not present in the normal release image. */
struct wm_fixture_status { pm_u32 ready,checks,passed,failure,ids[5],replacement; };
static struct wm_fixture_status wm_fixture_record;
static void wm_fixture_check(int ok) {
    wm_fixture_record.checks++;if(!ok && !wm_fixture_record.failure)wm_fixture_record.failure=wm_fixture_record.checks;
    wm_fixture_record.passed=!wm_fixture_record.failure;
}
static void wm_fixture_start(void) {
    memset(&wm_fixture_record,0,sizeof(wm_fixture_record));
    for(pm_u32 i=0;i<5;i++) {
        char name[]="Fixture 4";name[8]=(char)('4'+i);
        wm_fixture_record.ids[i]=wm_create_window(name,20+(int)i*65,270-(int)i*25,140,120,0x669977u+i*0x10101u);
        wm_fixture_check(wm_fixture_record.ids[i]!=0);
    }
    wm_fixture_record.ready=1;
}
static void wm_fixture_key(char c) {
    pm_u32 id=wm_fixture_record.ids[4];
    if(c=='h')wm_fixture_check(wm_hide_window(id) && !wm_find(id)->visible);
    if(c=='s')wm_fixture_check(wm_show_window(id) && wm->active==id);
    if(c=='n')wm_fixture_check(!wm_create_window("Ninth",0,0,100,100,0) && wm->count==8);
    if(c=='m')wm_fixture_check(wm_move_window(id,2147483647,-2147483647-1) && wm_find(id)->x==500 && !wm_find(id)->y);
    if(c=='x') {
        wm_fixture_check(wm_destroy_window(id));
        wm_fixture_record.replacement=wm_create_window("Replacement",490,300,140,120,0x996688);
        wm_fixture_check(wm_fixture_record.replacement && wm_fixture_record.replacement!=id && wm->count==8);
        wm_fixture_check(!wm_move_window(id,0,0) && !wm_destroy_window(id) && !wm_focus_window(id));
    }
    if(c=='i')wm_fixture_check(!wm_create_window("bad",0,0,-1,100,0) && !wm_get_window(0,0));
    if(c=='f')wm_fixture_check(wm_focus_window(1) && wm->active==1);
}
