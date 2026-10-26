/* Classic DNS over UDP, 512-byte messages, IN/A with bounded CNAME following. */
static unsigned short dns_identifier=0x4F56;
static unsigned int dns_queries,dns_answers,dns_rejected,dns_result;
static int dns_status; /* 0 pending, 1 A found, 2 no A, 3 server error */
static unsigned int dns_rcode;
static char dns_question[254];
static char lower_ascii(char c){return c>='A'&&c<='Z'?(char)(c+'a'-'A'):c;}
static int dns_character(unsigned char c)
{return(c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_';}

/* Cursor advances only over bytes at the original location, not pointer targets.
 * Every visited offset is marked: label/pointer cycles terminate safely. */
static int dns_name(const unsigned char *data,unsigned int length,unsigned int *cursor,char *name)
{
    unsigned char visited[512]={0};unsigned int pos=*cursor,out=0;int jumped=0;
    for(unsigned int steps=0;steps<512;steps++){
        if(pos>=length||visited[pos])return 0;visited[pos]=1;
        unsigned int label=data[pos++];
        if(!label){if(!jumped)*cursor=pos;name[out]=0;return 1;}
        if((label&0xC0)==0xC0){
            if(pos>=length)return 0;
            unsigned int target=((label&63)<<8)|data[pos++];
            if(target<12||target>=pos-2||target>=length)return 0;
            if(!jumped)*cursor=pos;jumped=1;pos=target;continue;
        }
        if(label&0xC0||label>63||label>length-pos)return 0;
        if(out){if(out>=253)return 0;name[out++]='.';}
        if(label>253-out)return 0;
        for(unsigned int i=0;i<label;i++){
            unsigned char c=data[pos++];if(!dns_character(c))return 0;name[out++]=lower_ascii((char)c);
        }
    }
    return 0;
}
struct dns_record { char owner[254],target[254];unsigned int address;unsigned short type; };
static int dns_parse(const unsigned char *d,unsigned int length)
{
    if(length<12||length>512||be16(d)!=dns_identifier)return 0;
    unsigned int flags=be16(d+2),answers=be16(d+6),authority=be16(d+8),additional=be16(d+10);
    if(!(flags&0x8000)||(flags&0x7800)||(flags&0x0200)||(flags&0x0040)||be16(d+4)!=1||
       answers+authority+additional>64)return 0;
    unsigned int cursor=12;char name[254];
    if(!dns_name(d,length,&cursor,name)||strcmp(name,dns_question)||cursor+4>length||
       be16(d+cursor)!=1||be16(d+cursor+2)!=1)return 0;
    cursor+=4;
    struct dns_record records[8];unsigned int count=0;
    for(unsigned int i=0;i<answers+authority+additional;i++){
        if(!dns_name(d,length,&cursor,name)||cursor+10>length)return 0;
        unsigned int type=be16(d+cursor),class=be16(d+cursor+2),size=be16(d+cursor+8);cursor+=10;
        if(size>length-cursor)return 0;
        char target[254];target[0]=0;
        if(type==1&&class==1&&size!=4)return 0;
        if(type==5&&class==1){unsigned int end=cursor;
            if(!dns_name(d,length,&end,target)||end!=cursor+size||!target[0])return 0;
        }
        if(i<answers&&class==1&&(type==1||type==5)){
            if(count>=8)return 0;
            struct dns_record *r=&records[count++];r->type=(unsigned short)type;
            for(unsigned int j=0;j<254;j++){r->owner[j]=name[j];if(!name[j])break;}
            if(type==1)r->address=be32(d+cursor);
            else for(unsigned int j=0;j<254;j++){r->target[j]=target[j];if(!target[j])break;}
        }
        cursor+=size;
    }
    if(cursor!=length)return 0;
    dns_rcode=flags&15;
    if(dns_rcode){dns_status=3;return 1;}
    for(unsigned int j=0;j<254;j++){name[j]=dns_question[j];if(!name[j])break;}
    char visited_names[8][254];unsigned int visited_count=0;
    for(unsigned int step=0;step<9;step++){
        int alias=-1;
        for(unsigned int i=0;i<count;i++)if(!strcmp(records[i].owner,name)){
            if(records[i].type==1){dns_result=records[i].address;dns_status=1;return 1;}
            if(alias>=0&&strcmp(records[alias].target,records[i].target))return 0;
            alias=(int)i;
        }
        if(alias<0){dns_status=2;return 1;}
        if(visited_count>=8)return 0;
        for(unsigned int i=0;i<visited_count;i++)if(!strcmp(visited_names[i],name))return 0;
        for(unsigned int j=0;j<254;j++){visited_names[visited_count][j]=name[j];if(!name[j])break;}
        visited_count++;
        for(unsigned int j=0;j<254;j++){name[j]=records[alias].target[j];if(!name[j])break;}
    }
    return 0;
}
static void dns_receive(const unsigned char *data,unsigned int length)
{
    if(dns_status)return;
    if(!dns_parse(data,length)){dns_rejected++;protocol_rejected++;return;}
    dns_answers++;
}
static void dns_command(const char *argument)
{
    const char *s=skip_spaces(argument);unsigned int n=0;
    while(*s&&*s!=' '){if(n>=253)goto usage;dns_question[n++]=lower_ascii(*s++);}
    if(*skip_spaces(s)||!n)goto usage;
    if(dns_question[n-1]=='.')n--;
    if(!n)goto usage;dns_question[n]=0;
    unsigned char query[512]={0};unsigned int cursor=12,label=0;
    for(unsigned int i=0;i<=n;i++){
        if(i==n||dns_question[i]=='.'){
            unsigned int size=i-label;if(!size||size>63)goto usage;
            query[cursor++]=(unsigned char)size;
            for(unsigned int j=label;j<i;j++)query[cursor++]=(unsigned char)dns_question[j];
            label=i+1;
        }else if(!dns_character((unsigned char)dns_question[i]))goto usage;
    }
    query[cursor++]=0;set16(query+cursor,1);set16(query+cursor+2,1);cursor+=4;
    if(!network_ready){print("DNS unavailable: ");print(network_error);print("\n");return;}
    dns_identifier++;dns_port=(unsigned short)(50000+(dns_identifier%10000));dns_status=0;dns_result=0;
    set16(query,dns_identifier);set16(query+2,0x0100);set16(query+4,1);
    print("DNS server: ");print_ip(DNS_SERVER);print("\nQuery: ");print(dns_question);print("\n");
    /* Set waiting before transmission so an immediate response is not lost. */
    dns_waiting=1;
    if(!udp_transmit(DNS_SERVER,dns_port,53,query,cursor)){
        dns_waiting=0;print("DNS send failed: ARP timeout or network error.\n");return;
    }
    dns_queries++;unsigned int start=clock_now();
    while(network_ready&&!dns_status&&(unsigned int)(clock_now()-start)<3000)network_poll();
    dns_waiting=0;
    if(dns_status==1){print("Address: ");print_ip(dns_result);print("\n");}
    else if(dns_status==2)print("DNS response: no IPv4 A record.\n");
    else if(dns_status==3){print("DNS server error: RCODE ");print_number(dns_rcode);print("\n");}
    else print(network_ready?"DNS query timed out.\n":"DNS failed: network unavailable.\n");
    return;
usage:print("Usage: nslookup DOMAIN (labels 1-63 characters)\n");
}
