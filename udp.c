/* IPv4 UDP, single synchronous receive socket; MTU 1500, no fragmentation. */
#define UDP_DATA_LIMIT 1472U
static unsigned int udp_sent, udp_received, udp_rejected;
static unsigned int udp_source_ip, udp_data_length;
static unsigned short udp_source_port, udp_listen_port;
static unsigned char udp_data[UDP_DATA_LIMIT];
static int udp_waiting, udp_available;
static unsigned short udp_ip_identifier;
static int dns_waiting;
static unsigned short dns_port;
static void dns_receive(const unsigned char *data, unsigned int length);

static unsigned short udp_checksum(unsigned int source, unsigned int destination,
                                   const unsigned char *packet, unsigned int length)
{
    unsigned char bytes[12+UDP_DATA_LIMIT+8];
    set32(bytes,source);set32(bytes+4,destination);bytes[8]=0;bytes[9]=17;set16(bytes+10,length);
    for(unsigned int i=0;i<length;i++)bytes[12+i]=packet[i];
    return checksum(bytes,length+12);
}
static const unsigned char *resolve_route(unsigned int destination)
{
    unsigned int next=(destination&NETMASK)==(LOCAL_IP&NETMASK)?destination:GATEWAY;
    static const unsigned char broadcast[6]={255,255,255,255,255,255};
    for(int retry=0;retry<3&&network_ready&&!arp_lookup(next);retry++){
        if(!arp_send(next,broadcast,0))return 0;
        unsigned int start=clock_now();
        while(network_ready&&!arp_lookup(next)&&(unsigned int)(clock_now()-start)<500)network_poll();
    }
    return network_ready?arp_lookup(next):0;
}
static int udp_transmit(unsigned int destination, unsigned short source_port,
                        unsigned short destination_port,const unsigned char *data,unsigned int length)
{
    if(!network_ready||!destination_port||length>UDP_DATA_LIMIT)return 0;
    const unsigned char *mac=resolve_route(destination);if(!mac)return 0;
    unsigned char f[1514]={0};ethernet_header(f,mac,0x800);
    unsigned char *ip=f+14,*udp=ip+20;
    ip[0]=0x45;set16(ip+2,28+length);set16(ip+4,++udp_ip_identifier);set16(ip+6,0x4000);
    ip[8]=64;ip[9]=17;set32(ip+12,LOCAL_IP);set32(ip+16,destination);set16(ip+10,checksum(ip,20));
    set16(udp,source_port);set16(udp+2,destination_port);set16(udp+4,8+length);
    for(unsigned int i=0;i<length;i++)udp[8+i]=data[i];
    unsigned short check=udp_checksum(LOCAL_IP,destination,udp,length+8);set16(udp+6,check?check:0xFFFF);
    if(!network_transmit(f,length+42))return 0;
    udp_sent++;return 1;
}
static void udp_receive_packet(unsigned int source,unsigned int destination,
                               const unsigned char *p,unsigned int length)
{
    /* Require IP payload and UDP length to agree; ignore Ethernet padding. */
    if(length<8||length>UDP_DATA_LIMIT+8||be16(p+4)!=length||!be16(p+2)||
       (be16(p+6)&&udp_checksum(source,destination,p,length))){udp_rejected++;protocol_rejected++;return;}
    udp_received++;
    unsigned short port=be16(p+2);
    if(dns_waiting&&port==dns_port){
        if(source!=DNS_SERVER||be16(p)!=53){udp_rejected++;protocol_rejected++;return;}
        dns_receive(p+8,length-8);return;
    }
    if(!udp_waiting||udp_available||port!=udp_listen_port)return;
    udp_source_ip=source;udp_source_port=be16(p);udp_data_length=length-8;
    for(unsigned int i=0;i<udp_data_length;i++)udp_data[i]=p[8+i];
    udp_available=1;
}
static const char *skip_spaces(const char *s){while(*s==' ')s++;return s;}
static int read_port(const char **text,unsigned short *result)
{
    const char *s=skip_spaces(*text);unsigned int value=0,digits=0;
    while(*s>='0'&&*s<='9'){value=value*10+(unsigned int)(*s++-'0');if(++digits>5||value>65535)return 0;}
    if(!digits||!value||(*s&&*s!=' '))return 0;
    *text=s;*result=(unsigned short)value;return 1;
}
static void udp_send_command(const char *argument)
{
    char address[16];unsigned int n=0,destination;const char *s=skip_spaces(argument);
    while(*s&&*s!=' '){if(n>=15)goto usage;address[n++]=*s++;}address[n]=0;
    if(!parse_ip(address,&destination))goto usage;
    unsigned short port;if(!read_port(&s,&port))goto usage;
    s=skip_spaces(s);if(!*s)goto usage;
    unsigned int length=0;while(s[length])length++;
    if(!network_ready){print("UDP unavailable: ");print(network_error);print("\n");return;}
    if(!udp_transmit(destination,49152,port,(const unsigned char *)s,length)){
        print("UDP send failed: ARP timeout or network error.\n");return;}
    print("UDP sent: ");print_number(length);print(" bytes, source port 49152 -> ");print_ip(destination);
    print(":");print_number(port);print(" (hardware completion)\n");return;
usage:print("Usage: udpsend IP PORT MESSAGE\n");
}
static void udp_receive_command(const char *argument)
{
    const char *s=argument;unsigned short port;
    if(!read_port(&s,&port)||*skip_spaces(s)){print("Usage: udprecv PORT (1-65535)\n");return;}
    if(!network_ready){print("UDP unavailable: ");print(network_error);print("\n");return;}
    udp_listen_port=port;udp_available=0;udp_waiting=1;
    print("Waiting for UDP port ");print_number(port);print(" (3 seconds)\n");
    unsigned int start=clock_now();
    while(network_ready&&!udp_available&&(unsigned int)(clock_now()-start)<3000)network_poll();
    udp_waiting=0;
    if(!udp_available){print(network_ready?"UDP receive timed out.\n":"UDP receive failed: network unavailable.\n");return;}
    print("UDP from ");print_ip(udp_source_ip);print(":");print_number(udp_source_port);
    print(" -> port ");print_number(port);print(", ");print_number(udp_data_length);print(" bytes\n");
    for(unsigned int i=0;i<udp_data_length;i++)putchar(udp_data[i]>=32&&udp_data[i]<=126?(char)udp_data[i]:'.');
    print("\n");
}
