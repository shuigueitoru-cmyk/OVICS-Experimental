/* Model/input diagnostics use separate GUI + WM states. They don't write RAM
   files or touch the live cursor/VRAM, and are labelled separately from QMP tests. */
static struct gui_context gui_test_state;
static struct wm_context gui_test_wm;
struct gui_test_result { pm_u32 checks,passed,failure,live_count_before,live_count_after; };
static struct gui_test_result gui_test_record;
static int gui_selftest(void) {
    struct gui_context *live_gui=gui;struct wm_context *live_wm=wm;
    pm_u32 active=desktop_active;struct gui_click_context live_click=gui_click;int ok=0;
    memset(&gui_test_record,0,sizeof(gui_test_record));gui_test_record.live_count_before=wm->count;
    gui=&gui_test_state;wm=&gui_test_wm;memset(gui,0,sizeof(*gui));memset(wm,0,sizeof(*wm));
    wm_init();gui_reset();desktop_active=1;
#define GUI_CHECK(c) do{gui_test_record.checks++;if(!(c)){gui_test_record.failure=gui_test_record.checks;goto done;}}while(0)
    pm_u32 explorer=gui_launch(1),terminal=gui_launch(2),about=gui_launch(3);
    GUI_CHECK(explorer && terminal && about && wm->count==3);
    GUI_CHECK(gui_launch(1)==explorer && wm->count==3 && wm->active==explorer);
    pm_u32 count=0;for(pm_u32 i=0;i<RAM_FILE_COUNT;i++)if(ram_files[i].used)count++;
    GUI_CHECK(gui->file_count==count);
    for(pm_u32 i=0;i<count;i++)GUI_CHECK(ram_files[gui->file_slots[i]].used && !strcmp(ram_files[gui->file_slots[i]].name,gui->file_names[i]));
    GUI_CHECK(gui_task_at(100,460)==201 && gui_task_at(280,460)==202 && gui_task_at(460,460)==203);
    struct mouse_event e={MOUSE_LEFT_DOWN,20,460,1,1};GUI_CHECK(desktop_mouse(&e) && gui->capture==100);
    e.type=MOUSE_LEFT_UP;e.buttons=0;GUI_CHECK(desktop_mouse(&e) && gui->menu_open);
    e.type=MOUSE_LEFT_DOWN;e.buttons=1;e.x=500;e.y=30;GUI_CHECK(desktop_mouse(&e) && !gui->menu_open);
    wm_focus_window(terminal);desktop_key('e',0);desktop_key('x',0);desktop_key('\b',0);
    GUI_CHECK(!strcmp(gui->input,"e"));
    desktop_key('\b',0);for(const char *s="echo gui diagnostic";*s;s++)desktop_key(*s,0);desktop_key('\n',0);
    GUI_CHECK(gui->commands==1 && !gui->input_length);
    int found=0;for(pm_u32 i=0;i<gui->line_count;i++)if(!strcmp(gui->lines[i],"gui diagnostic"))found=1;
    GUI_CHECK(found);
    wm_focus_window(about);desktop_key('z',0);GUI_CHECK(!gui->input_length);
    GUI_CHECK(wm_destroy_window(terminal));desktop_sync();GUI_CHECK(!gui->windows[1] && gui_task_at(280,460)==203);
    GUI_CHECK(gui_launch(2)!=terminal && wm->count==3);
    GUI_CHECK(!gui_command_supported("httpget http://example.com/"));
    for(pm_u32 i=0;i<80;i++)desktop_key('a',0);GUI_CHECK(gui->input_length==63 && gui->input_rejected==17);
    desktop_input_reset();GUI_CHECK(!gui->capture && !gui->menu_open);
    ok=1;
done:
    gui=live_gui;wm=live_wm;desktop_active=active;gui_click=live_click;gui_test_record.live_count_after=wm->count;gui_test_record.passed=ok;return ok;
#undef GUI_CHECK
}
static void gui_test_command(void) {
    print(gui_selftest()?"GUI model test PASS (isolated state)\n":"GUI model test FAIL\n");
    print("Checks: ");print_number(gui_test_record.checks);print("\nActual PS/2 desktop interaction is a separate test.\n");
}
