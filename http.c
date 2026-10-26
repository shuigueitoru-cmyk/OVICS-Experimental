/* Bounded HTTP/1.1 GET client. Raw ASCII HTML/text, no rendering or TLS. */
#define HTTP_HEADER_LIMIT 2048U
#define HTTP_BODY_LIMIT 8192U
static char http_host[254],http_path[256],http_request[768],http_location[256];
static unsigned char http_header[HTTP_HEADER_LIMIT+1],http_body[HTTP_BODY_LIMIT+1];
static unsigned int http_status,http_header_length,http_body_length,http_expected_length,http_received;
static unsigned int http_port,http_request_length;
static int http_explicit_port,http_headers_ready,http_complete,http_text_type,http_owned;
static int http_gui_request;
static const char *http_error;
static const char *http_result="Not run";
static int http_ci(const char *a,const char *b,unsigned int length)
{for(unsigned int i=0;i<length;i++)if(lower_ascii(a[i])!=lower_ascii(b[i]))return 0;return 1;}
static unsigned int http_length(const char *s){unsigned int n=0;while(s[n])n++;return n;}
static int http_equal(const char *a,const char *b)
{unsigned int n=http_length(a);return n==http_length(b)&&http_ci(a,b,n);}
static int http_hex(char c)
{return(c>='0'&&c<='9')||(lower_ascii(c)>='a'&&lower_ascii(c)<='f');}
static int http_url(const char *argument)
{
    const char *s=skip_spaces(argument);unsigned int n=http_length(s);
    if(n<7||!http_ci(s,"http://",7)){http_error="Only http:// URLs are supported (no HTTPS).";return 0;}
    if(n>247){http_error="URL exceeds input limit.";return 0;}
    for(unsigned int i=0;i<n;i++)if((unsigned char)s[i]<=32||(unsigned char)s[i]>=127||s[i]=='#'||s[i]=='\\'){
        http_error="Invalid URL character or unsupported fragment.";return 0;
    }
    s+=7;n=0;
    while(*s&&*s!=':'&&*s!='/'&&*s!='?'){
        char c=lower_ascii(*s++);if(n>=253||!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'||c=='.'))goto invalid;
        http_host[n++]=c;
    }
    if(!n)goto invalid;http_host[n]=0;
    unsigned int label=0;int numeric=1;
    for(unsigned int i=0;i<=n;i++){
        char c=http_host[i];if(c>='a'&&c<='z')numeric=0;if(c=='-')numeric=0;
        if(!c||c=='.'){
            if(!label||label>63||http_host[i-label]=='-'||http_host[i-1]=='-')goto invalid;
            label=0;
        }else label++;
    }
    if(numeric){unsigned int ip;if(!parse_ip(http_host,&ip))goto invalid;}
    http_port=80;http_explicit_port=0;
    if(*s==':'){
        s++;unsigned int value=0,digits=0;
        while(*s>='0'&&*s<='9'){value=value*10+(unsigned int)(*s++-'0');if(++digits>5||value>65535)goto invalid;}
        if(!digits||!value)goto invalid;http_port=value;http_explicit_port=1;
    }
    if(*s&&*s!='/'&&*s!='?')goto invalid;
    unsigned int p=0;if(!*s||*s=='?')http_path[p++]='/';
    while(*s){if(p>=255)goto invalid;
        if(*s=='%'&&(!s[1]||!s[2]||!http_hex(s[1])||!http_hex(s[2])))goto invalid;
        http_path[p++]=*s++;
    }
    http_path[p]=0;return 1;
invalid:http_error="Invalid HTTP URL, host, port or path.";return 0;
}
static int http_append(const char *s)
{
    while(*s){if(http_request_length>=sizeof(http_request)-1)return 0;http_request[http_request_length++]=*s++;}
    http_request[http_request_length]=0;return 1;
}
static void http_number(char *p,unsigned int value)
{
    char reverse[10];unsigned int n=0;do{reverse[n++]=(char)('0'+value%10);value/=10;}while(value);
    for(unsigned int i=0;i<n;i++)p[i]=reverse[n-i-1];p[n]=0;
}
static int http_make_request(void)
{
    http_request_length=0;
    if(!http_append("GET ")||!http_append(http_path)||!http_append(" HTTP/1.1\r\nHost: ")||!http_append(http_host))return 0;
    if(http_explicit_port){char port[6];http_number(port,http_port);if(!http_append(":")||!http_append(port))return 0;}
    return http_append("\r\nUser-Agent: OVICS/0.2.2-Experimental\r\nAccept: text/html, text/plain\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n");
}
static int http_token(char c)
{
    return(c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||
          c=='!'||c=='#'||c=='$'||c=='%'||c=='&'||c==39||c=='*'||c=='+'||c=='-'||c=='.'||
          c=='^'||c=='_'||c=='`'||c=='|'||c=='~';
}
static int http_parse_headers(void)
{
    char *h=(char *)http_header;unsigned int end=0;
    while(end+1<http_header_length&&!(h[end]=='\r'&&h[end+1]=='\n'))end++;
    if(end<13||end+1>=http_header_length||(!http_ci(h,"HTTP/1.1 ",9)&&!http_ci(h,"HTTP/1.0 ",9))||
       h[9]<'1'||h[9]>'5'||h[10]<'0'||h[10]>'9'||h[11]<'0'||h[11]>'9'||h[12]!=' ')goto malformed;
    http_status=(unsigned int)(h[9]-'0')*100+(unsigned int)(h[10]-'0')*10+(unsigned int)(h[11]-'0');
    unsigned int cursor=end+2;int have_length=0,have_type=0;
    while(cursor<http_header_length-2){
        unsigned int start=cursor;while(cursor+1<http_header_length&&!(h[cursor]=='\r'&&h[cursor+1]=='\n'))cursor++;
        if(cursor+1>=http_header_length)goto malformed;
        unsigned int colon=start;while(colon<cursor&&h[colon]!=':'){if(!http_token(h[colon]))goto malformed;colon++;}
        if(colon==start||colon==cursor)goto malformed;
        h[colon]=0;unsigned int value=colon+1;while(value<cursor&&(h[value]==' '||h[value]=='\t'))value++;
        unsigned int last=cursor;while(last>value&&(h[last-1]==' '||h[last-1]=='\t'))last--;
        h[last]=0;
        if(http_equal(h+start,"content-length")){
            if(have_length||value==last){http_error="Invalid or duplicate Content-Length.";return 0;}
            unsigned int number=0;
            for(unsigned int i=value;i<last;i++){
                if(h[i]<'0'||h[i]>'9'){http_error="Invalid Content-Length.";return 0;}
                unsigned int digit=(unsigned int)(h[i]-'0');
                if(number>(HTTP_BODY_LIMIT-digit)/10){http_error="HTTP body exceeds 8192-byte limit.";return 0;}
                number=number*10+digit;
            }
            http_expected_length=number;have_length=1;
        }else if(http_equal(h+start,"transfer-encoding")){
            http_error="Transfer-Encoding unsupported (including chunked).";return 0;
        }else if(http_equal(h+start,"content-encoding")){
            if(!http_equal(h+value,"identity")){http_error="Compressed Content-Encoding unsupported.";return 0;}
        }else if(http_equal(h+start,"content-type")){
            if(have_type)goto malformed;have_type=1;
            unsigned int type_end=value;while(type_end<last&&h[type_end]!=';'&&h[type_end]!=' ')type_end++;
            char saved=h[type_end];h[type_end]=0;
            http_text_type=http_equal(h+value,"text/html")||http_equal(h+value,"text/plain");h[type_end]=saved;
        }else if(http_equal(h+start,"location")){
            unsigned int n=last-value;if(n>=sizeof(http_location))n=sizeof(http_location)-1;
            for(unsigned int i=0;i<n;i++)http_location[i]=h[value+i]=='\t'?' ':h[value+i];http_location[n]=0;
        }
        cursor+=2;
    }
    if(http_status<200){http_error="Informational HTTP responses unsupported.";return 0;}
    if(http_status>=300&&http_status<400){http_error="HTTP redirect unsupported; not followed.";return 0;}
    if(http_status==204){if(have_length){http_error="Content-Length not allowed for HTTP 204.";return 0;}http_expected_length=0;}
    else if(!have_length){http_error="Response without Content-Length unsupported.";return 0;}
    http_headers_ready=1;if(!http_expected_length)http_complete=1;return 1;
malformed:http_error="Malformed HTTP status line or headers.";return 0;
}
static int http_feed(unsigned char c)
{
    http_received++;
    if(!http_headers_ready){
        if(http_header_length>=HTTP_HEADER_LIMIT){http_error="HTTP headers exceed 2048-byte limit.";return 0;}
        if(c!=13&&c!=10&&c!=9&&(c<32||c>126)){http_error="Invalid HTTP header byte.";return 0;}
        /* Bare LF, CR not followed by LF, and folded header lines are rejected. */
        if((c==10&&(!http_header_length||http_header[http_header_length-1]!=13))||
           (http_header_length&&http_header[http_header_length-1]==13&&c!=10)){
            http_error="Malformed HTTP line ending.";return 0;
        }
        http_header[http_header_length++]=c;http_header[http_header_length]=0;
        unsigned int n=http_header_length;
        if(n>=4&&http_header[n-4]==13&&http_header[n-3]==10&&http_header[n-2]==13&&http_header[n-1]==10)return http_parse_headers();
        return 1;
    }
    if(http_body_length>=http_expected_length){http_error="HTTP body exceeds Content-Length.";return 0;}
    http_body[http_body_length++]=c;http_body[http_body_length]=0;
    if(http_body_length==http_expected_length)http_complete=1;
    return 1;
}
static int http_drain(void)
{
    unsigned int count=tcp_rx_count;
    for(unsigned int i=0;i<count;i++)if(!http_feed(tcp_rx[(tcp_rx_head+i)%TCP_RX_SIZE]))return 0;
    tcp_rx_head=(tcp_rx_head+count)%TCP_RX_SIZE;tcp_rx_count-=count;
    if(count&&tcp_state!=TCP_CLOSED&&tcp_state!=TCP_TIME_WAIT)tcp_ack();
    return 1;
}
static void http_abort(void)
{
    if(!http_owned)return;
    if(tcp_state!=TCP_CLOSED&&network_ready)tcp_emit(tcp_snd_nxt,RST|ACK,0,0);
    tcp_fail("HTTP operation aborted.");http_owned=0;
}
static int http_write(void)
{
    unsigned int position=0;
    while(position<http_request_length){
        unsigned int start=clock_now();
        while(!tcp_peer_window&&tcp_state==TCP_ESTABLISHED&&(unsigned int)(clock_now()-start)<3000){
            network_poll();if(!http_drain())return 0;
        }
        if(tcp_state!=TCP_ESTABLISHED||!tcp_peer_window){http_error="HTTP request send failed or window timed out.";return 0;}
        unsigned int count=http_request_length-position;
        if(count>TCP_MSS)count=TCP_MSS;if(count>tcp_peer_mss)count=tcp_peer_mss;if(count>tcp_peer_window)count=tcp_peer_window;
        if(!tcp_queue(PSH|ACK,(const unsigned char *)http_request+position,count)){http_error="HTTP request send failed.";return 0;}
        while(tcp_pending&&tcp_state!=TCP_CLOSED){network_poll();if(!http_drain())return 0;}
        if(tcp_state==TCP_CLOSED){http_error="TCP failed while sending HTTP request.";return 0;}
        position+=count;
    }
    return 1;
}
static int http_finish(void)
{
    if(tcp_state!=TCP_ESTABLISHED&&tcp_state!=TCP_CLOSE_WAIT){http_error="TCP closed unexpectedly.";return 0;}
    tcp_state=tcp_state==TCP_CLOSE_WAIT?TCP_LAST_ACK:TCP_FIN_WAIT_1;
    if(!tcp_queue(FIN|ACK,0,0)){http_error="HTTP TCP close failed.";return 0;}
    while(tcp_state!=TCP_CLOSED){network_poll();if(!http_drain())return 0;}
    http_owned=0;
    if(strcmp(tcp_error,"None")){http_error="HTTP TCP close failed or timed out.";return 0;}
    return 1;
}
static void http_reset_result(void)
{
    http_status=http_header_length=http_body_length=http_expected_length=http_received=0;
    http_headers_ready=http_complete=http_text_type=http_owned=0;http_error=0;
    http_location[0]=0;http_body[0]=0;http_result="Pending";
}
static void http_input_overflow(void)
{
    http_reset_result();http_result="HTTP command exceeds 255 characters; not executed.";
    print(http_result);print("\n");
}
static void http_perform(const char *argument)
{
    http_reset_result();
    if(!http_url(argument)||!http_make_request())goto failed;
    if(tcp_state!=TCP_CLOSED){http_error="TCP connection already active; use tcpclose first.";goto failed;}
    if(!network_ready){http_error="HTTP unavailable: RTL8139 not ready.";goto failed;}
    unsigned int ip;
    if(!parse_ip(http_host,&ip)){
        dns_status=0;dns_result=0;dns_command(http_host);
        if(dns_status!=1||!unicast_ip(dns_result)||dns_result==LOCAL_IP){http_error="HTTP DNS resolution failed.";goto failed;}
        ip=dns_result;
    }
    char connection[23],part[6];unsigned int at=0;
    for(int i=3;i>=0;i--){http_number(part,(ip>>(i*8))&255);for(unsigned int j=0;part[j];j++)connection[at++]=part[j];if(i)connection[at++]='.';}
    connection[at++]=' ';http_number(part,http_port);for(unsigned int j=0;part[j];j++)connection[at++]=part[j];connection[at]=0;
    tcp_connect_command(connection);
    if(tcp_state!=TCP_ESTABLISHED){http_error="HTTP TCP connection failed.";goto failed;}
    http_owned=1;
    if(!http_write())goto failed;
    unsigned int idle=clock_now(),total=idle;
    while(!http_complete){
        network_poll();unsigned int old=http_received;if(!http_drain())goto failed;
        unsigned int now=clock_now();if(http_received!=old)idle=now;
        if(http_complete)break;
        if(tcp_state==TCP_CLOSED||tcp_state==TCP_CLOSE_WAIT){http_error="Truncated HTTP response or TCP error.";goto failed;}
        if((unsigned int)(now-idle)>=(http_gui_request?20000u:3000u)||(unsigned int)(now-total)>=(http_gui_request?30000u:15000u)){http_error="HTTP receive timed out.";goto failed;}
    }
    if(!http_finish())goto failed;
    if(http_gui_request){http_result=http_status>=400?"HTTP error status (body received)":"Success";return;}
    print("HTTP status: ");print_number(http_status);print("\nBody bytes: ");print_number(http_body_length);print("\n");
    if(http_status==204){http_result="Success (no content)";return;}
    int printable=http_text_type;
    for(unsigned int i=0;i<http_body_length;i++)if(http_body[i]!=10&&http_body[i]!=13&&http_body[i]!=9&&(http_body[i]<32||http_body[i]>126))printable=0;
    if(!printable){http_result="Body received; binary or unsupported text not displayed.";print(http_result);print("\n");return;}
    http_result=http_status>=400?"HTTP error status (body received)":"Success";
    print("--- HTTP body ---\n");
    for(unsigned int i=0;i<http_body_length;i++){
        unsigned char c=http_body[i];if(c==13)continue;if(c==9)putchar(' ');else putchar((char)c);
    }
    print("\n--- End HTTP body ---\n");return;
failed:
    if(!http_error)http_error="HTTP request exceeds buffer limit.";
    http_result=http_error;http_abort();print("HTTP failed: ");print(http_result);print("\n");
    if(http_location[0]){print("Location: ");print(http_location);print("\n");}
}
/* Explicit byte-result API: callers never scrape console diagnostics. Body is
   owned by HTTP until the next request and copied immediately by Horizon. */
struct http_response {const unsigned char *body;unsigned int length,status;int complete,text;const char *error;};
static void http_discard(char c){(void)c;}
static void http_get_native(const char *url,struct http_response *out) {
 void (*old)(char)=console_sink;console_sink=http_discard;http_gui_request=1;
 http_perform(url);http_gui_request=0;console_sink=old;
 out->body=http_body;out->length=http_body_length;out->status=http_status;
 out->complete=http_complete;out->text=http_text_type;out->error=http_error;
}
static void http_get_command(const char *argument){http_perform(argument);}
static void http_information(void)
{
    print("HTTP status: ");print_number(http_status);print("\nReceived bytes: ");print_number(http_received);
    print("\nHeader bytes: ");print_number(http_header_length);print("\nBody bytes: ");print_number(http_body_length);
    print("\nResult: ");print(http_result);print("\nTCP state: ");print(tcp_state_name());print("\n");
}
