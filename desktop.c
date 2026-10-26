/* Desktop shell, app controllers and client renderers. No fake filesystem data.
   Single instance of each app, static bounded state; no per-window framebuffer. */
#define GUI_EXPLORER 1u
#define GUI_TERMINAL 2u
#define GUI_ABOUT 3u
#define TERM_COLS 56u
#define TERM_LINES 64u
#define GUI_DOUBLE_CLICK_MS 500u
#define GUI_CLICK_SLOP 4
struct gui_click_context {
 pm_u32 selected_icon,down_icon,down_x,down_y,dragged,last_icon,last_x,last_y,last_valid;
 pm_u64 down_ms,last_ms;
 pm_u32 single_clicks,double_clicks,rejected;
};
static struct gui_click_context gui_click;
static int gui_click_near(pm_u32 x,pm_u32 y,pm_u32 xx,pm_u32 yy) {
 return (x>xx?x-xx:xx-x)<=GUI_CLICK_SLOP && (y>yy?y-yy:yy-y)<=GUI_CLICK_SLOP;
}
static int gui_double_within(pm_u64 old,pm_u64 now) {return now>=old && now-old<=GUI_DOUBLE_CLICK_MS;}
static void gui_click_reset(void){memset(&gui_click,0,sizeof(gui_click));}
struct gui_context {
 pm_u32 windows[4],menu_open,capture,sessions,launches,closes,refreshes,commands,unsupported;
 pm_u32 file_count,file_slots[16];
 char file_names[16][32];
 int selected;
 char lines[TERM_LINES][TERM_COLS+1];
 pm_u32 line_count,column,scrolls,input_length,input_rejected;
 char input[64];
};
static struct gui_context gui_state;
static struct gui_context *gui=&gui_state;
static const char *gui_app_name(pm_u32 app) {
    return app==GUI_EXPLORER?"Explorer":app==GUI_TERMINAL?"Terminal":app==GUI_ABOUT?"About":app==4?"Horizon":"None";
}
static pm_u32 gui_app_for_window(pm_u32 id) {
    if(!id)return 0;
    for(pm_u32 i=0;i<4;i++)if(gui->windows[i]==id)return i+1;
    return 0;
}
static void gui_reset(void) {
    pm_u32 sessions=gui->sessions+1;memset(gui,0,sizeof(*gui));gui->sessions=sessions;
    gui->selected=-1;gui->line_count=1;gui_click_reset();browser_reset();
}
static void gui_refresh_files(void) {
    gui->file_count=0;gui->selected=-1;
    for(pm_u32 i=0;i<RAM_FILE_COUNT;i++)if(ram_files[i].used) {
        pm_u32 n=gui->file_count++;gui->file_slots[n]=i;
        for(pm_u32 j=0;j<RAM_NAME_SIZE;j++)gui->file_names[n][j]=ram_files[i].name[j];
        gui->file_names[n][RAM_NAME_SIZE-1]=0;
    }
    gui->refreshes++;wm->dirty=1;
}
static void term_newline(void) {
    gui->column=0;
    if(gui->line_count<TERM_LINES)gui->line_count++;
    else {
        for(pm_u32 i=0;i<TERM_LINES-1;i++)for(pm_u32 j=0;j<=TERM_COLS;j++)gui->lines[i][j]=gui->lines[i+1][j];
        gui->scrolls++;
    }
    for(pm_u32 j=0;j<=TERM_COLS;j++)gui->lines[gui->line_count-1][j]=0;
}
static void term_output(char c) {
    if(c=='\r')return;
    if(c=='\n'){term_newline();return;}
    if(gui->column==TERM_COLS)term_newline();
    if((unsigned char)c<32 || (unsigned char)c>126)c='?';
    char *row=gui->lines[gui->line_count-1];row[gui->column++]=c;row[gui->column]=0;
}
static void term_puts(const char *s){while(*s)term_output(*s++);}
static int gui_command_supported(const char *s) {
    return strcmp(s,"help")==0 || strcmp(s,"version")==0 || strcmp(s,"ls")==0 || strcmp(s,"meminfo")==0 || strcmp(s,"vmeminfo")==0 ||
        strcmp(s,"echo")==0 || starts_with(s,"echo ") || strcmp(s,"touch")==0 || starts_with(s,"touch ") ||
        strcmp(s,"write")==0 || starts_with(s,"write ") || strcmp(s,"cat")==0 || starts_with(s,"cat ") || strcmp(s,"rm")==0 || starts_with(s,"rm ");
}
static void term_execute(void) {
    gui->input[gui->input_length]=0;term_puts("OVICS> ");term_puts(gui->input);term_output('\n');
    if(gui->input_length) {
        if(gui_command_supported(gui->input)) {
            void (*old)(char)=console_sink;console_sink=term_output;
            run_command(gui->input);console_sink=old;
            if(strcmp(gui->input,"help")==0)term_puts("GUI: help version echo ls meminfo vmeminfo\n     touch write cat rm\nOther commands are unsupported in GUI.\n");
        } else {term_puts("Unsupported in GUI: ");term_puts(gui->input);term_output('\n');gui->unsupported++;}
        gui->commands++;
    }
    gui->input_length=0;gui->input[0]=0;wm->dirty=1;
}
static pm_u32 gui_launch(pm_u32 app) {
    if(app<1 || app>4)return 0;
    pm_u32 id=gui->windows[app-1];
    if(wm_find(id)){wm_show_window(id);gui->menu_open=0;return id;}
    const char *title=app==1?"OVICS Explorer":app==2?"OVICS Terminal":app==3?"About OVICS":"OVICS Horizon";
    int width=app==1?440:app==2?480:app==3?380:600,height=app==1?340:app==2?330:app==3?260:370;
    id=wm_create_window(title,app==1?126:app==2?100:app==4?20:150,app==1?60:app==2?80:app==4?40:90,width,height,app==2?0x122035:app==1?0xe6edf3:0xf2ead7);
    if(!id)return 0;
    gui->windows[app-1]=id;gui->launches++;gui->menu_open=0;
    if(app==4)browser_prepare();
    if(app==1)gui_refresh_files();
    if(app==2) {
        memset(gui->lines,0,sizeof(gui->lines));gui->line_count=1;gui->column=gui->scrolls=gui->input_length=0;gui->input[0]=0;
        term_puts("OVICS GUI Terminal\nType help for commands.\n");
    }
    return id;
}
static void desktop_sync(void) {
    if(!desktop_active)return;
    for(pm_u32 i=0;i<4;i++)if(gui->windows[i] && !wm_find(gui->windows[i])) {
        gui->windows[i]=0;gui->closes++;wm->dirty=1;
    }
    if(gui->selected>=0) {
        pm_u32 n=(pm_u32)gui->selected;
        if(n>=gui->file_count || !ram_files[gui->file_slots[n]].used || strcmp(ram_files[gui->file_slots[n]].name,gui->file_names[n])) {
            gui->selected=-1;wm->dirty=1;
        }
    }
}
static void desktop_input_reset(void){if(desktop_active){gui->capture=gui->menu_open=0;gui_click_reset();wm->dirty=1;}}
static void desktop_mode_leave(void){desktop_active=0;gui->capture=0;gui->menu_open=0;gui_click_reset();}
static int gui_box(int x,int y,int left,int top,int width,int height){return x>=left && x<left+width && y>=top && y<top+height;}
static pm_u32 gui_task_at(int x,int y) {
    if(y<452 || y>=476)return 0;
    if(x>=4 && x<78)return 100;
    pm_u32 at=0,total=0;for(pm_u32 i=0;i<4;i++)if(gui->windows[i])total++;
    int stride=total>3?136:178;
    for(pm_u32 i=0;i<4;i++)if(gui->windows[i]) {
        if(x>=86+(int)at*stride && x<86+(int)at*stride+stride-4)return 200+i+1;
        at++;
    }
    return 0;
}
static pm_u32 gui_menu_at(int x,int y) {
    if(gui_box(x,y,4,320,192,24))return 405;
    if(!gui_box(x,y,4,344,192,96))return 0;
    return 400+(pm_u32)(y-344)/24+1;
}
static pm_u32 gui_icon_at(int x,int y) {
    for(pm_u32 i=0;i<4;i++)if(gui_box(x,y,12,64+(int)i*84,80,68))return 300+i+1;
    return 0;
}
static int desktop_mouse(const struct mouse_event *e) {
    if(!desktop_active)return 0;
    int x=(int)e->x,y=(int)e->y;pm_u32 target=0;
    pm_u64 stamp=0;int timed=mouse_event_time(e->sequence,&stamp) && !timer_watchdog_fault;
    if(e->type==MOUSE_LEFT_DOWN && !(!wm_hit(x,y) && gui_icon_at(x,y) && y<448))gui_click.last_valid=0;
    if(e->type==MOUSE_MOVE && gui_click.down_icon && !gui_click_near(e->x,e->y,gui_click.down_x,gui_click.down_y))gui_click.dragged=1;
    if(gui->menu_open) {
        if(e->type==MOUSE_LEFT_DOWN) {
            target=gui_task_at(x,y);if(target!=100)target=gui_menu_at(x,y);
            wm_cancel_capture();gui->capture=target;
            if(!target){gui->menu_open=0;wm->dirty=1;}return 1;
        }
        if(e->type==MOUSE_LEFT_UP && gui->capture) {
            pm_u32 capture=gui->capture;gui->capture=0;
            target=capture==100?gui_task_at(x,y):gui_menu_at(x,y);
            if(capture==target) {
                if(capture==100 || capture==404)gui->menu_open=0;
                else if(capture>=401 && capture<=403)gui_launch(capture-400);
                else if(capture==405)gui_launch(4);
                wm->dirty=1;
            }
            return 1;
        }
        return 1; /* popup blocks click-through including motion */
    }
    if(e->type==MOUSE_LEFT_UP && gui->capture) {
        pm_u32 capture=gui->capture;gui->capture=0;
        target=capture>=301 && capture<=304?(!wm_hit(x,y)?gui_icon_at(x,y):0):gui_task_at(x,y);
        if(capture>=301 && capture<=304 && capture!=target){gui_click.last_valid=0;gui_click.rejected++;}
        if(capture==target) {
            if(capture==100){gui->menu_open=1;wm_cancel_capture();}
            else if(capture>=201 && capture<=204)wm_show_window(gui->windows[capture-201]);
            else if(capture>=301 && capture<=304) {
                pm_u32 app=capture-300;
                if(timed && gui_click.down_icon==app && !gui_click.dragged && gui_double_within(gui_click.down_ms,stamp)) {
                    gui_click.selected_icon=app;gui_click.single_clicks++;
                    if(gui_click.last_valid && gui_click.last_icon==app && gui_double_within(gui_click.last_ms,stamp) && gui_click_near(e->x,e->y,gui_click.last_x,gui_click.last_y)) {
                        gui_launch(app);gui_click.double_clicks++;gui_click.last_valid=0;
                    } else {gui_click.last_valid=1;gui_click.last_icon=app;gui_click.last_ms=stamp;gui_click.last_x=e->x;gui_click.last_y=e->y;}
                } else {gui_click.last_valid=0;gui_click.rejected++;}
            }
            wm->dirty=1;
        }
        gui_click.down_icon=0;return 1;
    }
    if(e->type==MOUSE_MOVE && gui->capture) {
        if(!(e->buttons&1)){gui->capture=0;gui_click.down_icon=gui_click.last_valid=0;}return 1;
    }
    if(e->type!=MOUSE_LEFT_DOWN)return 0;
    if(y>=448) {gui->capture=gui_task_at(x,y);wm_cancel_capture();return 1;}
    struct wm_window *w=wm_hit(x,y);
    if(w && y>=w->y+WM_TITLE_HEIGHT && gui_app_for_window(w->id)==4){wm_focus_window(w->id);browser_mouse(w,x,y);return 1;}
    if(w && y>=w->y+WM_TITLE_HEIGHT && gui_app_for_window(w->id)==GUI_EXPLORER) {
        wm_focus_window(w->id);
        if(gui_box(x,y,w->x+8,w->y+28,80,22))gui_refresh_files();
        else if(gui_box(x,y,w->x+8,w->y+58,152,256)) {
            pm_u32 row=(pm_u32)(y-w->y-58)/16;
            if(row<gui->file_count){gui->selected=(int)row;wm->dirty=1;}
        }
        return 1;
    }
    if(!w) {
        gui->capture=gui_icon_at(x,y);
        if(gui->capture){
            wm_cancel_capture();gui_click.down_icon=gui->capture-300;gui_click.down_x=e->x;gui_click.down_y=e->y;gui_click.down_ms=stamp;gui_click.dragged=!timed;return 1;
        }
    }
    return 0;
}
static void desktop_key(char c,int control) {
    if(!desktop_active || gui->menu_open)return;
    if(gui_app_for_window(wm->active)==4){browser_key(c,control);return;}
    if(control || gui_app_for_window(wm->active)!=GUI_TERMINAL)return;
    if(c=='\n')term_execute();
    else if(c=='\b'){if(gui->input_length)gui->input[--gui->input_length]=0;wm->dirty=1;}
    else if(c>=32 && c<=126) {
        if(gui->input_length<63){gui->input[gui->input_length++]=c;gui->input[gui->input_length]=0;}
        else gui->input_rejected++;
        wm->dirty=1;
    }
}
static void gui_text_line(int x,int y,const char *s,pm_u32 max,pm_u32 color) {
    char line[65];pm_u32 n=0;if(max>64)max=64;
    while(n<max && s[n] && s[n]!='\n' && s[n]!='\r') {unsigned char c=(unsigned char)s[n];line[n]=c>=32 && c<=126?(char)c:'?';n++;}
    line[n]=0;gfx_text(x,y,line,color);
}
static void gui_text_block(int x,int y,const char *s,pm_u32 cols,pm_u32 rows,pm_u32 color) {
    pm_u32 at=0;
    for(pm_u32 row=0;row<rows && s[at];row++) {
        char line[65];pm_u32 n=0;
        while(s[at] && n<cols && at<1024) {
            unsigned char c=(unsigned char)s[at++];if(c=='\r')continue;if(c=='\n')break;
            line[n++]=c>=32 && c<=126?(char)c:'?';
        }
        line[n]=0;gfx_text(x,y+(int)row*16,line,color);
    }
}
static void desktop_content(const struct wm_window *w) {
    pm_u32 app=gui_app_for_window(w->id);int x=w->x,y=w->y;
    if(app==GUI_EXPLORER) {
        gfx_fill_rect(x+8,y+28,80,22,0xcedbe7);gfx_rect(x+8,y+28,80,22,0x50637a);gfx_text(x+16,y+31,"Refresh",0x152536);
        gfx_text(x+174,y+32,"RAM files",0x152536);gfx_line(x+164,y+26,x+164,y+w->height-8,0x90a4ba);
        for(pm_u32 i=0;i<gui->file_count;i++) {
            int selected=gui->selected==(int)i;
            if(selected)gfx_fill_rect(x+8,y+58+(int)i*16,152,16,0x2e79c4);
            gui_text_line(x+10,y+58+(int)i*16,gui->file_names[i],18,selected?0xffffff:0x152536);
        }
        if(!gui->file_count)gfx_text(x+10,y+58,"No files.",0x152536);
        if(gui->selected>=0 && (pm_u32)gui->selected<gui->file_count) {
            pm_u32 slot=gui->file_slots[gui->selected];
            gui_text_line(x+174,y+56,ram_files[slot].name,31,0x152536);
            gui_text_block(x+174,y+80,ram_files[slot].content,31,15,0x152536);
        } else gfx_text(x+174,y+80,"Select a file",0x50637a);
    } else if(app==4)browser_draw(w);
    else if(app==GUI_TERMINAL) {
        pm_u32 rows=(pm_u32)(w->height-48)/16,first=gui->line_count>rows?gui->line_count-rows:0;
        for(pm_u32 i=first;i<gui->line_count;i++)gfx_text(x+8,y+28+(int)(i-first)*16,gui->lines[i],0xffffff);
        char prompt[64]="OVICS> ";pm_u32 n=7,start=gui->input_length>TERM_COLS-8?gui->input_length-(TERM_COLS-8):0;
        for(pm_u32 i=start;i<gui->input_length && n<TERM_COLS;i++)prompt[n++]=gui->input[i];prompt[n]=0;
        gfx_text(x+8,y+w->height-20,prompt,0x88eebb);
    } else if(app==GUI_ABOUT) {
        gui_text_block(x+12,y+36,"OVICS\nOpen World Interface Computer System\n\nVersion: 0.2.2 Experimental\nArchitecture: i386 32-bit\nKernel: OVICS Custom Kernel\nGraphics: 640x480 32-bit",44,12,0x152536);
    }
}
static void desktop_background(void) {
    gfx_clear(0x183a4b);gfx_text(12,12,"OVICS Desktop 0.2.2 Experimental",0xffffff);
    gfx_text(12,426,"Icons: double click | Esc: CUI",0xb9d6e3);
    for(pm_u32 i=0;i<4;i++) {
        int y=64+(int)i*84;pm_u32 color=i==0?0xe5bd57:i==1?0x122035:i==2?0x2e79c4:0x315a73;
        if(gui_click.selected_icon==i+1)gfx_fill_rect(12,y,80,68,0x2e79c4);
        gfx_fill_rect(30,y,40,36,color);gfx_rect(30,y,40,36,0xe8f0f5);
        gfx_text(42,y+10,i==0?"E":i==1?">":i==2?"i":"H",i==0?0x152536:0xffffff);
        gfx_text(20,y+44,gui_app_name(i+1),0xffffff);
    }
}
static void desktop_overlay(void) {
    gfx_fill_rect(0,448,640,32,0x263849);gfx_line(0,448,639,448,0x90a4ba);
    gfx_fill_rect(4,452,74,24,gui->menu_open?0x2e79c4:0x50637a);gfx_text(14,456,"OVICS",0xffffff);
    pm_u32 at=0,total=0;for(pm_u32 i=0;i<4;i++)if(gui->windows[i])total++;
    int stride=total>3?136:178;
    for(pm_u32 i=0;i<4;i++)if(gui->windows[i]) {
        int x=86+(int)at++*stride;
        gfx_fill_rect(x,452,stride-4,24,wm->active==gui->windows[i]?0x2e79c4:0x50637a);
        gfx_text(x+8,456,gui_app_name(i+1),0xffffff);
    }
    if(gui->menu_open) {
        gfx_fill_rect(4,316,192,128,0xe6edf3);gfx_rect(4,316,192,128,0x90a4ba);gfx_text(16,324,"Horizon",0x152536);
        for(pm_u32 i=0;i<4;i++)gfx_text(16,348+(int)i*24,i==3?"Close Menu":i==2?"About OVICS":gui_app_name(i+1),0x152536);
    }
}
static void desktop_start(void) {
    if(!mouse_state.ready){print("Mouse unavailable: ");print(mouse_error);print("\n");return;}
    if(!gfx_enter()){print("Graphics unavailable: ");print(gfx_error);print("\n");return;}
    wm_init();gui_reset();desktop_active=1;mouse_demo_active=0;wm_session_active=1;
    struct mouse_event e;while(mouse_next_event(&e)){}
    wm->last_sequence=mouse_state.sequence;wm->queue_dropped=mouse_state.dropped;
    wm_draw();cursor_state.enabled=1;cursor_draw();
}
static void gui_print_info(void) {
    desktop_sync();print("Desktop Status: ");print(desktop_active?"Active\n":"Inactive\n");
    print("Window Count: ");print_number(wm->count);print("\nActive Application: ");print(gui_app_name(gui_app_for_window(wm->active)));
    print("\nGraphics Mode: 640x480 32-bit\nMouse Status: ");print(mouse_state.ready?"Ready\n":"Unavailable\n");
    print("Icon activation: double click (500 ms, 4 px)\nSessions: ");print_number(gui->sessions);print("\n");
}
