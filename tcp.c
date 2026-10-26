/* Single active-open, stop-and-wait TCP. Sequence arithmetic is modulo 2^32. */
#define TCP_MSS 536U
#define TCP_RX_SIZE 4096U
#define FIN 1U
#define SYN 2U
#define RST 4U
#define PSH 8U
#define ACK 16U
enum { TCP_CLOSED, TCP_SYN_SENT, TCP_ESTABLISHED, TCP_FIN_WAIT_1,
       TCP_FIN_WAIT_2, TCP_CLOSING, TCP_TIME_WAIT, TCP_CLOSE_WAIT, TCP_LAST_ACK };
static unsigned int tcp_state, tcp_remote_ip, tcp_snd_una, tcp_snd_nxt, tcp_rcv_nxt;
static unsigned short tcp_local_port, tcp_remote_port, tcp_peer_window, tcp_peer_mss=536;
static unsigned char tcp_mac[6], tcp_rx[TCP_RX_SIZE], tcp_pending_data[TCP_MSS];
static unsigned int tcp_rx_head,tcp_rx_count,tcp_pending,tcp_pending_seq,tcp_pending_length;
static unsigned int tcp_pending_flags,tcp_pending_end,tcp_attempts,tcp_deadline,tcp_close_deadline;
static unsigned int tcp_sent_segments,tcp_received_segments,tcp_sent_bytes,tcp_received_bytes;
static unsigned int tcp_retransmissions,tcp_rejected,tcp_duplicates,tcp_resets,tcp_timeouts;
static unsigned int tcp_connection_serial;
static unsigned int tcp_window_seq,tcp_window_ack;
static unsigned short tcp_ip_id;
static const char *tcp_error="None";
static int seq_before(unsigned int a,unsigned int b){return (int)(a-b)<0;}
static int deadline_passed(unsigned int now,unsigned int deadline){return (int)(now-deadline)>=0;}
static const char *tcp_state_name(void)
{
    static const char *const names[]={"CLOSED","SYN_SENT","ESTABLISHED","FIN_WAIT_1","FIN_WAIT_2",
                         "CLOSING","TIME_WAIT","CLOSE_WAIT","LAST_ACK"};
    return tcp_state<9?names[tcp_state]:"INVALID";
}
static void tcp_fail(const char *reason)
{tcp_error=reason;tcp_state=TCP_CLOSED;tcp_pending=0;}
static void tcp_reject(void){tcp_rejected++;protocol_rejected++;}
static unsigned short tcp_checksum(unsigned int source,unsigned int destination,
                                    const unsigned char *p,unsigned int length)
{
    unsigned char bytes[1512];set32(bytes,source);set32(bytes+4,destination);
    bytes[8]=0;bytes[9]=6;set16(bytes+10,length);
    for(unsigned int i=0;i<length;i++)bytes[12+i]=p[i];
    return checksum(bytes,length+12);
}
static int tcp_emit(unsigned int sequence,unsigned int flags,const unsigned char *data,unsigned int length)
{
    unsigned int header=(flags&SYN)?24:20;
    unsigned char f[14+20+24+TCP_MSS]={0};ethernet_header(f,tcp_mac,0x800);
    unsigned char *ip=f+14,*t=ip+20;
    ip[0]=0x45;set16(ip+2,20+header+length);set16(ip+4,++tcp_ip_id);set16(ip+6,0x4000);
    ip[8]=64;ip[9]=6;set32(ip+12,LOCAL_IP);set32(ip+16,tcp_remote_ip);set16(ip+10,checksum(ip,20));
    set16(t,tcp_local_port);set16(t+2,tcp_remote_port);set32(t+4,sequence);
    if(flags&ACK)set32(t+8,tcp_rcv_nxt);
    t[12]=(unsigned char)((header/4)<<4);t[13]=(unsigned char)flags;
    set16(t+14,TCP_RX_SIZE-tcp_rx_count);
    if(flags&SYN){t[20]=2;t[21]=4;set16(t+22,TCP_MSS);}
    for(unsigned int i=0;i<length;i++)t[header+i]=data[i];
    set16(t+16,tcp_checksum(LOCAL_IP,tcp_remote_ip,t,header+length));
    if(!network_transmit(f,34+header+length)){tcp_fail("Network transmit failed.");return 0;}
    tcp_sent_segments++;return 1;
}
static void tcp_ack(void){tcp_emit(tcp_snd_nxt,ACK,0,0);}
static int tcp_queue(unsigned int flags,const unsigned char *data,unsigned int length)
{
    if(tcp_pending||length>TCP_MSS)return 0;
    tcp_pending_seq=tcp_snd_nxt;tcp_pending_length=length;tcp_pending_flags=flags;
    tcp_pending_end=tcp_snd_nxt+length+((flags&SYN)?1:0)+((flags&FIN)?1:0);
    for(unsigned int i=0;i<length;i++)tcp_pending_data[i]=data[i];
    tcp_pending=1;tcp_attempts=1;tcp_snd_nxt=tcp_pending_end;tcp_deadline=clock_now()+1000;
    if(!tcp_emit(tcp_pending_seq,flags,tcp_pending_data,length))return 0;
    tcp_sent_bytes+=length;return 1;
}
static void tcp_tick(void)
{
    if(tcp_state==TCP_CLOSED)return;
    unsigned int now=clock_now();
    if(!network_ready){tcp_fail("Network unavailable.");return;}
    if(tcp_state==TCP_TIME_WAIT){
        if(deadline_passed(now,tcp_close_deadline))tcp_state=TCP_CLOSED;
        return;
    }
    if(tcp_pending&&deadline_passed(now,tcp_deadline)){
        if(tcp_attempts>=3){tcp_timeouts++;tcp_fail("Retransmission timeout.");return;}
        tcp_attempts++;tcp_retransmissions++;
        tcp_deadline=now+(1000U<<(tcp_attempts-1));
        tcp_emit(tcp_pending_seq,tcp_pending_flags,tcp_pending_data,tcp_pending_length);
    }
    if((tcp_state==TCP_FIN_WAIT_2||tcp_state==TCP_CLOSING)&&deadline_passed(now,tcp_close_deadline)){
        tcp_timeouts++;tcp_fail("Close timeout.");
    }
}
static int tcp_options(const unsigned char *t,unsigned int header,unsigned short *mss)
{
    for(unsigned int pos=20;pos<header;){
        unsigned int kind=t[pos++];if(!kind)return 1;if(kind==1)continue;
        if(pos>=header)return 0;unsigned int size=t[pos++];
        if(size<2||size-2>header-pos)return 0;
        if(kind==2){if(size!=4||!(t[13]&SYN))return 0;
            unsigned int value=be16(t+pos);if(!value)return 0;*mss=(unsigned short)value;}
        /* Unknown well-formed options are ignored; no WS/SACK/timestamps negotiated. */
        pos+=size-2;
    }
    return 1;
}
static void tcp_time_wait(void){tcp_state=TCP_TIME_WAIT;tcp_close_deadline=clock_now()+2000;}
static void tcp_receive_packet(unsigned int source,unsigned int destination,const unsigned char *t,unsigned int length)
{
    if(length<20||length>1500){tcp_reject();return;}
    unsigned int header=(t[12]>>4)*4;
    unsigned short mss=tcp_peer_mss;
    if(header<20||header>length||(t[12]&15)||(t[13]&0xE0)||tcp_checksum(source,destination,t,length)||
       !be16(t)||!be16(t+2)||!tcp_options(t,header,&mss)){tcp_reject();return;}
    if(tcp_state==TCP_CLOSED||source!=tcp_remote_ip||be16(t)!=tcp_remote_port||be16(t+2)!=tcp_local_port)return;
    tcp_received_segments++;
    unsigned int sequence=be32(t+4),acknowledgment=be32(t+8),flags=t[13],size=length-header;
    const unsigned char *payload=t+header;
    if((flags&RST)&&((flags&(SYN|FIN|PSH))||size)){tcp_reject();return;}
    if(tcp_state==TCP_SYN_SENT){
        if((flags&RST)&&(flags&ACK)&&acknowledgment==tcp_snd_nxt){tcp_resets++;tcp_fail("Connection reset by peer.");return;}
        if(flags!=(SYN|ACK)||size||acknowledgment!=tcp_snd_nxt){tcp_reject();return;}
        tcp_snd_una=acknowledgment;tcp_pending=0;tcp_rcv_nxt=sequence+1;tcp_peer_window=be16(t+14);
        tcp_peer_mss=mss;tcp_window_seq=sequence;tcp_window_ack=acknowledgment;
        tcp_state=TCP_ESTABLISHED;tcp_ack();return;
    }
    /* A reset must be exactly at RCV.NXT; in-window guesses get a challenge ACK. */
    if(flags&RST){
        if(sequence!=tcp_rcv_nxt){tcp_reject();tcp_ack();return;}
        tcp_resets++;tcp_fail("Connection reset by peer.");return;
    }
    if(flags&SYN){
        /* Retransmitted SYN/ACK when our final handshake ACK was lost. */
        if(flags==(SYN|ACK)&&!size&&sequence+1==tcp_rcv_nxt&&acknowledgment==tcp_snd_una){tcp_ack();return;}
        tcp_reject();tcp_ack();return;
    }
    if(!(flags&ACK)){tcp_reject();return;}
    unsigned int end=sequence+size+((flags&FIN)?1:0);
    if(tcp_state==TCP_TIME_WAIT){
        if((flags&FIN)&&end==tcp_rcv_nxt){tcp_ack();tcp_close_deadline=clock_now()+2000;}
        return;
    }
    if(sequence!=tcp_rcv_nxt){
        /* Trim already received bytes, but still process a new ACK on a duplicate
         * FIN/data segment. Otherwise loss of the final close ACK can deadlock. */
        if(seq_before(sequence,tcp_rcv_nxt)&&(size||(flags&FIN))){
            unsigned int skip=tcp_rcv_nxt-sequence;
            if(!seq_before(tcp_rcv_nxt,end)){
                tcp_duplicates++;size=0;flags&=~FIN;tcp_ack();
            }else if(skip<=size){
                tcp_duplicates++;payload+=skip;size-=skip;
            }else{tcp_reject();tcp_ack();return;}
            sequence=tcp_rcv_nxt;
        }else{tcp_reject();tcp_ack();return;}
    }
    if(size>TCP_RX_SIZE-tcp_rx_count || ((flags&FIN)&&tcp_state!=TCP_ESTABLISHED&&
       tcp_state!=TCP_FIN_WAIT_1&&tcp_state!=TCP_FIN_WAIT_2)) {tcp_reject();tcp_ack();return;}
    if(seq_before(tcp_snd_nxt,acknowledgment)){tcp_reject();tcp_ack();return;}
    if(!seq_before(acknowledgment,tcp_snd_una)){
        tcp_snd_una=acknowledgment;
        if(tcp_pending&&acknowledgment==tcp_pending_end){
            tcp_pending=0;
            if(tcp_state==TCP_FIN_WAIT_1){tcp_state=TCP_FIN_WAIT_2;tcp_close_deadline=clock_now()+5000;}
            else if(tcp_state==TCP_CLOSING)tcp_time_wait();
            else if(tcp_state==TCP_LAST_ACK){tcp_state=TCP_CLOSED;return;}
        }
        if(seq_before(tcp_window_seq,sequence)||(tcp_window_seq==sequence&&!seq_before(acknowledgment,tcp_window_ack))){
            tcp_peer_window=be16(t+14);tcp_window_seq=sequence;tcp_window_ack=acknowledgment;
        }
    }
    if(size&&tcp_state!=TCP_ESTABLISHED&&tcp_state!=TCP_FIN_WAIT_1&&tcp_state!=TCP_FIN_WAIT_2){tcp_reject();tcp_ack();return;}
    if(size>TCP_RX_SIZE-tcp_rx_count){tcp_reject();tcp_ack();return;}
    for(unsigned int i=0;i<size;i++)tcp_rx[(tcp_rx_head+tcp_rx_count+i)%TCP_RX_SIZE]=payload[i];
    tcp_rx_count+=size;tcp_received_bytes+=size;tcp_rcv_nxt+=size;
    if(flags&FIN){
        tcp_rcv_nxt++;
        if(tcp_state==TCP_ESTABLISHED)tcp_state=TCP_CLOSE_WAIT;
        else if(tcp_state==TCP_FIN_WAIT_1){tcp_state=TCP_CLOSING;tcp_close_deadline=clock_now()+5000;}
        else if(tcp_state==TCP_FIN_WAIT_2)tcp_time_wait();
        else{tcp_reject();tcp_ack();return;}
    }
    if(size||(flags&FIN))tcp_ack();
}
static void tcp_connect_command(const char *argument)
{
    const char *s=skip_spaces(argument);char address[16];unsigned int n=0,ip;unsigned short port;
    while(*s&&*s!=' '){if(n>=15)goto usage;address[n++]=*s++;}address[n]=0;
    if(!parse_ip(address,&ip)||!read_port(&s,&port)||*skip_spaces(s))goto usage;
    if(tcp_state!=TCP_CLOSED){print("TCP connection already active. Use tcpclose.\n");return;}
    if(!network_ready){print("TCP unavailable: ");print(network_error);print("\n");return;}
    const unsigned char *mac=resolve_route(ip);
    if(!mac){print("TCP connect failed: ARP timeout.\n");return;}
    for(int i=0;i<6;i++)tcp_mac[i]=mac[i];
    tcp_remote_ip=ip;tcp_remote_port=port;tcp_connection_serial++;
    tcp_local_port=(unsigned short)(40000+tcp_connection_serial%10000);
    /* Teaching implementation: time + per-boot serial; not cryptographic ISN. */
    tcp_snd_una=0x4F560000U+clock_now()*250U+tcp_connection_serial*65537U;tcp_snd_nxt=tcp_snd_una;
    tcp_rcv_nxt=0;tcp_rx_head=tcp_rx_count=0;tcp_pending=0;tcp_peer_mss=536;tcp_peer_window=0;
    tcp_error="None";tcp_state=TCP_SYN_SENT;
    print("TCP connecting to ");print_ip(ip);print(":");print_number(port);print("\n");
    if(!tcp_queue(SYN,0,0)){print("TCP connect failed: send error.\n");return;}
    while(tcp_state==TCP_SYN_SENT)network_poll();
    if(tcp_state==TCP_ESTABLISHED)print("TCP connected (3-way handshake complete).\n");
    else{print("TCP connect failed: ");print(tcp_error);print("\n");}
    return;
usage:print("Usage: tcpconnect IP PORT\n");
}
static void tcp_send_command(const char *argument)
{
    const char *s=skip_spaces(argument);unsigned int length=0;while(s[length])length++;
    if(!length){print("Usage: tcpsend MESSAGE\n");return;}
    if(tcp_state!=TCP_ESTABLISHED&&tcp_state!=TCP_CLOSE_WAIT){print("TCP is not connected.\n");return;}
    if(tcp_pending){print("TCP send already pending.\n");return;}
    if(length>TCP_MSS||length>tcp_peer_mss){print("TCP message exceeds peer MSS.\n");return;}
    unsigned int start=clock_now();
    while(tcp_state!=TCP_CLOSED&&length>tcp_peer_window&&(unsigned int)(clock_now()-start)<3000)network_poll();
    if(tcp_state==TCP_CLOSED){print("TCP send failed: ");print(tcp_error);print("\n");return;}
    if(length>tcp_peer_window){print("TCP send timed out: peer window too small.\n");return;}
    if(!tcp_queue(PSH|ACK,(const unsigned char *)s,length)){print("TCP send failed.\n");return;}
    while(tcp_pending&&tcp_state!=TCP_CLOSED)network_poll();
    if(tcp_state!=TCP_CLOSED){print("TCP data acknowledged: ");print_number(length);print(" bytes\n");}
    else{print("TCP send failed: ");print(tcp_error);print("\n");}
}
static void tcp_receive_command(void)
{
    unsigned int start=clock_now();
    while(!tcp_rx_count&&tcp_state!=TCP_CLOSED&&tcp_state!=TCP_CLOSE_WAIT&&
          (unsigned int)(clock_now()-start)<3000)network_poll();
    if(!tcp_rx_count){print(tcp_state==TCP_CLOSED||tcp_state==TCP_CLOSE_WAIT?"No TCP data (connection closed for receive).\n":"TCP receive timed out.\n");return;}
    unsigned int count=tcp_rx_count;print("TCP received: ");print_number(count);print(" bytes\n");
    for(unsigned int i=0;i<count;i++){
        unsigned char c=tcp_rx[(tcp_rx_head+i)%TCP_RX_SIZE];putchar(c>=32&&c<=126?(char)c:'.');
    }
    print("\n");tcp_rx_head=(tcp_rx_head+count)%TCP_RX_SIZE;tcp_rx_count=0;
    if(tcp_state!=TCP_CLOSED&&tcp_state!=TCP_TIME_WAIT)tcp_ack(); /* Advertise reopened window. */
}
static void tcp_close_command(void)
{
    if(tcp_state==TCP_CLOSED){print("TCP is already closed.\n");return;}
    if(tcp_state!=TCP_ESTABLISHED&&tcp_state!=TCP_CLOSE_WAIT){print("TCP close already in progress.\n");return;}
    if(tcp_pending){print("TCP data still pending.\n");return;}
    tcp_state=tcp_state==TCP_CLOSE_WAIT?TCP_LAST_ACK:TCP_FIN_WAIT_1;
    if(!tcp_queue(FIN|ACK,0,0)){print("TCP close failed: send error.\n");return;}
    while(tcp_state!=TCP_CLOSED)network_poll();
    if(strcmp(tcp_error,"None")==0)print("TCP closed normally (FIN exchange complete).\n");
    else{print("TCP close failed: ");print(tcp_error);print("\n");}
}
static void tcp_statistics(void)
{
    network_poll();print("TCP state: ");print(tcp_state_name());print("\nPeer: ");print_ip(tcp_remote_ip);
    print(":");print_number(tcp_remote_port);print("  Local port: ");print_number(tcp_local_port);
    print("\nTX segments: ");print_number(tcp_sent_segments);print("  RX segments: ");print_number(tcp_received_segments);
    print("\nTX data bytes: ");print_number(tcp_sent_bytes);print("  RX data bytes: ");print_number(tcp_received_bytes);
    print("\nRetransmissions: ");print_number(tcp_retransmissions);print("  Duplicates: ");print_number(tcp_duplicates);
    print("\nRejected: ");print_number(tcp_rejected);print("  Resets: ");print_number(tcp_resets);
    print("\nTimeouts: ");print_number(tcp_timeouts);print("\nReceive window: ");print_number(TCP_RX_SIZE-tcp_rx_count);
    print("  Peer window: ");print_number(tcp_peer_window);print("\nSND.UNA: ");print_hex(tcp_snd_una,8);
    print("  SND.NXT: ");print_hex(tcp_snd_nxt,8);print("  RCV.NXT: ");print_hex(tcp_rcv_nxt,8);
    print("\nLast error: ");print(tcp_error);print("\n");
}
