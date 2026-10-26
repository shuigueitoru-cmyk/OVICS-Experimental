/* Minimal IPv4 stack. All multi-byte wire fields are explicitly big endian. */
struct arp_entry { unsigned int ip; unsigned char mac[6]; };
static struct arp_entry arp_cache[8];
static unsigned int arp_cursor, arp_requests, arp_replies, protocol_rejected;
static unsigned int ipv4_received, icmp_received, ping_sent, ping_received;
static unsigned int ping_target, ping_sequence, ping_identifier = 0x4F56;
static int ping_waiting, ping_answered;
static unsigned int clock_milliseconds;

/* PIT channel 2 one-shot, ~50 ms, speaker disabled. No interrupt/IDT needed.
 * Poll continuously during waits; delayed polls extend deadlines, never shorten them. */
static void clock_start(void)
{
    unsigned char gate = inb(0x61);
    outb(0x61, gate & ~3U); outb(0x43, 0xB0);
    outb(0x42, 0x0B); outb(0x42, 0xE9); /* 59659 / 1193182 Hz */
    outb(0x61, (gate & ~2U) | 1U);
}
/* IRQ time is authoritative. PIT2 only bounds legacy network waits if IRQ0
   stops: no fabricated time is returned by the public uptime API. */
static pm_u64 clock_last_irq_ms;
static pm_u64 clock_watchdog_tick;
static unsigned int clock_now(void)
{
    pm_u64 current=timer_get_uptime_ms();
    if(current>=clock_last_irq_ms && current!=clock_last_irq_ms) {
        clock_milliseconds+=(pm_u32)(current-clock_last_irq_ms);clock_last_irq_ms=current;

    }
    if(inb(0x61)&0x20) {
        clock_start();
        if(current==clock_watchdog_tick){timer_watchdog_fault=1;clock_milliseconds+=50;}
        clock_watchdog_tick=current;
    }
    ps2_poll();if(wait_service)wait_service();return clock_milliseconds;
}
static unsigned short be16(const unsigned char *p) { return (unsigned short)((p[0]<<8)|p[1]); }
static unsigned int be32(const unsigned char *p) { return ((unsigned int)be16(p)<<16)|be16(p+2); }
static void set16(unsigned char *p, unsigned int n) { p[0]=(unsigned char)(n>>8);p[1]=(unsigned char)n; }
static void set32(unsigned char *p, unsigned int n) { set16(p,n>>16);set16(p+2,n); }
static int same(const unsigned char *a, const unsigned char *b, unsigned int n)
{ for (unsigned int i=0;i<n;i++) if(a[i]!=b[i]) return 0; return 1; }
static unsigned short checksum(const unsigned char *p, unsigned int n)
{
    unsigned int sum=0;
    while(n>=2) { sum+=be16(p);p+=2;n-=2; }
    if(n) sum+=(unsigned int)*p<<8;
    while(sum>>16) sum=(sum&0xFFFF)+(sum>>16);
    return (unsigned short)~sum;
}
static int unicast_ip(unsigned int ip)
{ return (ip>>24)!=0 && (ip>>24)!=127 && (ip>>24)<224 && (ip&255)!=255 && (ip&255)!=0; }
static void print_ip(unsigned int ip)
{ for(int i=3;i>=0;i--) {print_number((ip>>(i*8))&255);if(i)print(".");} }
static void ethernet_header(unsigned char *p, const unsigned char *mac, unsigned int type)
{ for(int i=0;i<6;i++){p[i]=mac[i];p[6+i]=network_mac[i];}set16(p+12,type); }
static const unsigned char *arp_lookup(unsigned int ip)
{ for(int i=0;i<8;i++)if(arp_cache[i].ip==ip)return arp_cache[i].mac;return 0; }
static void arp_remember(unsigned int ip,const unsigned char *mac)
{
    unsigned int slot=8;
    for(unsigned int i=0;i<8;i++)if(arp_cache[i].ip==ip){slot=i;break;}
    if(slot==8){slot=arp_cursor;arp_cursor=(arp_cursor+1)%8;}
    arp_cache[slot].ip=ip;
    for(int i=0;i<6;i++)arp_cache[slot].mac[i]=mac[i];
}
static int arp_send(unsigned int ip,const unsigned char *mac,int reply)
{
    unsigned char f[42]={0};ethernet_header(f,mac,0x806);
    set16(f+14,1);set16(f+16,0x800);f[18]=6;f[19]=4;set16(f+20,reply?2:1);
    for(int i=0;i<6;i++){f[22+i]=network_mac[i];if(reply)f[32+i]=mac[i];}
    set32(f+28,LOCAL_IP);set32(f+38,ip);
    int ok=network_transmit(f,sizeof(f));if(ok&&!reply)arp_requests++;return ok;
}
static void protocol_receive(const unsigned char *f,unsigned int length)
{
    static const unsigned char broadcast[6]={255,255,255,255,255,255};
    if(length<14)return;
    if(!same(f,network_mac,6)&&!same(f,broadcast,6))return;
    unsigned int type=be16(f+12);
    if(type==0x806){
        if(length<42){protocol_rejected++;return;}
        const unsigned char *a=f+14;unsigned int op=be16(a+6),sender=be32(a+14);
        if(be16(a)!=1||be16(a+2)!=0x800||a[4]!=6||a[5]!=4||
           (op!=1&&op!=2)||!same(f+6,a+8,6)||(a[8]&1)||
           !unicast_ip(sender)||sender==LOCAL_IP||(sender&NETMASK)!=(LOCAL_IP&NETMASK)||
           be32(a+24)!=LOCAL_IP || (op==2&&(!same(a+18,network_mac,6)||!same(f,network_mac,6)))){
            protocol_rejected++;return;
        }
        unsigned char zero[6]={0};if(same(a+8,zero,6)){protocol_rejected++;return;}
        arp_remember(sender,a+8);
        if(op==2)arp_replies++;else arp_send(sender,a+8,1);
        return;
    }
    if(type!=0x800)return;
    if(length<34){protocol_rejected++;return;}
    const unsigned char *ip=f+14;unsigned int h=(ip[0]&15)*4,total=be16(ip+2);
    if((ip[0]>>4)!=4||h<20||h>length-14||total<h||total>length-14||
       checksum(ip,h)||!ip[8]||(be16(ip+6)&0xBFFF)||be32(ip+16)!=LOCAL_IP||
       !unicast_ip(be32(ip+12))||!same(f,network_mac,6)){
        protocol_rejected++;return;
    }
    ipv4_received++;
    if(ip[9]==17){udp_receive_packet(be32(ip+12),be32(ip+16),ip+h,total-h);return;}
    if(ip[9]==6){tcp_receive_packet(be32(ip+12),be32(ip+16),ip+h,total-h);return;}
    if(ip[9]!=1)return;
    const unsigned char *icmp=ip+h;unsigned int size=total-h;
    if(size<8||checksum(icmp,size)){protocol_rejected++;return;}
    icmp_received++;
    if(icmp[0]!=0||icmp[1]!=0)return;
    if(!ping_waiting||ping_answered||be32(ip+12)!=ping_target||
       be16(icmp+4)!=ping_identifier||be16(icmp+6)!=ping_sequence||size!=40){protocol_rejected++;return;}
    for(unsigned int i=8;i<40;i++)if(icmp[i]!=(unsigned char)(i-8)){protocol_rejected++;return;}
    ping_answered=1;ping_received++;
}
static void ipconfig(void)
{
    print("IPv4: ");print_ip(LOCAL_IP);print("\nNetmask: ");print_ip(NETMASK);
    print("\nGateway: ");print_ip(GATEWAY);print("\nDNS: ");print_ip(DNS_SERVER);print("\nConfiguration: STATIC (explicit QEMU network)\n");
    print("Rejected packets: ");print_number(protocol_rejected);print("\n");
}
static void arp_display(void)
{
    int count=0;print("ARP cache\n");
    for(int i=0;i<8;i++)if(arp_cache[i].ip){count++;print_ip(arp_cache[i].ip);print("  ");
        for(int j=0;j<6;j++){if(j)print(":");print_hex(arp_cache[i].mac[j],2);}print("\n");}
    if(!count)print("No entries.\n");
}
static int parse_ip(const char *s,unsigned int *result)
{
    while(*s==' ')s++;
    unsigned int ip=0;
    for(int part=0;part<4;part++){
        unsigned int v=0,digits=0;
        while(*s>='0'&&*s<='9'){v=v*10+(unsigned int)(*s++-'0');if(++digits>3||v>255)return 0;}
        if(!digits)return 0;ip=(ip<<8)|v;
        if(part<3){if(*s++!='.')return 0;}
    }
    while(*s==' ')s++;
    if(*s||!unicast_ip(ip)||ip==LOCAL_IP)return 0;
    *result=ip;return 1;
}
static void ping_command(const char *argument)
{
    unsigned int target;
    if(!parse_ip(argument,&target)){print("Usage: ping IPv4-address (unicast, excluding local IP)\n");return;}
    if(!network_ready){print("Ping unavailable: ");print(network_error);print("\n");return;}
    ping_sent=ping_received=0;ping_target=target;ping_identifier++;
    print("PING ");print_ip(target);print(" (32 data bytes)\n");
    unsigned int next=(target&NETMASK)==(LOCAL_IP&NETMASK)?target:GATEWAY;
    static const unsigned char broadcast[6]={255,255,255,255,255,255};
    for(int retry=0;retry<3&&!arp_lookup(next);retry++){
        if(!arp_send(next,broadcast,0)){print("ARP send failed.\n");break;}
        unsigned int start=clock_now();
        while(network_ready&&!arp_lookup(next)&&(unsigned int)(clock_now()-start)<500)network_poll();
    }
    const unsigned char *mac=arp_lookup(next);
    if(!mac){print("ARP resolution timed out. Sent: 0 Received: 0 Lost: 0\n");return;}
    unsigned char destination[6];for(int i=0;i<6;i++)destination[i]=mac[i];
    for(unsigned int sequence=1;sequence<=3;sequence++){
        unsigned char f[74]={0};ethernet_header(f,destination,0x800);
        unsigned char *ip=f+14,*icmp=ip+20;
        ip[0]=0x45;set16(ip+2,60);set16(ip+4,sequence);set16(ip+6,0x4000);
        ip[8]=64;ip[9]=1;set32(ip+12,LOCAL_IP);set32(ip+16,target);set16(ip+10,checksum(ip,20));
        icmp[0]=8;set16(icmp+4,ping_identifier);set16(icmp+6,sequence);
        for(unsigned int i=0;i<32;i++)icmp[8+i]=(unsigned char)i;
        set16(icmp+2,checksum(icmp,40));
        ping_sequence=sequence;ping_answered=0;ping_waiting=1;
        unsigned int start=clock_now();
        if(!network_transmit(f,sizeof(f))){ping_waiting=0;print("Echo send failed.\n");break;}
        ping_sent++;
        while(network_ready&&!ping_answered&&(unsigned int)(clock_now()-start)<1000)network_poll();
        ping_waiting=0;
        if(ping_answered){print("Reply from ");print_ip(target);print(": seq=");print_number(sequence);print(" time=");
            unsigned int elapsed=clock_now()-start;if(!elapsed)print("<50");else print_number(elapsed);print(" ms\n");}
        else{print("Request timed out: seq=");print_number(sequence);print("\n");}
    }
    print("Sent: ");print_number(ping_sent);print(" Received: ");print_number(ping_received);
    print(" Lost: ");print_number(ping_sent-ping_received);print("\n");
}
