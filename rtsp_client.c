/*****************************************************************************
 * rtsp_client.c: RTSP Client
 *
 * Copyright (C) 2004 Shiro Ninomiya <shiron@snino.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA.
 *****************************************************************************/
#include <stdint.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ctype.h>
#include "aexcl_lib.h"
#include "rtsp_client.h"

#define MAX_NUM_KD 20
typedef struct rtspcl_data_t {
    int fd;
    char url[128];
    int cseq;
    key_data_t *kd;
    key_data_t *exthds;
    char *session;
    char *transport;
    uint16_t server_port;
    uint16_t control_port;
    uint16_t timing_port;
    struct in_addr host_addr;
    struct in_addr local_addr;
    
    const char *useragent;
} rtspcl_data_t;

unsigned int alatency=11025;

static int exec_request(rtspcl_data_t *rtspcld, char *cmd, char *content_type,
             char *content, int length, int get_response, key_data_t *hds, key_data_t **kd, char* url);

rtspcl_t *rtspcl_open()
{
    rtspcl_data_t *rtspcld;
    rtspcld=malloc(sizeof(rtspcl_data_t));
    memset(rtspcld, 0, sizeof(rtspcl_data_t));
    rtspcld->useragent="RTSPClient";
    return (rtspcl_t *)rtspcld;
}

int rtspcl_close(rtspcl_t *p)
{
    rtspcl_data_t *rtspcld;
    int ret;
    
    if(!p) return -1;
    rtspcld=(rtspcl_data_t *)p;
    if ( ( ret = rtspcl_teardown(p) ) != 0 )
    {
       ERRMSG( "Couldn't tear down streaming connection : %d\n", ret );
    }
    rtspcl_disconnect(p);
    free(rtspcld);
    p=NULL;
    rtspcl_remove_all_exthds(p);
    return 0;
}

uint16_t rtspcl_get_server_port(rtspcl_t *p)
{
    rtspcl_data_t *rtspcld;
    
    if(!p) return -1;
    rtspcld=(rtspcl_data_t *)p;
    return rtspcld->server_port;
}

uint16_t rtspcl_get_timing_port(rtspcl_t *p)
{
    rtspcl_data_t *rtspcld;
    
    if(!p) return -1;
    rtspcld=(rtspcl_data_t *)p;
    return rtspcld->timing_port;
}

uint16_t rtspcl_get_control_port(rtspcl_t *p)
{
    rtspcl_data_t *rtspcld;
    
    if(!p) return -1;
    rtspcld=(rtspcl_data_t *)p;
    return rtspcld->control_port;
}

int rtspcl_set_useragent(rtspcl_t *p, const char *name)
{
    rtspcl_data_t *rtspcld;
    
    if(!p) return -1;
    rtspcld=(rtspcl_data_t *)p;
    rtspcld->useragent=name;
    return 0;
}

int rtspcl_add_exthds(rtspcl_t *p, char *key, char *data)
{
    int i=0;
    rtspcl_data_t *rtspcld;
    
    if(!p) return -1;
    rtspcld=(rtspcl_data_t *)p;
    if(!rtspcld->exthds){
        if(realloc_memory((void*)&rtspcld->exthds, 17*sizeof(key_data_t),__func__)) return -1;
    }else{
        i=0;
        while(rtspcld->exthds[i].key) {
            if(rtspcld->exthds[i].key[0]==0xff) break;
            i++;
        }
        if(i && i%16==0 && rtspcld->exthds[i].key[0]!=0xff){
            if(realloc_memory((void*)&rtspcld->exthds,(16*((i%16)+1)+1)*sizeof(key_data_t),__func__))
                return -1;
            memset(rtspcld->exthds+16*(i/16),0,17*sizeof(key_data_t));
        }
    }
    if(realloc_memory((void*)&rtspcld->exthds[i].key,strlen(key),__func__)) return -1;
    strcpy((char*)rtspcld->exthds[i].key,key);
    if(realloc_memory((void*)&rtspcld->exthds[i].data,strlen(data),__func__)) return -1;
    strcpy((char*)rtspcld->exthds[i].data,data);
    rtspcld->exthds[i+1].key=NULL;
    return 0;
}

int rtspcl_mark_del_exthds(rtspcl_t *p, char *key)
{
    int i=0;
    rtspcl_data_t *rtspcld;
    
    if(!p) return -1;
    rtspcld=(rtspcl_data_t *)p;
    if(!rtspcld->exthds) return -1;
    while(rtspcld->exthds[i].key) {
        if(!strcmp((char*)key,(char*)rtspcld->exthds[i].key)){
            rtspcld->exthds[i].key[0]=0xff;
            return 0;
        }
        i++;
    }
    return -1;
}

int rtspcl_remove_all_exthds(rtspcl_t *p)
{
    int i=0;
    rtspcl_data_t *rtspcld;
    
    if(!p) return -1;
    rtspcld=(rtspcl_data_t *)p;
    while(rtspcld->exthds && rtspcld->exthds[i].key) {
        free(rtspcld->exthds[i].key);
        free(rtspcld->exthds[i].data);
    }
    free(rtspcld->exthds);
    rtspcld->exthds=NULL;
    return 0;
}

int rtspcl_connect(rtspcl_t *p, char *host, uint16_t destport, char *sid)
{
    uint16_t myport=0;
    struct sockaddr_in name;
    socklen_t namelen=sizeof(name);
    rtspcl_data_t *rtspcld;
    
    if(!p) return -1;
    rtspcld=(rtspcl_data_t *)p;
    rtspcld->session=NULL;
    if((rtspcld->fd=open_tcp_socket(NULL, &myport))==-1) return -1;
    if(get_tcp_connect_by_host(rtspcld->fd, host, destport)) return -1;
    getsockname(rtspcld->fd, (struct sockaddr*)&name, &namelen);
    memcpy(&rtspcld->local_addr,&name.sin_addr,sizeof(struct in_addr));
    sprintf(rtspcld->url,"rtsp://%s/%s",inet_ntoa(name.sin_addr),sid);
    getpeername(rtspcld->fd, (struct sockaddr*)&name, &namelen);
    memcpy(&rtspcld->host_addr,&name.sin_addr,sizeof(struct in_addr));
    return 0;
}

char* rtspcl_local_ip(rtspcl_t *p)
{
    rtspcl_data_t *rtspcld;
    
    if(!p) return NULL;
    rtspcld=(rtspcl_data_t *)p;
    return inet_ntoa(rtspcld->local_addr);
}
    

int rtspcl_disconnect(rtspcl_t *p)
{
    rtspcl_data_t *rtspcld;
    
    if(!p) return -1;
    rtspcld=(rtspcl_data_t *)p;
    if(rtspcld->fd>0) close(rtspcld->fd);
    rtspcld->fd=0;
    return 0;
}

int rtspcl_announce_sdp(rtspcl_t *p, char *sdp)
{
    rtspcl_data_t *rtspcld;
    
    if(!p) return -1;
    rtspcld=(rtspcl_data_t *)p;
    return exec_request(rtspcld, "ANNOUNCE", "application/sdp", sdp, 0, 1, NULL, &rtspcld->kd, NULL);
}

int rtspcl_setup(rtspcl_t *p, key_data_t **kd)
{
    key_data_t *rkd=NULL;
    key_data_t hds[2];
    const char delimiters[] = ";";
    char *buf=NULL;
    char *token,*pc;
    char *temp;
    int rval=-1;
    rtspcl_data_t *rtspcld;

    if(!p) return -1;
    rtspcld=(rtspcl_data_t *)p;
    hds[0].key=(uint8_t*)"Transport";
    hds[0].data=(uint8_t*)"RTP/AVP/UDP;unicast;interleaved=0-1;mode=record;control_port=6001;timing_port=6002";
    hds[1].key=NULL;
    if(exec_request(rtspcld, "SETUP", NULL, NULL, 0, 1, hds, &rkd, NULL)) return -1;
    if(!(temp=kd_lookup(rkd, "Session"))){
        ERRMSG("%s: no session in response\n",__func__);
        goto erexit;
    }
    DBGMSG("<------- : %s: session:%s\n",__func__,temp);
    rtspcld->session = (char *) malloc(100);
    sprintf( rtspcld->session, "%s", trim(temp) );
    if(!(rtspcld->transport=kd_lookup(rkd, "Transport"))){
        ERRMSG("%s: no transport in responce\n",__func__);
        goto erexit;
    }
    if(realloc_memory((void*)&buf,strlen(rtspcld->transport)+1,__func__)) goto erexit;
    strcpy(buf,rtspcld->transport);
    token=strtok(buf,delimiters);
    rtspcld->server_port=0;
    while(token)
    {
        if((pc=strstr(token,"=")))
        {
            *pc=0;
            if(!strcmp(token,"server_port"))
            {
                rtspcld->server_port=atoi(pc+1);
                DBGMSG( "got server port : %d\n", rtspcld->server_port );
            }
            if(!strcmp(token,"control_port"))
            {
                rtspcld->control_port=atoi(pc+1);
                DBGMSG( "got control port : %d\n", rtspcld->control_port );
            }
            if(!strcmp(token,"timing_port"))
            {
                rtspcld->timing_port=atoi(pc+1);
                DBGMSG( "got timing port : %d\n", rtspcld->timing_port );
            }
        }
        token=strtok(NULL,delimiters);
    }
    if(rtspcld->server_port==0){
        ERRMSG("%s: no server_port in response\n",__func__);
        goto erexit;
    }
    rval=0;
 erexit:
    if(buf) free(buf);
    if(rval) {
        free_kd(rkd);
        rkd=NULL;
    }
    *kd=rkd;
    return rval;
}

int rtspcl_record(rtspcl_t *p)
{
    key_data_t hds[3];
    rtspcl_data_t *rtspcld;
    struct timeval now;

    if(!p) return -1;
    rtspcld=(rtspcl_data_t *)p;
    if(!rtspcld->session){
        ERRMSG("%s: no session in progress\n",__func__);
        return -1;
    }
    gettimeofday(&now,NULL);
    hds[0].key=(uint8_t*)"Range";
    hds[0].data=(uint8_t*)"npt=0-";
    hds[1].key=(uint8_t*)"RTP-Info";
    hds[1].data=(uint8_t*)"seq=0;rtptime=0";
    hds[2].key=NULL;
    return exec_request(rtspcld,"RECORD",NULL,NULL,0,1,hds,&rtspcld->kd, NULL);
}    

int rtspcl_set_parameter(rtspcl_t *p, char *para)
{
    rtspcl_data_t *rtspcld;
    
    if(!p) return -1;
    rtspcld=(rtspcl_data_t *)p;
    return exec_request(rtspcld, "SET_PARAMETER", "text/parameters", para, 0, 1, NULL, &rtspcld->kd, NULL);
}

int rtspcl_set_daap(rtspcl_t *p, char *para, unsigned long timestamp, int count)
{
    key_data_t hds[2];
    char rtptime[20];
    rtspcl_data_t *rtspcld;
    
    if(!p) return -1;
    rtspcld=(rtspcl_data_t *)p;
    
    sprintf(rtptime, "rtptime=%ld",timestamp);
    
    hds[0].key=(uint8_t*)"RTP-Info";
    hds[0].data=(uint8_t*)rtptime;
    hds[1].key=NULL;
    
    return exec_request(rtspcld, "SET_PARAMETER", "application/x-dmap-tagged", para, count, 2, hds, &rtspcld->kd, NULL);
}

int rtspcl_options(rtspcl_t *p)
{
    rtspcl_data_t *rtspcld;
    
    if(!p) return -1;
    rtspcld=(rtspcl_data_t *)p;
    return exec_request(rtspcld, "OPTIONS", NULL, NULL, 0, 1, NULL, &rtspcld->kd, "*");
}

int rtspcl_auth_setup(rtspcl_t *p)
{
    rtspcl_data_t *rtspcld;
    /* //itunes second
    char data[] = {
    0x01, 0x80, 0xc3, 0xb3, 0xe8, 0xd6, 0x22, 0xd0, 0x50, 0xeb, 0xd8, 
    0x17, 0x40, 0x11, 0xd8, 0x93, 0x00, 0x55, 0x65, 0xe7, 0x56, 0x43, 
    0x76, 0xff, 0x41, 0x12, 0x84, 0x92, 0xac, 0xfb, 0xec, 0xd4, 0x1d }; */
    //itunes first
    char data[] = {
    0x01, 0xad, 0xb2, 0xa4, 0xc7, 0xd5, 0x5c, 0x97, 0x6c, 0x34, 0xf9, 
    0x2e, 0x0e, 0x05, 0x48, 0x90, 0x3b, 0x3a, 0x2f, 0xc6, 0x72, 0x2b, 
    0x88, 0x58, 0x08, 0x76, 0xd2, 0x9c, 0x61, 0x94, 0x18, 0x52, 0x50 };
    
    if(!p) return -1;
    rtspcld=(rtspcl_data_t *)p;
    return exec_request(rtspcld, "POST", "application/octet-stream", data, 33, 1, NULL, &rtspcld->kd,"/auth-setup");
}
    
int rtspcl_flush(rtspcl_t *p)
{
    key_data_t hds[2];
    rtspcl_data_t *rtspcld;
    
    if(!p) return -1;
    rtspcld=(rtspcl_data_t *)p;
    hds[0].key=(uint8_t*)"RTP-Info";
    hds[0].data=(uint8_t*)"seq=0;rtptime=0";
    hds[1].key=NULL;
    return exec_request(rtspcld, "FLUSH", NULL, NULL, 0, 1, hds, &rtspcld->kd, NULL);
}

int rtspcl_teardown(rtspcl_t *p)
{
    rtspcl_data_t *rtspcld;
    
    if(!p) return -1;
    rtspcld=(rtspcl_data_t *)p;
    return exec_request(rtspcld, "TEARDOWN", NULL, NULL, 0, 1, NULL, &rtspcld->kd, NULL);
}

/*
 * send RTSP request, and get responce if it's needed
 * if this gets a success, *kd is allocated or reallocated (if *kd is not NULL)
 */
static int exec_request(rtspcl_data_t *rtspcld, char *cmd, char *content_type,
                char *content, int length, int get_response, key_data_t *hds, key_data_t **kd, char* url)
{
    char line[2048];
    char req[1024];
    char reql[128];
    const char delimiters[] = " ";
    char *token,*dp;
    int i,j,dsize,rval,totallength;
    int timeout=100; // msec unit

    if(!rtspcld) return -1;

    if(url==NULL)
    {
        sprintf(req, "%s %s RTSP/1.0\r\n",cmd,rtspcld->url);
    }
    else
    {
        sprintf(req, "%s %s RTSP/1.0\r\n",cmd,url);
    }
    i=0;
    while( hds && hds[i].key != NULL )
    {
        sprintf(reql,"%s: %s\r\n", hds[i].key, hds[i].data);
        strncat(req,reql,sizeof(req)-1);
        i++;
    }

    if( content_type && content) 
    {
        if(!length)
        {
            sprintf(reql, "Content-Type: %s\r\nContent-Length: %d\r\n",
            content_type, (int)strlen(content));
        }
        else
        {
            sprintf(reql, "Content-Type: %s\r\nContent-Length: %d\r\n",
            content_type, length);
        }
        strncat(req,reql,sizeof(req)-1);
    }
    
    sprintf(reql,"CSeq: %d\r\n",++rtspcld->cseq);
    strncat(req,reql,sizeof(req)-1);
    
    sprintf(reql, "User-Agent: %s\r\n", rtspcld->useragent );
    strncat(req,reql,sizeof(req)-1);

    i=0;
    while(rtspcld->exthds && rtspcld->exthds[i].key)
    {
        if(rtspcld->exthds[i].key[0]==0xff) {i++;continue;}
        sprintf(reql,"%s: %s\r\n", rtspcld->exthds[i].key, rtspcld->exthds[i].data);
        strncat(req,reql,sizeof(req)-1);
        i++;
    }
    
    if( rtspcld->session != NULL )
    {
        sprintf(reql,"Session: %s\r\n",rtspcld->session);
        strncat(req,reql,sizeof(req)-1);
    }
    
    strncat(req,"\r\n",sizeof(req)-1);

    if( content_type && content)
    {
        if(!length)
        {
            strncat(req,content,sizeof(req)-1);
        }
        else
        {
            totallength = strlen(req) + length;
            memcpy(req+strlen(req),content,length);
        }
    }

    if(!length)
    {
        rval=write(rtspcld->fd,req,strlen(req));
        DBGMSG("----> %s : write %s\n",__func__, req);
        if ( rval != strlen(req) )
        {
           ERRMSG("couldn't write request (%d!=%ld)\n",rval,strlen(req));
        }
    }
    else
    {
        rval=write(rtspcld->fd,req,totallength);
        DBGMSG("----> %s : write %s\n",__func__,req);
        if ( rval != totallength )
        {
           ERRMSG("couldn't write request (%d!=%d)\n",rval,totallength);
        }
    }

    if( !get_response ) return 0;
    
    if(read_line(rtspcld->fd,line,sizeof(line),timeout,0)<=0)    
    {
        if(get_response==1)
        {
            ERRMSG("%s: response : %s request failed\n",__func__, line);
            return -1;
        }
        else
        {
            return 0;
        }
    }

    token = strtok(line, delimiters);
    token = strtok(NULL, delimiters);
    if(token==NULL || strcmp(token,"200"))
    {
        if(get_response==1)
        {
            ERRMSG("<------ : %s: request failed, error %s\n",__func__,line);
            return -1;
        }
    }
    else
    {
        DBGMSG("<------ : %s: request ok (%s)\n",__func__,token);
    }

    i=0;
    while(read_line(rtspcld->fd,line,sizeof(line),timeout,0)>0){
        DBGMSG("<------ : %s\n",line);
        timeout=10; // once it started, it shouldn't take a long time
        if(i%16==0){
            if(realloc_memory((void*)kd,(16*(i/16+1)+1)*sizeof(key_data_t),__func__)) return -1;
            memset(*kd+16*(i/16),0,17*sizeof(key_data_t));
        }

        if(i && line[0]==' '){
            for(j=0;j<strlen(line);j++) if(line[j]!=' ') break;
            dsize+=strlen(line+j);
            if(realloc_memory((void*)&(*kd)[i].data,dsize,__func__)) return -1;
            strcat((char*)(*kd)[i].data,line+j);
            continue;
        }

        dp=strstr(line,":");

        if(!dp){
            ERRMSG("%s: Request failed, bad header\n",__func__);
            free_kd(*kd);
            *kd=NULL;
            return -1;
        }

        *dp=0;
        if(realloc_memory((void*)&(*kd)[i].key,strlen(line)+1,__func__)) return -1;
        strcpy((char*)(*kd)[i].key,line);
        dsize=strlen(dp+1)+1;
        if(realloc_memory((void*)&(*kd)[i].data,dsize,__func__)) return -1;
        strcpy((char*)(*kd)[i].data,dp+1);
        i++;

        if ( strcmp( line, "Audio-Latency" ) == 0 )
        {
           alatency=atoi( dp+2 );
           DBGMSG( "saving latency : %d\n", alatency );
        }
    }
    while(read_line(rtspcld->fd,line,sizeof(line),timeout,0)>0)
    {
        //read body
        DBGMSG("<------ : %s\n",line);
    }
    (*kd)[i].key=NULL;
    return 0;
}

char *ltrim(char *s)
{
    while(isspace(*s)) s++;
    return s;
}

char *rtrim(char *s)
{
    char* back = s + strlen(s);
    while(isspace(*--back));
    *(back+1) = '\0';
    return s;
}

char *trim(char *s)
{
    return rtrim(ltrim(s)); 
}
