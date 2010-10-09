/**
 *
 * Phantom OS
 *
 * Copyright (C) 2005-2010 Dmitry Zavalishin, dz@dz.ru
 *
 * Very basic resolver.
 *
**/

#include <config.h>
#include <kernel/stats.h>

#define DEBUG_MSG_PREFIX "dns"
#include "debug_ext.h"
#define debug_level_flow 10
#define debug_level_error 10
#define debug_level_info 10


#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/cdefs.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "resolve.h"

#include <endian.h>
#include <arpa/inet.h>
#include "udp.h"



// TODO resolver eats memory, see malloc

static void ChangetoDnsNameFormat (unsigned char*,unsigned char*);
static unsigned char* ReadName (unsigned char*,unsigned char*,int*);

ipv4_addr  ngethostbyname (unsigned char*);





errno_t dns_request(unsigned char *host, ipv4_addr server, ipv4_addr *result)
{
    unsigned char buf[65536], *qname, *reader;
    int i, j, stop;

    struct RES_RECORD answers[20], auth[20], addit[20]; //the replies from the DNS server

    struct DNS_HEADER *dns = NULL;
    struct QUESTION *qinfo = NULL;

#ifdef KERNEL
    void *s;
    int res = udp_open(&s);
    if( res )
    {
        printf("Can't get socket");
        return ENOTSOCK;
    }
#else
    int s = socket(AF_INET , SOCK_DGRAM , IPPROTO_UDP); //UDP packet for DNS queries
#endif
    printf("got sock\n");

#ifdef KERNEL
    sockaddr addr;
    addr.port = 53;
    addr.addr.len = 4;
    addr.addr.type = ADDR_TYPE_IP;
    NETADDR_TO_IPV4(addr.addr) = server;
#else
    struct sockaddr_in dest;
    dest.sin_family = AF_INET;
    dest.sin_port = htons(53);
    dest.sin_addr.s_addr = inet_addr(dns_servers[0]); //dns servers
#endif

    memset(buf, 0, sizeof(buf));

    //Set the DNS structure to standard queries
    dns = (struct DNS_HEADER *)&buf;

#ifdef KERNEL
    dns->id = (unsigned short) random();
#else
    dns->id = (unsigned short) htons(getpid());
#endif
    dns->qr = 0; //This is a query
    dns->opcode = 0; //This is a standard query
    dns->aa = 0; //Not Authoritative
    dns->tc = 0; //This message is not truncated
    dns->rd = 1; //Recursion Desired
    dns->ra = 0; //Recursion not available! hey we dont have it (lol)
    dns->z = 0;
    dns->ad = 0;
    dns->cd = 0;
    dns->rcode = 0;
    dns->q_count = htons(1); //we have only 1 question
    dns->ans_count = 0;
    dns->auth_count = 0;
    dns->add_count = 0;

    //point to the query portion
    qname =(unsigned char*)&buf[sizeof(struct DNS_HEADER)];
    ChangetoDnsNameFormat(qname , host);
    int qname_len = (strlen((const char*)qname) + 1);

    qinfo =(struct QUESTION*)&buf[sizeof(struct DNS_HEADER) + qname_len]; //fill it

    qinfo->qtype = htons(1); //we are requesting the ipv4 address
    qinfo->qclass = htons(1); //its internet (lol)

    STAT_INC_CNT(STAT_CNT_DNS_REQ);

    printf("Sending Packet...\n");
    int sendLen = sizeof(struct DNS_HEADER) + qname_len + sizeof(struct QUESTION);
#ifdef KERNEL
    int ret = udp_sendto( s, buf, sendLen, &addr);
    if( ret != sendLen )
#else
    if(sendto(s,(char*)buf, sendLen, 0, (struct sockaddr*)&dest, sizeof(dest)) != sendLen)
#endif
    {
        printf("Error sending DNS req, %d\n", ret);
    }
    printf("Sent\n");

    printf("nReceiving answer...\n");
#ifdef KERNEL
    long tmo = 1000L*1000L*2; // 2 sec
    //long tmo = 1000L*10;
    if( udp_recvfrom( s, buf, sizeof(buf),
                     &addr,
                     SOCK_FLAG_TIMEOUT, tmo) <= 0 )
#else
    i = sizeof dest;
    ret = recvfrom (s,(char*)buf,65536,0,(struct sockaddr*)&dest,&i);
    if(ret == 0)
#endif
    {
        printf("Failed.\n");
    }
    printf("Received.\n");

    STAT_INC_CNT(STAT_CNT_DNS_ANS);

    dns = (struct DNS_HEADER*) buf;

    //move ahead of the dns header and the query field
    reader = &buf[sizeof(struct DNS_HEADER) + (strlen((const char*)qname)+1) + sizeof(struct QUESTION)];


    if(debug_level_flow > 6)
    {
        printf("The response contains : \n");
        printf("%d Questions.\n",ntohs(dns->q_count));
        printf("%d Answers.\n",ntohs(dns->ans_count));
        printf("%d Authoritative Servers.\n",ntohs(dns->auth_count));
        printf("%d Additional records.\n\n",ntohs(dns->add_count));
    }

    //reading answers
    stop=0;

    for(i=0;i<ntohs(dns->ans_count);i++)
    {
        answers[i].name=ReadName(reader,buf,&stop);
        reader = reader + stop;

        answers[i].resource = (struct R_DATA*)(reader);
        reader = reader + sizeof(struct R_DATA);

        if(ntohs(answers[i].resource->type) == 1) //if its an ipv4 address
        {
            answers[i].rdata = (unsigned char*)malloc(ntohs(answers[i].resource->data_len));

            for(j=0 ; j<ntohs(answers[i].resource->data_len) ; j++)
                answers[i].rdata[j]=reader[j];

            answers[i].rdata[ntohs(answers[i].resource->data_len)] = '?';

            reader = reader + ntohs(answers[i].resource->data_len);
        }
        else
        {
            answers[i].rdata = ReadName(reader,buf,&stop);
            reader = reader + stop;
        }
    }

    //read authorities
    for(i=0;i<ntohs(dns->auth_count);i++)
    {
        auth[i].name=ReadName(reader,buf,&stop);
        reader+=stop;

        auth[i].resource=(struct R_DATA*)(reader);
        reader+=sizeof(struct R_DATA);

        auth[i].rdata=ReadName(reader,buf,&stop);
        reader+=stop;
    }

    //read additional
    for(i=0;i<ntohs(dns->add_count);i++)
    {
        addit[i].name=ReadName(reader,buf,&stop);
        reader+=stop;

        addit[i].resource=(struct R_DATA*)(reader);
        reader+=sizeof(struct R_DATA);

        if(ntohs(addit[i].resource->type)==1)
        {
            addit[i].rdata = (unsigned char*)malloc(ntohs(addit[i].resource->data_len));
            for(j=0;j<ntohs(addit[i].resource->data_len);j++)
                addit[i].rdata[j]=reader[j];

            addit[i].rdata[ntohs(addit[i].resource->data_len)]='?';
            reader+=ntohs(addit[i].resource->data_len);
        }
        else
        {
            addit[i].rdata=ReadName(reader,buf,&stop);
            reader+=stop;
        }
    }

    if(debug_level_flow > 6)
    {

        //print answers
        for(i=0;i<ntohs(dns->ans_count);i++)
        {
            printf("Name : %s ",answers[i].name);

            if(ntohs(answers[i].resource->type)==1) //IPv4 address
            {

                long *p;
                p=(long*)answers[i].rdata;

                struct sockaddr_in a;

                a.sin_addr.s_addr=(*p); //working without ntohl
                printf("has IPv4 address : %s",inet_ntoa(a.sin_addr));
            }
            if(ntohs(answers[i].resource->type)==5) //Canonical name for an alias
                printf("has alias name : %s",answers[i].rdata);

            printf("\n");
        }

        //print authorities
        for(i=0;i<ntohs(dns->auth_count);i++)
        {
            printf("Name : %s ",auth[i].name);
            if(ntohs(auth[i].resource->type)==2)
                printf("has authoritative nameserver : %s",auth[i].rdata);
            printf("\n");
        }

        //print additional resource records
        for(i=0;i<ntohs(dns->add_count);i++)
        {
            printf("Name : %s ",addit[i].name);
            if(ntohs(addit[i].resource->type)==1)
            {
                long *p;
                p=(long*)addit[i].rdata;
                struct sockaddr_in a;
                a.sin_addr.s_addr=(*p); //working without ntohl
                printf("has IPv4 address : %s",inet_ntoa(a.sin_addr));
            }
            printf("\n");
        }

    }

    // return answer
    for( i=0; i < ntohs(dns->ans_count); i++ )
    {
        if( ntohs(answers[i].resource->type) == 1 ) //IPv4 address
        {
            *result = *( (long*)answers[i].rdata);
            return 0;
        }
    }

    // No direct answer
    *result = 0;

    return ENOENT;
}

static unsigned char* ReadName( unsigned char* reader, unsigned char* buffer, int* count )
{
    unsigned char *name;
    unsigned int p=0,jumped=0,offset;
    int i , j;

    *count = 1;
    name = (unsigned char*)malloc(256);

    name[0]='?';

    //read the names in 3www6google3com format
    while(*reader!=0)
    {
        if(*reader>=192)
        {
            offset = (*reader)*256 + *(reader+1) - 49152; //49152 = 11000000 00000000 
            reader = buffer + offset - 1;
            jumped = 1; //we have jumped to another location so counting wont go up!
        }
        else
            name[p++]=*reader;

        reader=reader+1;

        if(!jumped)
            //*count = *count + 1; //if we havent jumped to another location then we can count up
            (*count)++;
    }

    name[p]='?'; //string complete
    if(jumped)
        //*count = *count + 1; //number of steps we actually moved forward in the packet
        (*count)++;

    //now convert 3www6google3com0 to www.google.com
    for( i=0; i < (int)strlen((const char*)name); i++ )
    {
        p=name[i];
        for(j=0;j<(int)p;j++) {
            name[i]=name[i+1];
            i=i+1;
        }
        name[i]='.';
    }
    name[i-1]='?'; //remove the last dot
    return name;
}


//this will convert www.google.com to 3www6google3com ;got it <img src="http://www.binarytides.com/blog/wp-includes/images/smilies/icon_smile.gif" alt=":)" class="wp-smiley">
static void ChangetoDnsNameFormat(unsigned char* dns, unsigned char* _host)
{
    unsigned char copy[128];
    strlcpy( (char*)copy, (const char*)_host, 127 );
    strcat( (char*)copy, "..");
    unsigned char* host = copy;

    printf("format: '%s'\n", host );


    int lock = 0 , i;
    for(i = 0 ; i < (int)strlen((char*)host) ; i++)
    {
        if(host[i]=='.')
        {
            *dns++=i-lock;

            for( ; lock < i; lock++)
            {
                *dns++=host[lock];
            }
            lock++; //or lock=i+1;
        }
    }

    *dns++='?';
}






#define MAX_DNS_SERVERS 32

static ipv4_addr servers[MAX_DNS_SERVERS] =
{
    IPV4_DOTADDR_TO_ADDR(8, 8, 8, 8),
    IPV4_DOTADDR_TO_ADDR(8, 8, 4, 4),
    IPV4_DOTADDR_TO_ADDR(192, 168, 0, 1),
    IPV4_DOTADDR_TO_ADDR(192, 168, 1, 1),
};


ipv4_addr ngethostbyname(unsigned char *host)
{
    int tries = 20;

    ipv4_addr 	result;
    //ipv4_addr 	next_servers[MAX_DNS_SERVERS];

    ipv4_addr *	sptr = servers;
    int         sleft = MAX_DNS_SERVERS;

    while(tries--)
    {

        ipv4_addr 	server = *sptr++;

        if(sleft-- <= 0 || server == 0)
        {
            printf("No more places to look in, give up\n");
            return 0;
        }


        printf("look in %s\n", inet_ntoa(* (struct in_addr*)&server) );
        errno_t res = dns_request(host, server, &result );

        if( res == 0 || result != 0 )
        {
            printf("DNS andwer is %s\n", inet_ntoa(* (struct in_addr*)&result) );
            return result;
        }
    }

    return 0;

}






