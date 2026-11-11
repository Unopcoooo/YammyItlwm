#include <dns_sd.h>
#include <sys/select.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
static const char type[]="_airdrop._tcp";
static double monotonic(void){struct timespec t;int e=clock_gettime(CLOCK_MONOTONIC,&t);assert(!e);return t.tv_sec+t.tv_nsec/1e9;}
static void reply(DNSServiceRef ref,DNSServiceFlags flags,uint32_t index,DNSServiceErrorType error,const char *name,const char *regtype,const char *domain,void *ctx){
 (void)ref;(void)name;(void)regtype;(void)domain;(void)ctx;
 printf("callback flags=%u interface=%u error=%d; callback is not peer or transfer proof\n",flags,index,error);
}
int main(int argc,char **argv){
 if(argc==2&&!strcmp(argv[1],"--self-test")){assert(kDNSServiceFlagsIncludeAWDL==0x100000&&kDNSServiceInterfaceIndexAny==0&&!strcmp(type,"_airdrop._tcp"));puts("PASS public DNS-SD AWDL browse plan; no daemon call or RF");return 0;}
 if(argc!=2||strcmp(argv[1],"--browse")){fputs("usage: --self-test | --browse (one bounded120s IncludeAWDL browse)\n",stderr);return 2;}
 setvbuf(stdout,NULL,_IONBF,0);DNSServiceRef ref=NULL;
 DNSServiceErrorType error=DNSServiceBrowse(&ref,kDNSServiceFlagsIncludeAWDL,kDNSServiceInterfaceIndexAny,type,"local.",reply,NULL);
 printf("DNSServiceBrowse return=%d flags=0x%x; daemon acceptance only, inspect native service-request logs\n",error,kDNSServiceFlagsIncludeAWDL);
 if(error)return 1;int fd=DNSServiceRefSockFD(ref),result=0;if(fd<0||fd>=FD_SETSIZE){DNSServiceRefDeallocate(ref);return 1;}
 const double end=monotonic()+120;
 while(monotonic()<end){double left=end-monotonic();if(left<=0)break;struct timeval wait={(long)left,(int)((left-(long)left)*1000000)};fd_set set;FD_ZERO(&set);FD_SET(fd,&set);
  int n=select(fd+1,&set,NULL,NULL,&wait);if(n<0){if(errno==EINTR)continue;result=1;break;}if(n>0&&(error=DNSServiceProcessResult(ref))){printf("process result=%d\n",error);result=1;break;}
 }
 DNSServiceRefDeallocate(ref);puts("browse deallocated; no register/resolve/file transfer");return result;
}
