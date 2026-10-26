/* Cursor compositor is separate from PS/2 decoding and event ownership.
   All graphics API writes hide/restore the saved background transactionally. */
static const char *cursor_shape[18]={
 "#...........","##..........","#W#.........","#WW#........","#WWW#.......","#WWWW#......",
 "#WWWWW#.....","#WWWWWW#....","#WWWWWWW#...","#WWWW#####..","#WW#W#......","#W#.W#......",
 "##..#W#.....","#...#W#.....",".....#W#....",".....#W#....","......##....","............"};
struct cursor_status { pm_u32 enabled,visible,x,y,width,height,draws,restores,depth; };
static struct cursor_status cursor_state={0,0,0,0,12,18,0,0,0};
static pm_u32 cursor_background[12*18];
static void cursor_hide(void) {
    if(!cursor_state.visible)return;
    for(pm_u32 y=0;y<18 && cursor_state.y+y<GFX_HEIGHT;y++)for(pm_u32 x=0;x<12 && cursor_state.x+x<GFX_WIDTH;x++)
        gfx_fb[(cursor_state.y+y)*GFX_WIDTH+cursor_state.x+x]=cursor_background[y*12+x];
    cursor_state.visible=0;cursor_state.restores++;
}
static void cursor_draw(void) {
    if(!cursor_state.enabled || !gfx_stats.active || cursor_state.visible || cursor_state.depth)return;
    mouse_get_position(&cursor_state.x,&cursor_state.y);
    for(pm_u32 y=0;y<18 && cursor_state.y+y<GFX_HEIGHT;y++)for(pm_u32 x=0;x<12 && cursor_state.x+x<GFX_WIDTH;x++) {
        pm_u32 i=(cursor_state.y+y)*GFX_WIDTH+cursor_state.x+x;
        cursor_background[y*12+x]=gfx_fb[i];char c=cursor_shape[y][x];
        if(c=='#')gfx_fb[i]=0;if(c=='W')gfx_fb[i]=0xffffff;
    }
    cursor_state.visible=1;cursor_state.draws++;
}
static void cursor_graphics_begin(void){if(!cursor_state.depth)cursor_hide();cursor_state.depth++;}
static void cursor_graphics_end(void){if(cursor_state.depth)cursor_state.depth--;if(!cursor_state.depth)cursor_draw();}
static void cursor_mode_leave(void){cursor_hide();cursor_state.enabled=0;cursor_state.depth=0;}
static void cursor_update(void) {
    if(cursor_state.enabled && (!cursor_state.visible || cursor_state.x!=mouse_state.x || cursor_state.y!=mouse_state.y)) {
        cursor_hide();cursor_draw();
    }
}
